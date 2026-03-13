#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <string.h>

#include "mtx_mul_ioctl.h"

static void print_matrix(const char *name, int32_t *mat, uint32_t rows, uint32_t cols)
{
    uint32_t i, j;

    printf("%s =\n", name);
    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            printf("%4d ", mat[i * cols + j]);
        }
        printf("\n");
    }
    printf("\n");
}

static void print_slot_state(uint32_t s)
{
    switch (s) {
    case MTX_MUL_SLOT_FREE:    printf("FREE"); break;
    case MTX_MUL_SLOT_LOADED:  printf("LOADED"); break;
    case MTX_MUL_SLOT_PENDING: printf("PENDING"); break;
    case MTX_MUL_SLOT_DONE:    printf("DONE"); break;
    default:                   printf("UNKNOWN"); break;
    }
}

int main(void)
{
    int fd;
    struct mtx_mul_slot_job sj;
    struct mtx_mul_slot_status st;
    uint32_t slot_id = 0;
    ssize_t n;

    memset(&sj, 0, sizeof(sj));
    sj.slot_id = slot_id;

    fd = open("/dev/mtx_mul", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    sj.job.m = 2;
    sj.job.k = 2;
    sj.job.n = 2;

    sj.job.a[0] = 1; sj.job.a[1] = 2;
    sj.job.a[2] = 3; sj.job.a[3] = 4;

    sj.job.b[0] = 5; sj.job.b[1] = 6;
    sj.job.b[2] = 7; sj.job.b[3] = 8;

    n = write(fd, &sj, sizeof(sj));
    if (n < 0) {
        perror("write");
        close(fd);
        return 1;
    }

    printf("Loaded slot %u\n", slot_id);

    if (ioctl(fd, MTX_MUL_IOCTL_SUBMIT_SLOT, &slot_id) < 0) {
        perror("ioctl(SUBMIT_SLOT)");
        close(fd);
        return 1;
    }

    printf("Submitted slot %u\n", slot_id);

    if (ioctl(fd, MTX_MUL_IOCTL_WAIT_SLOT, &slot_id) < 0) {
        perror("ioctl(WAIT_SLOT)");
        close(fd);
        return 1;
    }

    printf("Completed slot %u\n", slot_id);

    st.slot_id = slot_id;
    if (ioctl(fd, MTX_MUL_IOCTL_GET_SLOT_STATUS, &st) < 0) {
        perror("ioctl(GET_SLOT_STATUS)");
        close(fd);
        return 1;
    }

    printf("Slot %u state: ", st.slot_id);
    print_slot_state(st.slot_state);
    printf("\n");

    memset(&sj, 0, sizeof(sj));
    sj.slot_id = slot_id;

    n = read(fd, &sj, sizeof(sj));
    if (n < 0) {
        perror("read");
        close(fd);
        return 1;
    }

    print_matrix("C", sj.job.c, sj.job.m, sj.job.n);

    close(fd);
    return 0;
}