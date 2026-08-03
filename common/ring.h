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
#define N		  10000000ULL

struct ring {
	#ifndef NO_PADDING
	_Alignas(CACHELINE) 
	#endif
	_Atomic uint32_t head;

	#ifndef NO_PADDING
	_Alignas(CACHELINE)
	#endif
	_Atomic uint32_t tail;

	#ifndef NO_PADDING
	_Alignas(CACHELINE)
	#endif
	unsigned char slots[CAPACITY*SLOT_SIZE];
};

static inline void ring_init(struct ring *r) {
	atomic_init(&r->head, 0);
	atomic_init(&r->tail, 0);
}

static inline int ring_push(struct ring *r, const void *msg) {
	uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
	uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
	if (h - t == CAPACITY) return -1;
	memcpy(&r->slots[(h & MASK) * SLOT_SIZE], msg, SLOT_SIZE);
	atomic_store_explicit(&r->head, h + 1, memory_order_release);
	return 0;
}

static inline int ring_pop(struct ring *r, void *out) {
	uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
	uint32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
	if (h == t) return -1;
	memcpy(out, &r->slots[(t & MASK) * SLOT_SIZE], SLOT_SIZE);
	atomic_store_explicit(&r->tail, t + 1, memory_order_release);
	return 0;
}