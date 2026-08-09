#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    int fd = open("/dev/sysipc", O_RDWR);
    if (fd == -1) { perror("open"); return 1; }
    printf("opened\n");
    close(fd);
}