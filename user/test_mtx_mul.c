#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

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

static void print_status(int fd)
{
    struct mtx_mul_status st;

    if (ioctl(fd, MTX_MUL_IOCTL_GET_STATUS, &st) < 0) {
        perror("ioctl(GET_STATUS)");
        return;
    }

    printf("STATUS: control=0x%x status=0x%x irq_status=0x%x job_pending=%u\n",
           st.control, st.status, st.irq_status, st.job_pending);
}

int main(void)
{
    int fd;
    struct mtx_mul_job job = {0};
    struct mtx_mul_job result = {0};

    fd = open("/dev/mtx_mul", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    job.m = 2;
    job.k = 2;
    job.n = 2;

    /* A = [1 2; 3 4] */
    job.a[0] = 1; job.a[1] = 2;
    job.a[2] = 3; job.a[3] = 4;

    /* B = [5 6; 7 8] */
    job.b[0] = 5; job.b[1] = 6;
    job.b[2] = 7; job.b[3] = 8;

    print_matrix("A", job.a, job.m, job.k);
    print_matrix("B", job.b, job.k, job.n);

    print_status(fd);

    if (ioctl(fd, MTX_MUL_IOCTL_SUBMIT, &job) < 0) {
        perror("ioctl(SUBMIT)");
        close(fd);
        return 1;
    }

    printf("Job submitted\n");
    print_status(fd);

    if (ioctl(fd, MTX_MUL_IOCTL_WAIT) < 0) {
        perror("ioctl(WAIT)");
        close(fd);
        return 1;
    }

    printf("Job completed\n");
    print_status(fd);

    if (ioctl(fd, MTX_MUL_IOCTL_GET_RESULT, &result) < 0) {
        perror("ioctl(GET_RESULT)");
        close(fd);
        return 1;
    }

    print_matrix("C", result.c, result.m, result.n);

    print_status(fd);

    close(fd);
    return 0;
}