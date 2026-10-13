#ifndef MEDIUM_H
#define MEDIUM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "block.h"

#define MEDIUM_MIN_SIZE       ((size_t)4096)
#define MEDIUM_MAX_SIZE       ((size_t)128 << 10)
#define MEDIUM_CHUNK_SIZE     ((size_t)1 << 20)
#define MEDIUM_CHUNK_HDR      sizeof(chunk_hdr_t*)
#define MEDIUM_CHUNK_SENTINEL HDR_SIZE
#define MEDIUM_NUM_CLASSES    40
#define MEDIUM_LAST_CLASS     (MEDIUM_NUM_CLASSES - 1)

/* Chunk layout (MEDIUM_CHUNK_SIZE):
 *
 *   [ owner ]  [ block ][ block ] ...
 *
 * Blocks start at 8 mod 16. Free blocks are coalesced with their neighbors and
 * kept in size-class bins (bitmap marks non-empty bins).
 *
 * Allocated: [ hdr 8 ][ payload ... ]
 * Free:      [ hdr 8 ][ prev 8 ][ next 8 ] ... [ ftr 8 ]
 *
 * hdr = size | ALLOCATED | PREV_FREE. The ftr exists only on free blocks, so the
 * next block's PREV_FREE flag says whether the previous block can be found.
 */

typedef bin_t medium_bin_t;

typedef struct {
  medium_bin_t bins[MEDIUM_NUM_CLASSES];
  uint64_t     bitmap;
  void*        owner;
} medium_heap_t;

typedef enum {
  SPLIT_SUCCESS,
  SPLIT_DONTNEED,
  SPLIT_FAILED,
} split_res_t;

// Bin
block_t*    medium_pop_fit          (medium_heap_t* heap, int class_idx);
block_t*    medium_coalesce         (medium_heap_t* heap, block_t* b);
void        medium_init_heap        (medium_heap_t* heap, void* owner);
split_res_t medium_split            (medium_heap_t* heap, block_t* b, size_t need);
bool        medium_add_free_block   (medium_heap_t* heap, block_t* b, int class_idx);
bool        medium_remove_free_block(medium_heap_t* heap, block_t* b, int class_idx);
int         get_medium_class        (size_t         n);
size_t      get_medium_class_size   (int            class_idx);

// Allocation
void*    medium_malloc             (medium_heap_t* heap, size_t block_size_req);
bool     medium_free               (medium_heap_t* heap, void*  ptr);
bool     medium_try_resize_in_place(medium_heap_t* heap, void*  ptr , size_t new_block_size);

#endif // MEDIUM_H
