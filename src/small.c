#include "small.h"
#include "block.h"
#include "os.h"
#include <stdbool.h>
#include <string.h>

static const uint16_t small_class_size[SMALL_NUM_CLASSES] = {
    // 16-byte steps up to 256 (15 classes, 0..14)
    32, 48, 64, 80, 96, 112, 128,
    144, 160, 176, 192, 208, 224, 240, 256,
    // 32-byte steps up to 512 (8 classes, 15..22)
    288, 320, 352, 384, 416, 448, 480, 512,
    // 64-byte steps up to 1024 (8 classes, 23..30)
    576, 640, 704, 768, 832, 896, 960, 1024,
    // 128-byte steps up to 2048 (8 classes, 31..38)
    1152, 1280, 1408, 1536, 1664, 1792, 1920, 2048,
    // 256-byte steps up to 4096 (8 classes, 39..46)
    2304, 2560, 2816, 3072, 3328, 3584, 3840, 4096
};

static const uint8_t small_class_of[257] = {
  0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19,
  20, 20, 21, 21, 22, 22, 23, 23, 23, 23, 24, 24, 24, 24, 25, 25, 25, 25, 26, 26, 26, 26, 27, 27,
  27, 27, 28, 28, 28, 28, 29, 29, 29, 29, 30, 30, 30, 30, 31, 31, 31, 31, 31, 31, 31, 31, 32, 32,
  32, 32, 32, 32, 32, 32, 33, 33, 33, 33, 33, 33, 33, 33, 34, 34, 34, 34, 34, 34, 34, 34, 35, 35,
  35, 35, 35, 35, 35, 35, 36, 36, 36, 36, 36, 36, 36, 36, 37, 37, 37, 37, 37, 37, 37, 37, 38, 38,
  38, 38, 38, 38, 38, 38, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 40, 40,
  40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41,
  41, 41, 41, 41, 41, 41, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 43, 43,
  43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44,
  44, 44, 44, 44, 44, 44, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 46, 46,
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46
};

int get_small_class(size_t n) {
  if (n == 0 || n > SMALL_MAX_SIZE) return -1;

  size_t idx = (n + 15) >> 4;
  if (idx > 256) idx = 256;

  return small_class_of[idx];
}

size_t get_small_class_size(int class_idx) {
  if (class_idx < 0 || class_idx >= SMALL_NUM_CLASSES) return 0;

  return small_class_size[class_idx];
}

void small_init_heap(small_heap_t* heap) {
  if (!heap) return;
  
  memset(heap, 0, sizeof(*heap));
}

bool small_add_free_block(small_heap_t* heap, block_t* b, int class_idx) {
  if (class_idx < 0 || class_idx >= SMALL_NUM_CLASSES || !b || !heap) return false;

  block_t* head = heap->bins[class_idx].head;
 
  if (head && head->prev != NULL) return false;
 
  b->prev = NULL;
  b->next = head;
 
  if (head) {
    head->prev = b;
  }
  heap->bins[class_idx].head = b;
 
  return true;
}


bool small_remove_free_block(small_heap_t* heap, block_t* b, int class_idx) {
  if (class_idx < 0 || class_idx >= SMALL_NUM_CLASSES || !b || !heap) return false;
 
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
 
  b->next = NULL;
  b->prev = NULL;
 
  return true;
}


block_t* small_pop_free(small_heap_t* heap, int class_idx) {
  if (!heap || class_idx < 0 || class_idx >= SMALL_NUM_CLASSES) return NULL;
  
  block_t* b = heap->bins[class_idx].head;
  if (!b || block_is_allocated(b)) return NULL;
  if (((uintptr_t)b % ALIGNMENT) != (HDR_SIZE % ALIGNMENT) ||
      block_size(b) != get_small_class_size(class_idx)) return NULL;
  if (!small_remove_free_block(heap, b, class_idx)) return NULL;
  return b;
}

static inline bool _small_carve_tail(small_heap_t* heap) {
  while (heap->cur && heap->left >= small_class_size[0]) {
    size_t avail = heap->left;

    int cls = (avail >= SMALL_MAX_SIZE) ? (SMALL_NUM_CLASSES - 1) : get_small_class(avail);
    if (small_class_size[cls] > avail) cls--;

    size_t sz  = small_class_size[cls];
    block_t* b = (block_t*)heap->cur;

    block_set_hdr(b, sz, false, false);
    if (!small_add_free_block(heap, b, cls)) return false;
 
    heap->cur  += sz;
    heap->left -= sz;
  }

  return true;
}

static inline bool _small_refill(small_heap_t* heap) {
  char* base = os_map_pages(SMALL_CHUNK_SIZE);
  if (!base) return false;
 
  if(!_small_carve_tail(heap)) {
    os_unmap_pages(base, SMALL_CHUNK_SIZE);
    return false;
  }
 
  heap->cur  = base + HDR_SIZE; // 8 mod 16
  heap->left = SMALL_CHUNK_SIZE - HDR_SIZE;
 
  return true;
}

void* small_malloc(small_heap_t* heap, size_t block_size) {
  if (!heap || block_size < MIN_BLOCK_SIZE || block_size > SMALL_MAX_SIZE) return NULL;
 
  int cls = get_small_class(block_size);
  if (cls < 0) return NULL;
  size_t sz = get_small_class_size(cls);
  if (sz == 0) return NULL;
 
  block_t* b = small_pop_free(heap, cls);
  if (!b) {
    if (heap->left < sz && !_small_refill(heap)) return NULL;
 
    if (!heap->cur || heap->left < sz) return NULL;
 
    b = (block_t*)heap->cur;
    heap->cur  += sz;
    heap->left -= sz;
  }
 
  block_set_hdr(b, sz, true, false);
  return block_to_ptr(b);
}


bool small_free(small_heap_t* heap, void* ptr) {
  if (!heap || !ptr) return false;
  if (((uintptr_t)ptr & (ALIGNMENT - 1)) != 0) return false;

  block_t* b = ptr_to_block(ptr);
  if (!block_is_allocated(b)) return false;

  size_t sz = block_size(b);
  if (sz < MIN_BLOCK_SIZE || sz > SMALL_MAX_SIZE) return false;

  int cls = get_small_class(sz);
  if (cls < 0 || get_small_class_size(cls) != sz) return false;

  bool result = small_add_free_block(heap, b, cls);

  if (result)
    block_set_hdr(b, sz, false, false);

  return result;
}

bool small_try_resize_in_place(void* ptr, size_t new_block_size) {
  if (!ptr || new_block_size < MIN_BLOCK_SIZE || new_block_size > SMALL_MAX_SIZE) return false;
  if (((uintptr_t)ptr & (ALIGNMENT - 1)) != 0) return false;

  block_t* b = ptr_to_block(ptr);
  if (!b || !block_is_allocated(b)) return false;

  size_t sz = block_size(b);
  if (sz < MIN_BLOCK_SIZE || sz > SMALL_MAX_SIZE) return false;

  return get_small_class(new_block_size) == get_small_class(sz);
}
