#pragma once

#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#define CACHELINE 64
#ifndef CAPACITY
#define CAPACITY 1024
#endif
#define SLOT_SIZE 64
#define MASK      (CAPACITY - 1)

struct ring {
	uint32_t head;
	uint32_t tail;
	int data[CAPACITY];
};

static inline void ring_init(struct ring *r) {
	r->head=0;
	r->tail=0;
}

static inline int ring_push(struct ring *r, const void *msg) {
	if (r->head - r->tail == CAPACITY) return -1;
	r->data[r->head & MASK] = *(const int *)msg;
	r->head++;
	return 0;
}

static inline int ring_pop(struct ring *r, void *out) {
	if (r->head == r->tail) return -1;
	*(int *)out = r->data[r->tail & MASK];
	r->tail++;
	return 0;
}