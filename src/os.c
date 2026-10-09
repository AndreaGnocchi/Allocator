#define _DEFAULT_SOURCE

#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include "os.h"

static size_t page_size = 4096;

void os_init(void) {
  long ps = sysconf(_SC_PAGESIZE);
  if (ps > 0) {
    page_size = (size_t)ps;
  }
}

size_t os_get_page_size(void) {
  return page_size;
}

size_t os_align_to_page(size_t n) {
  size_t mask = page_size - 1;
  if (n > SIZE_MAX - mask) return 0;
  return (n + mask) & ~mask;
}

void* os_map_pages(size_t size) {
  if (size == 0) return NULL;
  
  size_t actual_size = os_align_to_page(size);
  if (actual_size == 0) return NULL;

  void* ptr = mmap(NULL, actual_size, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (ptr == MAP_FAILED) return NULL;

  return ptr;
}

bool os_unmap_pages(void* addr, size_t size) {
  if (!addr || size == 0) return false;

  size_t actual_size = os_align_to_page(size);
  if (actual_size == 0) return false;

  return munmap(addr, actual_size) == 0;
}

bool os_madvise_dontneed(void* addr, size_t size) {
  if (!addr || size == 0) return false;

  size_t actual_size = os_align_to_page(size);
  if (actual_size == 0) return false;

  return madvise(addr, actual_size, MADV_DONTNEED) == 0;
}
