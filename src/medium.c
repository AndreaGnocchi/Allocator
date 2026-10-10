#include "medium.h"
#include "block.h"
#include "os.h"
#include <string.h>

static const uint32_t medium_class_size[MEDIUM_NUM_CLASSES] = {
  // 4KB to 8KB (step 512)
  4608, 5120, 5632, 6144, 6656, 7168, 7680, 8192,
  // 8KB to 16KB (step 1024)
  9216, 10240, 11264, 12288, 13312, 14336, 15360, 16384,
  // 16KB to 32KB (step 2048)
  18432, 20480, 22528, 24576, 26624, 28672, 30720, 32768,
  // 32KB to 64KB (step 4096)
  36864, 40960, 45056, 49152, 53248, 57344, 61440, 65536,
  // 64KB to 128KB (step 8192)
  73728, 81920, 90112, 98304, 106496, 114688, 122880, 131072
};

static const uint8_t medium_class_of[248] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 16,
  16, 17, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22,
  22, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 25, 26, 26, 26,
  26, 26, 26, 26, 26, 27, 27, 27, 27, 27, 27, 27, 27, 28, 28, 28, 28, 28, 28, 28, 28, 29, 29, 29,
  29, 29, 29, 29, 29, 30, 30, 30, 30, 30, 30, 30, 30, 31, 31, 31, 31, 31, 31, 31, 31, 32, 32, 32,
  32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33,
  33, 33, 33, 33, 33, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 35, 35, 35,
  35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
  36, 36, 36, 36, 36, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 38, 38, 38,
  38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39,
  39, 39, 39, 39, 39
};

#define MEDIUM_MIN_BLOCK ((size_t)medium_class_size[0])

int get_medium_class(size_t n) {
   if (n <= MEDIUM_MIN_SIZE || n > MEDIUM_MAX_SIZE) return -1;

   size_t idx = ((n + 511) >> 9) - 9;
   if (idx > 247) idx = 247;

   return medium_class_of[idx];
}

size_t get_medium_class_size(int class_idx) {
  if (class_idx < 0 || class_idx >= MEDIUM_NUM_CLASSES) return 0;

  return medium_class_size[class_idx];
}

void medium_init_heap(medium_heap_t* heap) {
  if (!heap) return;

  memset(heap, 0, sizeof(*heap));
}

static inline int medium_floor_class(size_t sz) {
  if (sz >= MEDIUM_MAX_SIZE) return MEDIUM_LAST_CLASS; // catch-all bin
  int c = medium_class_of[((sz + 511) >> 9) - 9];
  if (medium_class_size[c] != sz) c--;
  return c;
}

bool medium_add_free_block(medium_heap_t* heap, block_t* b, int class_idx) {
  if (class_idx < 0 || class_idx >= MEDIUM_NUM_CLASSES || !b || !heap) return false;

  block_t* head = heap->bins[class_idx].head;

  if (head && head->prev != NULL) return false;

  b->prev = NULL;
  b->next = head;

  if (head) {
    head->prev = b;
  }
  heap->bins[class_idx].head = b;
  heap->bitmap              |= (uint64_t)1 << class_idx;
  
  return true;
}

bool medium_remove_free_block(medium_heap_t* heap, block_t* b, int class_idx) {
  if (class_idx < 0 || class_idx >= MEDIUM_NUM_CLASSES || !b || !heap) return false;

  block_t* prev = b->prev;
  block_t* next = b->next;
 
  if (prev) {
    if (prev->next != b) return false;
  } else {
    if (heap->bins[class_idx].head != b) return false;
  }
  if (next && next->prev != b) return false;
 
  if (prev) {
    prev->next = next;
  } else {
    heap->bins[class_idx].head = next;
  }
 
  if (next) {
    next->prev = prev;
  }

  if (!heap->bins[class_idx].head) {
    heap->bitmap &= ~((uint64_t)1 << class_idx);
  }
  
  b->next = NULL;
  b->prev = NULL;
  
  return true;
}

