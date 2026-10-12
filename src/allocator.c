#include "../include/allocator.h"
#include "block.h"
#include "large.h"
#include "medium.h"
#include "os.h"
#include "small.h"
#include <errno.h>
#include <stdbool.h>
#include <string.h>

// Single-threaded for now (see docs/DESIGN.md).
static small_heap_t  g_small;
static medium_heap_t g_medium;
static bool          g_ready = false;

static inline void ensure_init(void) {
  if (g_ready) return;
 
  os_init();
  small_init_heap(&g_small);
  medium_init_heap(&g_medium);
  g_ready = true;

}

typedef enum { HEAP_SMALL, HEAP_MEDIUM, HEAP_LARGE } heap_kind_t;

static inline heap_kind_t heap_of_size(size_t block_sz) {
  if (block_sz <= SMALL_MAX_SIZE)  return HEAP_SMALL;
  if (block_sz <= MEDIUM_MAX_SIZE) return HEAP_MEDIUM;
  return HEAP_LARGE;
}

void* my_malloc(size_t size) {
  if (size == 0) return NULL;
  ensure_init();

  size_t sz = request_to_block_size(size);
  if (sz == 0) {
    errno = ENOMEM;
    return NULL;
  }

  void* ptr;
  if (sz <= SMALL_MAX_SIZE) {
    ptr = small_malloc(&g_small, sz);
  } else if (sz <= MEDIUM_MAX_SIZE) {
    ptr = medium_malloc(&g_medium, sz);
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
 
  case HEAP_MEDIUM:
    old_usable = old_sz - HDR_SIZE;
    if (new_sz > MEDIUM_MIN_SIZE && new_sz <= MEDIUM_MAX_SIZE &&
        medium_try_resize_in_place(&g_medium, p, new_sz)) return p;
    break;
 
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
  if (!p || !g_ready) return;
 
  size_t sz = block_size(ptr_to_block(p));
 
  switch (heap_of_size(sz)) {
  case HEAP_SMALL:  small_free (&g_small,  p); break;
  case HEAP_MEDIUM: medium_free(&g_medium, p); break;
  case HEAP_LARGE:  large_free (p); break;
  }
}
