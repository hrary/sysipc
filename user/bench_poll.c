#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "../common/ring.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <poll.h>

#define LN N/100


uint64_t now_ns() {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        uint64_t nanoseconds = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
        return nanoseconds;
    }
    perror("clock_gettime");
    return 0;
}

int compare (const void * a, const void * b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

struct pair {
        struct ring req, resp;
};


int main(int argc, char *argv[]) {

    int mode = (argc > 1) ? atoi(argv[1]) : 0;

    int fd = open("/dev/sysipc", O_RDWR);
    if (fd == -1) { perror("open"); return 1; }

    struct pair *p = mmap(NULL, sizeof(struct pair), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    struct ring *r = &p->req;

    unsigned char buf[SLOT_SIZE];

    ring_init(&p->req);
    ring_init(&p->resp);

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return 1;
    } 
    
    else if (pid == 0) {
        // Child process: consumer
        
        // MODE = 0: throughput benchmark
        if (mode == 0) {
            uint64_t sum = 0, v;
            for (uint64_t i = 0; i<N; i++) {
                int spins = 0;
                while (ring_pop(r, buf) != 0) {
                    if (++spins < SPIN_LIMIT)
                        continue;  
                    atomic_store_explicit(&r->consumer_waiting, 1, memory_order_relaxed);
                    atomic_thread_fence(memory_order_seq_cst);

                    if (ring_readable(r)) { // re-check after announcing
                        atomic_store_explicit(&r->consumer_waiting, 0, memory_order_relaxed);
                        continue;
                    }

                    struct pollfd pfd = { .fd = fd, .events = POLLIN };
                    poll(&pfd, 1, 10);
                    atomic_store_explicit(&r->consumer_waiting, 0, memory_order_relaxed);
                    spins = 0;
                }
                memcpy(&v, buf, sizeof(v));
                sum += v;
                spins = 0;
            }
            fprintf(stderr, "checksum %llu\n", (unsigned long long)sum);
            _exit(0);
        }
        // MODE = 1: latency benchmark
        else if (mode == 1) {
            for (uint64_t i = 0; i < LN; i++) {
                while (ring_pop(&p->req, buf) != 0) ;
                while (ring_push(&p->resp, buf) != 0) ;
            }
            _exit(0);
        }
    }

    if (mode == 0) {
        uint64_t kicks = 0;
        // MODE = 0: throughput benchmark
        uint64_t start = now_ns();
        for (uint64_t i = 0; i<N; i++) {
            memcpy(buf, &i, sizeof(i));
            while (ring_push(r, buf) != 0);
            atomic_thread_fence(memory_order_seq_cst);
            if (atomic_load_explicit(&r->consumer_waiting, memory_order_relaxed)) {
                kicks++;
                ioctl(fd, SYSIPC_KICK);
            }
        }
        waitpid(pid, NULL, 0);
        uint64_t end = now_ns();
        printf("sizeof(struct ring) = %zu\n", sizeof(struct ring));
        fprintf(stderr, "Benchmark time: %llu ns = %.2f ms\n", (unsigned long long)(end - start), (end - start) / 1e6);
        printf("%.2f Mmsg/s\n", N / ((end - start) / 1e9) / 1e6);
        fprintf(stderr, "kicks: %llu / %llu\n", (unsigned long long)kicks, (unsigned long long)N);
    }
    else if (mode == 1) {
        // MODE = 1: latency benchmark
        uint64_t *samples = malloc(LN * sizeof(*samples));
        for (uint64_t i = 0; i<LN; i++) {
            uint64_t start = now_ns();
            memcpy(buf, &start, sizeof(start));
            while (ring_push(&p->req, buf) != 0);
            while (ring_pop(&p->resp, buf) != 0);
            uint64_t end = now_ns();
            samples[i] = end - start;
        }
        waitpid(pid, NULL, 0);
        qsort(samples, LN, sizeof(*samples), compare);
        fprintf(stderr, "p50: %llu ns\np99: %llu ns\n", (unsigned long long)(samples[LN/2]), (unsigned long long)(samples[99*LN/100]));
        free(samples);
    }
    close(fd);
}