block_t* medium_pop_fit(medium_heap_t* heap, int class_idx) {
  if (!heap || class_idx < 0 || class_idx >= MEDIUM_NUM_CLASSES) return NULL;

  uint64_t candidates = heap->bitmap & (~(uint64_t)0 << class_idx);
  if (candidates == 0) return NULL;

  int i = __builtin_ctzll(candidates);

  block_t* b = heap->bins[i].head;
  if (i == MEDIUM_LAST_CLASS && class_idx == MEDIUM_LAST_CLASS) {
    while (b && !(block_size(b) == MEDIUM_MAX_SIZE ||
                  block_size(b) >= MEDIUM_MAX_SIZE + MEDIUM_MIN_BLOCK))
      b = b->next; // Traverse list to find a block large enough to split or exact match
  }
  
  if (!b || block_is_allocated(b)) return NULL;
  if (((uintptr_t)b % ALIGNMENT) != (HDR_SIZE % ALIGNMENT)) return NULL;
  if (block_size(b) < medium_class_size[i]) return NULL;
  if (!medium_remove_free_block(heap, b, i)) return NULL;

  return b;
}

block_t* medium_coalesce(medium_heap_t* heap, block_t* b) {
  if (!heap || !b || block_is_allocated(b)) return NULL;

  size_t sz = block_size(b);
  if (sz == 0) return NULL;

  block_t* next = block_next(b);
  if (!block_is_allocated(next)) {
    size_t next_sz = block_size(next);
    int c          = medium_floor_class(next_sz);
    if (!medium_remove_free_block(heap, next, c)) return NULL;
    sz += next_sz;
  }

  if (block_prev_is_free(b)) {
    block_t* prev    = block_prev(b);
    size_t   prev_sz = block_size(prev);
    int      c       = medium_floor_class(prev_sz);
    if (!medium_remove_free_block(heap, prev, c)) return NULL;
    sz += prev_sz;
    b = prev;
  }

  block_set_hdr(b, sz, false, false);
  block_write_ftr(b);
  block_set_prev_free(block_next(b), true);
  
  return b;
}

split_res_t medium_split(medium_heap_t* heap, block_t* b, size_t need) {
  if (!heap || !b) return SPLIT_FAILED;

  size_t sz = block_size(b);
 
  if (need < MEDIUM_MIN_BLOCK || need > sz) return SPLIT_FAILED;
 
  size_t rem = sz - need;
  if (rem < MEDIUM_MIN_BLOCK) return SPLIT_DONTNEED;
 
  b->hdr = (need & BLOCK_SIZE_MASK) | (b->hdr & BLOCK_FLAGS_MASK); // Shirking while keeping flags
 
  block_t* r = (block_t*)((char*)b + need);
  block_set_hdr(r, rem, false, false);
  block_write_ftr(r);
  block_set_prev_free(block_next(r), true);
 
  r = medium_coalesce(heap, r);
  if (!r) return SPLIT_FAILED;
 
  int c = medium_floor_class(block_size(r));
  return medium_add_free_block(heap, r, c) ? SPLIT_SUCCESS : SPLIT_FAILED;
}

static inline bool _medium_refill(medium_heap_t* heap) {
  char* base = os_map_pages(MEDIUM_CHUNK_SIZE);
  if (!base) return false;

  size_t sz = MEDIUM_CHUNK_SIZE - MEDIUM_CHUNK_HDR - MEDIUM_CHUNK_SENTINEL;

  block_t* b = (block_t*)(base + MEDIUM_CHUNK_HDR); // 8 mod 16
  block_set_hdr(b, sz, false, false);
  block_write_ftr(b);

  block_t* sentinel = block_next(b);
  block_set_hdr(sentinel, 0, true, true);

  if (!medium_add_free_block(heap, b, medium_floor_class(sz))) {
    os_unmap_pages(base, MEDIUM_CHUNK_SIZE);
    return false;
  }

  *(void**)base = heap->chunks;
  heap->chunks  = base;
  
  return true;
}

