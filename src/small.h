#ifndef SMALL_H
#define SMALL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "block.h"

#define SMALL_MAX_SIZE    4096
#define SMALL_NUM_CLASSES 47
#define SMALL_CHUNK_SIZE  ((size_t)64 << 10)
#define SMALL_CHUNK_HDR   sizeof(void*)

typedef bin_t small_bin_t;

typedef struct {
  small_bin_t bins[SMALL_NUM_CLASSES];
  char*       cur;
  size_t      left;
  void*       chunks;
} small_heap_t;

// Bins
block_t* small_pop_free         (small_heap_t* heap, int class_idx);
size_t   get_small_class_size   (int           class_idx);
void     small_init_heap        (small_heap_t* heap);
bool     small_add_free_block   (small_heap_t* heap, block_t* b, int class_idx);
bool     small_remove_free_block(small_heap_t* heap, block_t* b, int class_idx);
int      get_small_class        (size_t        n);

// Allocation.
void*    small_malloc             (small_heap_t* heap, size_t block_size);
bool     small_free               (small_heap_t* heap, void*  ptr);
void     small_teardown           (small_heap_t* heap);
bool     small_try_resize_in_place(void*         ptr,  size_t new_block_size);

#endif // SMALL_H
