#include "large.h"
#include "os.h"
#include "block.h"
#include <stdbool.h>

void* large_malloc(size_t size) {
  if (size < LARGE_MIN_SIZE) return NULL;

  size_t tot_size = size + LARGE_OVERHEAD;
  if (tot_size < size) return NULL;

  size_t mapped_size = os_align_to_page(tot_size);

  char* base = os_map_pages(mapped_size);
  if (!base) return NULL;

  block_t* b = (block_t*)(base + LARGE_OVERHEAD - HDR_SIZE);
  block_set_hdr(b, mapped_size, true, false);

  return block_to_ptr(b);   // base + 16
}

bool large_free(void* ptr) {
  if (!ptr || ((uintptr_t)ptr & (ALIGNMENT - 1))) return false;

  block_t* b = ptr_to_block(ptr);
  if (!block_is_allocated(b)) return false;

  return os_unmap_pages((char*)ptr - LARGE_OVERHEAD, block_size(b));
}

size_t large_usable_size(const void* ptr) {
  const block_t* b = (const block_t*)((const char*)ptr - HDR_SIZE);
  return block_size(b) - LARGE_OVERHEAD;
}
