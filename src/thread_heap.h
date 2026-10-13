#ifndef THREAD_HEAP_H
#define THREAD_HEAP_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include "block.h"
#include "small.h"
#include "medium.h"

typedef struct remote_node_t { struct remote_node_t* next; } remote_node_t;

typedef struct thread_heap_t {
  small_heap_t          small;
  medium_heap_t         medium;
  struct thread_heap_t* pool_next;

  _Alignas(64) _Atomic(remote_node_t*) remote_head;
} thread_heap_t;

extern __thread thread_heap_t* t_heap;

thread_heap_t* heap_create(void);
thread_heap_t* heap_attach(void);

static inline bool local_free(thread_heap_t* h, void* p, size_t sz) {
  return (sz <= SMALL_MAX_SIZE) ? small_free (&h->small,  p)
    : medium_free(&h->medium, p);
}

static inline void heap_drain_remote(thread_heap_t* h) {
  if (atomic_load_explicit(&h->remote_head, memory_order_relaxed) == NULL) return;

  remote_node_t* n = atomic_exchange_explicit(&h->remote_head, NULL, memory_order_acquire);
  while (n) {
    remote_node_t* next = n->next;
    local_free(h, n, block_size(ptr_to_block(n)));
    n = next;
  }
}

static inline void remote_free(thread_heap_t* owner, void* p) {
  remote_node_t* n    = (remote_node_t*)p;
  remote_node_t* head = atomic_load_explicit(&owner->remote_head, memory_order_relaxed);
  do {
    n->next = head;
  } while (!atomic_compare_exchange_weak_explicit
           (&owner->remote_head, &head, n,
            memory_order_release, memory_order_relaxed));
}

static inline thread_heap_t* get_heap(void) {
  thread_heap_t* h = t_heap;
  if (__builtin_expect(h == NULL, 0)) h = heap_attach();
  return h;
}

#endif // THREAD_HEAP_H
