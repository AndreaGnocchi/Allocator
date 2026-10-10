#include "large.h"
#include "os.h"
#include "block.h"
#include <stdbool.h>
#include <string.h>

void large_init_heap(large_heap_t* heap) {
  if (!heap) return;

  memset(heap, 0, sizeof(*heap));
}

void* large_malloc(large_heap_t* heap, size_t size) {
  if (!heap || size < LARGE_MIN_SIZE) return NULL;

  size_t tot_size = size + LARGE_OVERHEAD;
  if (tot_size < size) return NULL;

  size_t mapped_size = os_align_to_page(tot_size);
  if (mapped_size == 0) return NULL;

  char* base = os_map_pages(mapped_size);
  if (!base) return NULL;

  block_t* node = (block_t*)base;
  block_set_hdr(node, mapped_size, true, false);
  node->prev = NULL;
  node->next = heap->head;
  
  block_t* b = (block_t*)(base + sizeof(block_t));
  block_set_hdr(b, mapped_size, true, false);

  if (heap->head) {
    heap->head->prev = node;
  }
  heap->head = node;

  return block_to_ptr(b);
}

static inline block_t* ptr_to_node(void* ptr) {
  return (block_t*)((char*)ptr - LARGE_OVERHEAD);
}


bool large_free(large_heap_t* heap, void* ptr) {
  if (!heap || !ptr) return false;
  if ((uintptr_t)ptr & (ALIGNMENT - 1)) return false;

  block_t* b    = ptr_to_block(ptr);
  block_t* node = ptr_to_node(ptr);

  if (!block_is_allocated(b)) return false;

  block_t* prev = node->prev;
  block_t* next = node->next;

  if (!os_unmap_pages(node, block_size(node))) return false;
  
  if (prev) {
    prev->next = next;
  }
  if (next) {
    next->prev = prev;
  }

  if (heap->head == node) {
    heap->head = next;
  }

  return true;
}

size_t large_usable_size(const void* ptr) {
  if (!ptr) return 0;
 
  const block_t* node = (const block_t*)((const char*)ptr - LARGE_OVERHEAD);
  return block_size(node) - LARGE_OVERHEAD;
}

void large_teardown(large_heap_t* heap) {
  if (!heap) return;

  block_t* curr = heap->head;
  while (curr) {
    block_t* next = curr->next;
    os_unmap_pages(curr, block_size(curr));
    curr = next;
  }

  large_init_heap(heap);
}
