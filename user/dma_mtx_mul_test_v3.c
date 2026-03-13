#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include "../include/dma_mtx_mul_ioctl_v3.h"

#define NUM_PROCESSES 3
#define THREADS_PER_PROCESS 4
#define TEST_SECONDS 5
#define RETRY_SLEEP_US 200
#define SUCCESS_SLEEP_US 800

struct thread_stats {
    unsigned long ops;
    unsigned long errors;
    int id;
};

static volatile int stop_flag = 0;

static uint32_t xs32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int alloc_buf_once(int fd, uint32_t *buf_id)
{
    if (ioctl(fd, DMA_MTX_MUL_IOCTL_ALLOC_BUF, buf_id) == 0)
        return 0;
    return -1;
}

static int free_buf_safe(int fd, uint32_t buf_id)
{
    uint32_t tmp = buf_id;
    return ioctl(fd, DMA_MTX_MUL_IOCTL_FREE_BUF, &tmp);
}

static void free_partial_buffers(int fd, uint32_t *buf_a, uint32_t *buf_b, uint32_t *buf_c)
{
    if (*buf_c != UINT32_MAX) {
        free_buf_safe(fd, *buf_c);
        *buf_c = UINT32_MAX;
    }
    if (*buf_b != UINT32_MAX) {
        free_buf_safe(fd, *buf_b);
        *buf_b = UINT32_MAX;
    }
    if (*buf_a != UINT32_MAX) {
        free_buf_safe(fd, *buf_a);
        *buf_a = UINT32_MAX;
    }
}

static int alloc_three_buffers_with_retry(int fd,
                                          uint32_t *buf_a,
                                          uint32_t *buf_b,
                                          uint32_t *buf_c)
{
    *buf_a = UINT32_MAX;
    *buf_b = UINT32_MAX;
    *buf_c = UINT32_MAX;

    while (!stop_flag) {
        if (*buf_a == UINT32_MAX) {
            if (alloc_buf_once(fd, buf_a) < 0) {
                if (errno == ENOSPC) {
                    usleep(RETRY_SLEEP_US);
                    continue;
                }
                return -1;
            }
        }

        if (*buf_b == UINT32_MAX) {
            if (alloc_buf_once(fd, buf_b) < 0) {
                if (errno == ENOSPC) {
                    free_partial_buffers(fd, buf_a, buf_b, buf_c);
                    usleep(RETRY_SLEEP_US);
                    continue;
                }
                free_partial_buffers(fd, buf_a, buf_b, buf_c);
                return -1;
            }
        }

        if (*buf_c == UINT32_MAX) {
            if (alloc_buf_once(fd, buf_c) < 0) {
                if (errno == ENOSPC) {
                    free_partial_buffers(fd, buf_a, buf_b, buf_c);
                    usleep(RETRY_SLEEP_US);
                    continue;
                }
                free_partial_buffers(fd, buf_a, buf_b, buf_c);
                return -1;
            }
        }

        return 0;
    }

    errno = EINTR;
    free_partial_buffers(fd, buf_a, buf_b, buf_c);
    return -1;
}

static int32_t *map_buf_page(int fd, uint32_t buf_id)
{
    void *addr = mmap(NULL,
                      DMA_MTX_MUL_MMAP_STRIDE,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      fd,
                      DMA_MTX_MUL_MMAP_OFFSET(buf_id));
    if (addr == MAP_FAILED)
        return NULL;
    return (int32_t *)addr;
}

static void cpu_matmul_2x2(const int32_t *a, const int32_t *b, int32_t *c)
{
    c[0] = a[0] * b[0] + a[1] * b[2];
    c[1] = a[0] * b[1] + a[1] * b[3];
    c[2] = a[2] * b[0] + a[3] * b[2];
    c[3] = a[2] * b[1] + a[3] * b[3];
}

static void fill_rand_2x2(uint32_t *seed, int32_t *m)
{
    for (int i = 0; i < 4; i++)
        m[i] = (int32_t)(xs32(seed) % 11) - 5;
}

