#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../common/ring.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    struct ring* r;

    // Producer file comments for longer explanations

    // creates file descriptor with same id as producer one so they point to same object
    int fd = shm_open("/sysipc", O_RDWR, 0);
    if (fd == -1) {
        perror("shm_open");
        return 1;
    }

    // maps the shared memory region into the consumer's address space so it can access the shared ring buffer data
    r = mmap(NULL, sizeof(struct ring), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (r == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    close(fd);

    unsigned char msg[SLOT_SIZE];
    memset(msg, 0, SLOT_SIZE);

    if (ring_pop(r, msg) != 0) {
        perror("pop failed");
        return 1;
    }

    printf("received: %s\n", msg);
    return 0;
}