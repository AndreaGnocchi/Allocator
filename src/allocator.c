#include "../include/allocator.h"
#include "block.h"
#include "large.h"
#include "medium.h"
#include "small.h"
#include "thread_heap.h"
#include <errno.h>
#include <stdbool.h>
#include <string.h>

typedef enum { HEAP_SMALL, HEAP_MEDIUM, HEAP_LARGE } heap_kind_t;

static inline heap_kind_t heap_of_size(size_t block_sz) {
  if (block_sz <= SMALL_MAX_SIZE)  return HEAP_SMALL;
  if (block_sz <= MEDIUM_MAX_SIZE) return HEAP_MEDIUM;
  return HEAP_LARGE;
}

void* my_malloc(size_t size) {
  if (size == 0) return NULL;
  
  size_t sz = request_to_block_size(size);
  if (sz == 0) {
    errno = ENOMEM;
    return NULL;
  }

  void* ptr;
  if (sz <= MEDIUM_MAX_SIZE) {
    thread_heap_t* h  = get_heap();
    if (!h) {
      errno = ENOMEM;
      return NULL;
    }
    
    heap_drain_remote(h);
    if (sz <= SMALL_MAX_SIZE) {
      ptr = small_malloc(&h->small, sz);
    } else {
      ptr = medium_malloc(&h->medium, sz);
    }
  } else {
    ptr = large_malloc(size < LARGE_MIN_SIZE ? LARGE_MIN_SIZE : size);
  }

  if (!ptr) errno = ENOMEM;
  return ptr;
}

void* my_calloc(size_t n, size_t size) {
  if (n != 0 && size > SIZE_MAX / n) {
    errno = ENOMEM;
    return NULL;
  }
 
  size_t tot_size = n * size;

  void* ptr = my_malloc(tot_size);
  if (!ptr) return NULL;
 
  size_t sz = request_to_block_size(tot_size);
  if (sz <= MEDIUM_MAX_SIZE)
    memset(ptr, 0, tot_size);
 
  return ptr;
}

void* my_realloc(void* p, size_t size) {
  if (!p) return my_malloc(size);
  if (size == 0) {
    my_free(p);
    return NULL;
  }

  size_t      new_sz = request_to_block_size(size);
  size_t      old_sz = block_size(ptr_to_block(p));
  heap_kind_t kind   = heap_of_size(old_sz);
 
  size_t old_usable;
  switch (kind) {
  case HEAP_SMALL:
    old_usable = old_sz - HDR_SIZE;
    if (new_sz <= SMALL_MAX_SIZE && small_try_resize_in_place(p, new_sz)) return p;
    break;
 
  case HEAP_MEDIUM: {
    old_usable          = old_sz - HDR_SIZE;
    thread_heap_t* self = t_heap;
    if (new_sz > MEDIUM_MIN_SIZE && new_sz <= MEDIUM_MAX_SIZE &&
        self && chunk_of(p, MEDIUM_CHUNK_SIZE)->owner == self &&
        medium_try_resize_in_place(&self->medium, p, new_sz)) return p;
  } break;
 
  case HEAP_LARGE:
  default:
    old_usable = large_usable_size(p);
    if (new_sz > MEDIUM_MAX_SIZE && size <= old_usable && size >= old_usable / 2) return p;
    break;
  }
 
  void* np = my_malloc(size);
  if (!np) return NULL;
 
  memcpy(np, p, size < old_usable ? size : old_usable);
  my_free(p);
  return np;
}

void my_free(void* p) {
  if (!p) return;
 
  size_t sz = block_size(ptr_to_block(p));
  if (sz > MEDIUM_MAX_SIZE) {
    large_free(p);
    return;
  }
  
  size_t csz = (sz <= SMALL_MAX_SIZE) ? SMALL_CHUNK_SIZE : MEDIUM_CHUNK_SIZE;
  thread_heap_t* owner = chunk_of(p, csz)->owner;
  
  if (owner == t_heap) local_free(owner, p, sz);
  else                 remote_free(owner, p); 
}
