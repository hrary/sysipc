#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../common/ring.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

int main(void) {
    struct ring* r;

    // Producer file comments for longer explanations

    // creates file descriptor with same id as producer one so they point to same object
    int fd = open("/dev/sysipc", O_RDWR);
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

    unsigned char msg[SLOT_SIZE];
    memset(msg, 0, SLOT_SIZE);
    uint64_t expected = 0, received;

    while (1) {
         while (ring_pop(r, msg) != 0) {
            struct pollfd pfd = {
                .fd = fd,
                .events = POLLIN,
            };
            poll(&pfd, 1, -1); // wait for data to be available in the ring buffer
        }
        memcpy(&received, msg, sizeof(received));
        assert(received == expected);
        expected++;
        if (received >= N-1) break;
    }
    close(fd);
    printf("consumer last received: %lu\n", received);
    return 0;
}