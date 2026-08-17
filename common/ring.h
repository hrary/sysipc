#pragma once

#ifdef __KERNEL__
  #include <linux/types.h>
  #include <linux/compiler.h>
  #include <linux/string.h>
  typedef u32 ring_idx_t;
#else
	#include <stdint.h>
	#include <stdatomic.h>
	#include <string.h>
	typedef _Atomic uint32_t ring_idx_t;
#endif

#define CACHELINE 64
#ifndef CAPACITY
#define CAPACITY 1024
#endif
#define SLOT_SIZE 64
#define MASK      (CAPACITY - 1)
#define N		  10000000ULL
#define SYSIPC_IOC_MAGIC 'S'
#define SYSIPC_KICK      _IO(SYSIPC_IOC_MAGIC, 1)

struct ring {
	_Alignas(CACHELINE) ring_idx_t head;
    _Alignas(CACHELINE) ring_idx_t tail;
    _Alignas(CACHELINE) unsigned char slots[CAPACITY * SLOT_SIZE];
};

#ifndef __KERNEL__
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
#endif

#ifdef __KERNEL__
static inline int ring_readable(const struct ring *r) {
	return READ_ONCE(r->head) != READ_ONCE(r->tail);
}

static inline int ring_writeable(const struct ring *r) {
	return READ_ONCE(r->head) - READ_ONCE(r->tail) != CAPACITY;
}
#else
static inline int ring_readable(const struct ring *r) {
	return atomic_load_explicit(&r->head, memory_order_acquire) != atomic_load_explicit(&r->tail, memory_order_relaxed);
}

static inline int ring_writeable(const struct ring *r) {
	return atomic_load_explicit(&r->head, memory_order_relaxed) - atomic_load_explicit(&r->tail, memory_order_acquire) != CAPACITY;
}
#endif
