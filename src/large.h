#ifndef LARGE_H
#define LARGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "block.h"

#define LARGE_MIN_SIZE  ((size_t)(128 << 10) + 1)
#define LARGE_OVERHEAD  (sizeof(block_t) + HDR_SIZE)

typedef struct {
  block_t* head;
} large_heap_t;

void   large_init_heap  (large_heap_t* heap);
void*  large_malloc     (large_heap_t* heap, size_t size);
bool   large_free       (large_heap_t* heap, void*  ptr);
void   large_teardown   (large_heap_t* heap);
size_t large_usable_size(const void* ptr);

#endif // LARGE_H
