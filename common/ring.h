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
	unsigned char slots[CAPACITY*SLOT_SIZE];
};

static inline void ring_init(struct ring *r) {
	r->head=0;
	r->tail=0;
}

static inline int ring_push(struct ring *r, const void *msg) {
	if (r->head - r->tail == CAPACITY) return -1;
	memcpy(&r->slots[(r->head & MASK) * SLOT_SIZE], msg, SLOT_SIZE);
	r->head++;
	return 0;
}

static inline int ring_pop(struct ring *r, void *out) {
	if (r->head == r->tail) return -1;
	memcpy(out, &r->slots[(r->tail & MASK) * SLOT_SIZE], SLOT_SIZE);
	r->tail++;
	return 0;
}