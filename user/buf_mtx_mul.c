#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>

#include "../include/mtx_mul_ioctl.h"

static void print_matrix_buf(const char *name, struct mtx_mul_buffer *b, uint32_t rows, uint32_t cols)
{
    uint32_t i, j;

    printf("%s (buf %u) =\n", name, b->buf_id);
    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            printf("%4d ", b->data[i * cols + j]);
        }
        printf("\n");
    }
    printf("\n");
}

int main(void)
{
    int fd;
    uint32_t buf_a, buf_b, buf_c;
    struct mtx_mul_buffer a, b, c;
    struct mtx_mul_desc desc;

    fd = open("/dev/mtx_mul", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ioctl(fd, MTX_MUL_IOCTL_ALLOC_BUF, &buf_a) < 0 ||
        ioctl(fd, MTX_MUL_IOCTL_ALLOC_BUF, &buf_b) < 0 ||
        ioctl(fd, MTX_MUL_IOCTL_ALLOC_BUF, &buf_c) < 0) {
        perror("ioctl(ALLOC_BUF)");
        close(fd);
        return 1;
    }

    printf("Allocated buffers: A=%u B=%u C=%u\n", buf_a, buf_b, buf_c);

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));

    a.buf_id = buf_a;
    a.num_elems = 4;
    a.data[0] = 1; a.data[1] = 2;
    a.data[2] = 3; a.data[3] = 4;

    b.buf_id = buf_b;
    b.num_elems = 4;
    b.data[0] = 5; b.data[1] = 6;
    b.data[2] = 7; b.data[3] = 8;

    c.buf_id = buf_c;
    c.num_elems = 0;

    if (write(fd, &a, sizeof(a)) < 0 ||
        write(fd, &b, sizeof(b)) < 0 ||
        write(fd, &c, sizeof(c)) < 0) {
        perror("write");
        close(fd);
        return 1;
    }

    print_matrix_buf("A", &a, 2, 2);
    print_matrix_buf("B", &b, 2, 2);

    memset(&desc, 0, sizeof(desc));
    desc.m = 2;
    desc.k = 2;
    desc.n = 2;
    desc.buf_a = buf_a;
    desc.buf_b = buf_b;
    desc.buf_c = buf_c;

    if (ioctl(fd, MTX_MUL_IOCTL_SUBMIT_DESC, &desc) < 0) {
        perror("ioctl(SUBMIT_DESC)");
        close(fd);
        return 1;
    }

    if (ioctl(fd, MTX_MUL_IOCTL_WAIT) < 0) {
        perror("ioctl(WAIT)");
        close(fd);
        return 1;
    }

    memset(&c, 0, sizeof(c));
    c.buf_id = buf_c;

    if (read(fd, &c, sizeof(c)) < 0) {
        perror("read");
        close(fd);
        return 1;
    }

    print_matrix_buf("C", &c, 2, 2);

    ioctl(fd, MTX_MUL_IOCTL_FREE_BUF, &buf_a);
    ioctl(fd, MTX_MUL_IOCTL_FREE_BUF, &buf_b);
    ioctl(fd, MTX_MUL_IOCTL_FREE_BUF, &buf_c);

    close(fd);
    return 0;
}