#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(void)
{
    int fd = open("/dev/mtx_mul", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    printf("Opened /dev/mtx_mul successfully\n");

    close(fd);
    printf("Closed /dev/mtx_mul successfully\n");

    return 0;
}