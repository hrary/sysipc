#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../common/ring.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>

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
        int spins = 0;
         while (ring_pop(r, msg) != 0) {
            if (++spins < SPIN_LIMIT)
                continue; 

            atomic_store_explicit(&r->consumer_waiting, 1, memory_order_relaxed); // set consumer_waiting to 1 to indicate that the consumer is waiting for data
            atomic_thread_fence(memory_order_seq_cst); // ensure that the store to consumer_waiting is visible to other threads before proceeding
            
            if (ring_readable(r)) { // re-check
                atomic_store_explicit(&r->consumer_waiting, 0, memory_order_relaxed);
                continue;
            }

            struct pollfd pfd = {
                .fd = fd,
                .events = POLLIN,
            };
            
            poll(&pfd, 1, 10); // wait for data to be available in the ring buffer

            atomic_store_explicit(&r->consumer_waiting, 0, memory_order_relaxed);

            spins = 0;
        }
        memcpy(&received, msg, sizeof(received));
        assert(received == expected);
        spins = 0;
        expected++;
        if (received >= N-1) break;
    }
    close(fd);
    printf("consumer last received: %lu\n", received);
    return 0;
}