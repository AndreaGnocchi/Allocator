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

  void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (ptr == MAP_FAILED) return NULL;

  return ptr;
}

void* os_map_aligned(size_t size, size_t align) {
  if (size == 0 || align < page_size || size > SIZE_MAX - align) return NULL;

  size_t span = size + align;
  char* raw = os_map_pages(span);
  if (!raw) return NULL;

  char*  base = (char*)(((uintptr_t)raw + align - 1) & ~(uintptr_t)(align - 1));
  size_t head = (size_t)(base - raw);
  size_t tail = align - head;

  if (head)
    os_unmap_pages(raw, head);
  if (tail)
    os_unmap_pages(base + size, tail);
  
  return base;
}

bool os_unmap_pages(void* addr, size_t size) {
  if (!addr || size == 0) return false;

  return munmap(addr, size) == 0;
}
