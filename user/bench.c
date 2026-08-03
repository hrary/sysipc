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


int main(int argc, char *argv[]) {

    int mode = (argc > 1) ? atoi(argv[1]) : 0;

    struct ring* r = mmap(NULL, sizeof(struct ring), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (r == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    struct pair {
        struct ring req, resp;
    };
    struct pair *p = mmap(NULL, sizeof(struct pair), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    unsigned char buf[SLOT_SIZE];


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
                while (ring_pop(r, buf) != 0);
                memcpy(&v, buf, sizeof(v));
                sum += v;
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
        // MODE = 0: throughput benchmark
        uint64_t start = now_ns();
        ring_init(r);
        for (uint64_t i = 0; i<N; i++) {
            memcpy(buf, &i, sizeof(i));
            while (ring_push(r, buf) != 0);
        }
        waitpid(pid, NULL, 0);
        printf("sizeof(struct ring) = %zu\n", sizeof(struct ring));
        uint64_t end = now_ns();
        fprintf(stderr, "Benchmark time: %llu ns = %.2f ms\n", (unsigned long long)(end - start), (end - start) / 1e6);
        printf("%.2f Mmsg/s\n", N / ((end - start) / 1e9) / 1e6);
    }
    else if (mode == 1) {
        // MODE = 1: latency benchmark
        uint64_t *samples = malloc(LN * sizeof(*samples));
        ring_init(&p->req);
        ring_init(&p->resp);
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
}