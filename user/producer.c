#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../common/ring.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    struct ring *r;

    int file_descriptor = shm_open("/sysipc", O_CREAT | O_RDWR, 0666); 
    // creates shared memory object and returns a file descriptor, which is 
    // an int the kernel uses to identify an open resource. Other processes can 
    // look for "/sysipc"
    if (file_descriptor == -1) {
        perror("shm_open");
        return 1;
    } // error check

    if (ftruncate(file_descriptor, sizeof(struct ring)) == -1) {
        perror("ftruncate");
        return 1;
    };
    // sizes the file descriptor object

    r = mmap(NULL, sizeof(struct ring), PROT_READ | PROT_WRITE, MAP_SHARED, file_descriptor, 0);
    /** mmap "maps a file or device directly into a program's memory"
        so the order:
        1. we create a shared memory region identified by the file descriptor, and give it the right size
        2. the ring buffer is a ptr in our own address space -> mmap returns a ptr to our address space
        3. behind the scenes, mmap asks the kernel to point part of my address space to the physical memory of the shm
    */
    if (r == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    close(file_descriptor); // close the file descriptor, we don't need it anymore

    ring_init(r);

    unsigned char msg[SLOT_SIZE];
    memset(msg, 0, SLOT_SIZE);
    uint64_t i;

    for (i = 0; i < N; i++) {
        memcpy(msg, &i, sizeof(i));
        while (ring_push(r, msg) != 0);
    }

    printf("producer last sent: %lu\n", i - 1);
    return 0;
}