static void *thread_fn(void *arg)
{
    struct thread_stats *st = arg;
    uint32_t seed = 0x12345678u + (uint32_t)st->id * 0x9e3779b9u;
    int fd = open("/dev/dma_mtx_mul", O_RDWR);

    if (fd < 0) {
        perror("open");
        st->errors++;
        return NULL;
    }

    while (!stop_flag) {
        uint32_t buf_a = UINT32_MAX;
        uint32_t buf_b = UINT32_MAX;
        uint32_t buf_c = UINT32_MAX;
        struct dma_mtx_mul_desc desc;
        struct dma_mtx_mul_completion done;
        int32_t gold[4];
        int32_t *map_a = NULL;
        int32_t *map_b = NULL;
        int32_t *map_c = NULL;

        if (alloc_three_buffers_with_retry(fd, &buf_a, &buf_b, &buf_c) < 0) {
            if (!stop_flag) {
                perror("ALLOC_BUF");
                st->errors++;
            }
            break;
        }

        map_a = map_buf_page(fd, buf_a);
        map_b = map_buf_page(fd, buf_b);
        map_c = map_buf_page(fd, buf_c);
        if (!map_a || !map_b || !map_c) {
            perror("mmap");
            st->errors++;
            if (map_a)
                munmap(map_a, DMA_MTX_MUL_MMAP_STRIDE);
            if (map_b)
                munmap(map_b, DMA_MTX_MUL_MMAP_STRIDE);
            if (map_c)
                munmap(map_c, DMA_MTX_MUL_MMAP_STRIDE);
            free_partial_buffers(fd, &buf_a, &buf_b, &buf_c);
            usleep(RETRY_SLEEP_US);
            continue;
        }

        fill_rand_2x2(&seed, map_a);
        fill_rand_2x2(&seed, map_b);
        cpu_matmul_2x2(map_a, map_b, gold);
        memset(map_c, 0, DMA_MTX_MUL_BUF_BYTES);

        memset(&desc, 0, sizeof(desc));
        desc.m = 2;
        desc.k = 2;
        desc.n = 2;
        desc.buf_a = buf_a;
        desc.buf_b = buf_b;
        desc.buf_c = buf_c;

        if (write(fd, &desc, sizeof(desc)) != (ssize_t)sizeof(desc)) {
            perror("write(desc)");
            st->errors++;
            munmap(map_a, DMA_MTX_MUL_MMAP_STRIDE);
            munmap(map_b, DMA_MTX_MUL_MMAP_STRIDE);
            munmap(map_c, DMA_MTX_MUL_MMAP_STRIDE);
            free_partial_buffers(fd, &buf_a, &buf_b, &buf_c);
            usleep(RETRY_SLEEP_US);
            continue;
        }

        memset(&done, 0, sizeof(done));
        if (read(fd, &done, sizeof(done)) != (ssize_t)sizeof(done)) {
            perror("read(done)");
            st->errors++;
            munmap(map_a, DMA_MTX_MUL_MMAP_STRIDE);
            munmap(map_b, DMA_MTX_MUL_MMAP_STRIDE);
            munmap(map_c, DMA_MTX_MUL_MMAP_STRIDE);
            free_partial_buffers(fd, &buf_a, &buf_b, &buf_c);
            usleep(RETRY_SLEEP_US);
            continue;
        }

        if (done.status != DMA_MTX_MUL_STATUS_OK) {
            fprintf(stderr, "thread %d: completion status=%d\n", st->id, done.status);
            st->errors++;
            munmap(map_a, DMA_MTX_MUL_MMAP_STRIDE);
            munmap(map_b, DMA_MTX_MUL_MMAP_STRIDE);
            munmap(map_c, DMA_MTX_MUL_MMAP_STRIDE);
            free_partial_buffers(fd, &buf_a, &buf_b, &buf_c);
            usleep(RETRY_SLEEP_US);
            continue;
        }

        if (done.desc.buf_a != buf_a || done.desc.buf_b != buf_b || done.desc.buf_c != buf_c) {
            fprintf(stderr, "thread %d: done descriptor mismatch\n", st->id);
            st->errors++;
            munmap(map_a, DMA_MTX_MUL_MMAP_STRIDE);
            munmap(map_b, DMA_MTX_MUL_MMAP_STRIDE);
            munmap(map_c, DMA_MTX_MUL_MMAP_STRIDE);
            free_partial_buffers(fd, &buf_a, &buf_b, &buf_c);
            usleep(RETRY_SLEEP_US);
            continue;
        }

        if (memcmp(map_c, gold, 4 * sizeof(int32_t)) != 0) {
            fprintf(stderr,
                    "thread %d: result mismatch: [%d %d; %d %d] != [%d %d; %d %d]\n",
                    st->id,
                    map_c[0], map_c[1], map_c[2], map_c[3],
                    gold[0], gold[1], gold[2], gold[3]);
            st->errors++;
            munmap(map_a, DMA_MTX_MUL_MMAP_STRIDE);
            munmap(map_b, DMA_MTX_MUL_MMAP_STRIDE);
            munmap(map_c, DMA_MTX_MUL_MMAP_STRIDE);
            free_partial_buffers(fd, &buf_a, &buf_b, &buf_c);
            usleep(RETRY_SLEEP_US);
            continue;
        }

        st->ops++;
        munmap(map_a, DMA_MTX_MUL_MMAP_STRIDE);
        munmap(map_b, DMA_MTX_MUL_MMAP_STRIDE);
        munmap(map_c, DMA_MTX_MUL_MMAP_STRIDE);
        free_partial_buffers(fd, &buf_a, &buf_b, &buf_c);
        usleep(SUCCESS_SLEEP_US);
    }

    close(fd);
    return NULL;
}

static int run_one_process(int proc_idx)
{
    pthread_t threads[THREADS_PER_PROCESS];
    struct thread_stats stats[THREADS_PER_PROCESS];
    unsigned long total_ops = 0;
    unsigned long total_errors = 0;

    memset(stats, 0, sizeof(stats));
    for (int i = 0; i < THREADS_PER_PROCESS; i++) {
        stats[i].id = proc_idx * 100 + i;
        if (pthread_create(&threads[i], NULL, thread_fn, &stats[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    sleep(TEST_SECONDS);
    stop_flag = 1;

    for (int i = 0; i < THREADS_PER_PROCESS; i++)
        pthread_join(threads[i], NULL);

    for (int i = 0; i < THREADS_PER_PROCESS; i++) {
        printf("process %d thread %d: ops=%lu errors=%lu\n",
               proc_idx, i, stats[i].ops, stats[i].errors);
        total_ops += stats[i].ops;
        total_errors += stats[i].errors;
    }

    printf("process %d TOTAL: ops=%lu errors=%lu\n",
           proc_idx, total_ops, total_errors);
    return total_errors == 0 ? 0 : 1;
}

int main(void)
{
    pid_t pids[NUM_PROCESSES];
    int overall_fail = 0;

    for (int i = 0; i < NUM_PROCESSES; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid == 0)
            _exit(run_one_process(i));
        pids[i] = pid;
    }

    for (int i = 0; i < NUM_PROCESSES; i++) {
        int status;
        if (waitpid(pids[i], &status, 0) < 0) {
            perror("waitpid");
            overall_fail = 1;
            continue;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            overall_fail = 1;
    }

    printf("FINAL RESULT: %s\n", overall_fail ? "FAIL" : "PASS");
    return overall_fail ? 1 : 0;
}