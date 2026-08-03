#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <stdint.h>
#include "../common/ring.h"


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

// Helper function to guarantee all bytes are read from the socket
ssize_t read_all(int fd, void *buf, size_t count) {
    size_t total = 0;
    char *ptr = (char *)buf;
    while (total < count) {
        ssize_t n = read(fd, ptr + total, count - total);
        if (n <= 0) {
            if (n < 0) perror("read");
            return -1;
        }
        total += n;
    }
    return total;
}

// Helper function to guarantee all bytes are written to the socket
ssize_t write_all(int fd, const void *buf, size_t count) {
    size_t total = 0;
    const char *ptr = (const char *)buf;
    while (total < count) {
        ssize_t n = write(fd, ptr + total, count - total);
        if (n <= 0) {
            if (n < 0) perror("write");
            return -1;
        }
        total += n;
    }
    return total;
}

int main(int argc, char *argv[]) {
    int mode = (argc > 1) ? atoi(argv[1]) : 0;
    
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        perror("socketpair");
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
        close(fds[0]); // Close parent side
        int sfd = fds[1];

        // MODE = 0: throughput benchmark
        if (mode == 0) {
            uint64_t sum = 0, v;
            for (uint64_t i = 0; i < N; i++) {
                if (read_all(sfd, buf, SLOT_SIZE) < 0) _exit(1);
                memcpy(&v, buf, sizeof(v));
                sum += v;
            }
            fprintf(stderr, "checksum %llu\n", (unsigned long long)sum);
            close(sfd);
            _exit(0);
        }
        // MODE = 1: latency benchmark
        else if (mode == 1) {
            for (uint64_t i = 0; i < LN; i++) {
                if (read_all(sfd, buf, SLOT_SIZE) < 0) _exit(1);
                if (write_all(sfd, buf, SLOT_SIZE) < 0) _exit(1);
            }
            close(sfd);
            _exit(0);
        }
    }

    // Parent process: producer
    close(fds[1]); // Close child side
    int sfd = fds[0];

    if (mode == 0) {
        // MODE = 0: throughput benchmark
        uint64_t start = now_ns();
        
        for (uint64_t i = 0; i < N; i++) {
            memcpy(buf, &i, sizeof(i));
            if (write_all(sfd, buf, SLOT_SIZE) < 0) break;
        }
        
        close(sfd);
        waitpid(pid, NULL, 0);
        
        uint64_t end = now_ns();
        fprintf(stderr, "Benchmark time: %llu ns = %.2f ms\n", (unsigned long long)(end - start), (end - start) / 1e6);
        printf("%.2f Mmsg/s\n", N / ((end - start) / 1e9) / 1e6);
    } 
    else if (mode == 1) {
        // MODE = 1: latency benchmark
        uint64_t *samples = malloc(LN * sizeof(*samples));
        
        for (uint64_t i = 0; i < LN; i++) {
            uint64_t start = now_ns();
            memcpy(buf, &start, sizeof(start));
            
            if (write_all(sfd, buf, SLOT_SIZE) < 0) break;
            if (read_all(sfd, buf, SLOT_SIZE) < 0) break;
            
            uint64_t end = now_ns();
            samples[i] = end - start;
        }
        
        close(sfd);
        waitpid(pid, NULL, 0);
        
        qsort(samples, LN, sizeof(*samples), compare);
        fprintf(stderr, "p50: %llu ns\np99: %llu ns\n", (unsigned long long)(samples[LN/2]), (unsigned long long)(samples[99*LN/100]));
        free(samples);
    }
    return 0;
}