void* medium_malloc(medium_heap_t* heap, size_t block_size_req) {
  if (!heap || block_size_req <= MEDIUM_MIN_SIZE || block_size_req > MEDIUM_MAX_SIZE) return NULL;
  
  int cls = get_medium_class(block_size_req);
  if (cls < 0) return NULL;
  size_t sz = medium_class_size[cls];
  if (sz == 0) return NULL;
 
  block_t* b = medium_pop_fit(heap, cls);
  if (!b) {
    if (!_medium_refill(heap)) return NULL;
    b = medium_pop_fit(heap, cls);
    if (!b) return NULL;
  }

  switch (medium_split(heap, b, sz)) {
  case SPLIT_SUCCESS: break;
    
  case SPLIT_DONTNEED:
    block_set_prev_free(block_next(b), false);   
    break;
    
  case SPLIT_FAILED: default: return NULL;
  }


  block_set_hdr(b, block_size(b), true, block_prev_is_free(b));
  return block_to_ptr(b);
}

bool medium_free(medium_heap_t* heap, void* ptr) {
  if (!heap || !ptr) return false;
  if (((uintptr_t)ptr & (ALIGNMENT - 1)) != 0) return false;

  block_t* b = ptr_to_block(ptr);
  if (!block_is_allocated(b)) return false;

  size_t sz = block_size(b);
  if (sz < MEDIUM_MIN_BLOCK || sz > MEDIUM_MAX_SIZE) return false;

  block_set_hdr(b, sz, false, block_prev_is_free(b));

  b = medium_coalesce(heap, b);
  if (!b) return false;

  return medium_add_free_block(heap, b, medium_floor_class(block_size(b)));
}

bool medium_try_resize_in_place(medium_heap_t* heap, void* ptr, size_t new_block_size) {
  if (!heap || !ptr || new_block_size <= MEDIUM_MIN_SIZE
      || new_block_size > MEDIUM_MAX_SIZE) return false;
  if (((uintptr_t)ptr & (ALIGNMENT - 1)) != 0) return false;
  
  int cls = get_medium_class(new_block_size);
  if (cls < 0) return false;
  size_t want = get_medium_class_size(cls);
  if (want == 0) return false;

  block_t* b = ptr_to_block(ptr);
  if (!block_is_allocated(b)) return false;

  size_t cur = block_size(b);
  if (cur <= MEDIUM_MIN_SIZE || cur > MEDIUM_MAX_SIZE) return false;

  if (want <= cur) {
    return medium_split(heap, b, want) != SPLIT_FAILED;
  }

  block_t* next = block_next(b);
  if (block_is_allocated(next)) return false;

  size_t size_next = block_size(next);
  size_t total     = cur + size_next;
  if (total < want || (total > MEDIUM_MAX_SIZE && total - want < MEDIUM_MIN_BLOCK)) return false;

  if (!medium_remove_free_block(heap, next, medium_floor_class(size_next))) return false;
  
  b->hdr = (total & BLOCK_SIZE_MASK) | (b->hdr & BLOCK_FLAGS_MASK);

  switch (medium_split(heap, b, want)) {
  case SPLIT_SUCCESS: return true;
    
  case SPLIT_DONTNEED:
    block_set_prev_free(block_next(b), false);
    return true;
    
  case SPLIT_FAILED: default: return false;
  }
}

void medium_teardown(medium_heap_t* heap) {
  if (!heap) return;
 
  void* chunk = heap->chunks;
  while (chunk) {
    void* next = *(void**)chunk;
    os_unmap_pages(chunk, MEDIUM_CHUNK_SIZE);
    chunk = next;
  }
 
  medium_init_heap(heap);
}
