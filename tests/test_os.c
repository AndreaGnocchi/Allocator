#define _GNU_SOURCE

#include "test_util.h"
#include "test_main.h"
#include "../src/os.h"
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

static inline bool is_unmapped(void* addr) {
  unsigned char vec[16];
  return mincore(addr, 4096, vec) == -1 && errno == ENOMEM;
}

static inline void test_os_page_size(void) {
  os_init();
  size_t ps = os_get_page_size();
  CHECK(ps == (size_t)sysconf(_SC_PAGESIZE));
  CHECK((ps & (ps - 1)) == 0);
  CHECK(ps >= 4096);
 
  os_init();
  CHECK(os_get_page_size() == ps);
}

static inline void test_os_align_to_page(void) {
  os_init();
  size_t ps = os_get_page_size();
 
  CHECK(os_align_to_page(0)           == 0);
  CHECK(os_align_to_page(1)           == ps);
  CHECK(os_align_to_page(ps - 1)      == ps);
  CHECK(os_align_to_page(ps)          ==  ps);
  CHECK(os_align_to_page(ps + 1)      == 2 * ps);
  CHECK(os_align_to_page(10 * ps)     == 10 * ps);
  CHECK(os_align_to_page(10 * ps + 1) == 11 * ps);
 
  size_t max_ok = SIZE_MAX - (ps - 1);
  CHECK(os_align_to_page(max_ok)     == max_ok);
  CHECK(os_align_to_page(max_ok + 1) ==  0);
  CHECK(os_align_to_page(SIZE_MAX)   ==  0);
}

static inline void test_os_map_pages_basic(void) {
  os_init();
  size_t ps = os_get_page_size();
 
  char* p = os_map_pages(ps);
  CHECK(p != NULL);
  CHECK((uintptr_t)p % ps == 0);
  CHECK(is_zero(p, ps));
  memset(p, 0xAB, ps);
  CHECK(os_unmap_pages(p, ps));
  CHECK(is_unmapped(p));
}

static inline void test_os_map_pages_large_zeroed_and_writable(void) {
  os_init();
  size_t n = (size_t)4 << 20;
  char*  p = os_map_pages(n);
  CHECK(p != NULL);
  if (!p) return;
  
  CHECK(is_zero(p, n));
  memset(p, 0x5A, n);
  CHECK(p[0] == 0x5A && p[n - 1] == 0x5A);
  CHECK(os_unmap_pages(p, n));
}

static inline void test_os_map_pages_distinct(void) {
  os_init();
  size_t n = 64 << 10;
  char*  a = os_map_pages(n);
  char*  b = os_map_pages(n);
  CHECK(a && b);
  CHECK(a != b);
  CHECK((a + n <= b) || (b + n <= a));
  memset(a, 1, n);
  memset(b, 2, n);
  CHECK(a[n - 1] == 1 && b[0] == 2);
  os_unmap_pages(a, n);
  os_unmap_pages(b, n);
}

static inline void test_os_map_pages_invalid(void) {
  os_init();
  CHECK(os_map_pages(0) == NULL);
  CHECK(os_map_pages(SIZE_MAX) == NULL);
  CHECK(os_map_pages(SIZE_MAX / 2) == NULL);
}

static inline void test_os_unmap_invalid(void) {
  os_init();
  size_t ps = os_get_page_size();
  CHECK(!os_unmap_pages(NULL, ps));
 
  char* p = os_map_pages(2 * ps);
  CHECK(p != NULL);
  CHECK(!os_unmap_pages(p, 0));
  CHECK(!os_unmap_pages(p + 1, ps));
  CHECK(os_unmap_pages(p, 2 * ps));
}

static inline void test_os_map_aligned_alignments(void) {
  os_init();
  size_t ps = os_get_page_size();
  size_t aligns[] = {ps, 2 * ps, (size_t)64 << 10, (size_t)256 << 10, (size_t)1 << 20,
                     (size_t)2 << 20, (size_t)4 << 20};
 
  for (size_t i = 0; i < sizeof(aligns) / sizeof(aligns[0]); i++) {
    size_t al = aligns[i];
    for (int rep = 0; rep < 8; rep++) {
      char* p = os_map_aligned(al, al);
      CHECK(p != NULL);
      if (!p) continue;
      CHECK((uintptr_t)p % al == 0);
      CHECK(p[0] == 0 && p[al - 1] == 0);
      p[0] = 1;
      p[al - 1] = 2;
      CHECK(os_unmap_pages(p, al));
    }
  }
}

static inline void test_os_map_aligned_size_larger_than_align(void) {
  os_init();
  size_t al = (size_t)64 << 10;
  size_t sz = al * 5;
  char*  p  = os_map_aligned(sz, al);
  CHECK(p != NULL);
  if (!p) return;
  CHECK((uintptr_t)p % al == 0);
  memset(p, 0x77, sz);
  CHECK(os_unmap_pages(p, sz));
}

static inline void test_os_map_aligned_tail_is_trimmed(void) {
  os_init();
  size_t ps = os_get_page_size();
  size_t al = (size_t)1 << 20;
  char*  p  = os_map_aligned(al, al);
  CHECK(p != NULL);
  if (!p) return;
  CHECK(is_unmapped(p + al));
  (void)ps;
  os_unmap_pages(p, al);
}

static inline void test_os_map_aligned_does_not_leak_address_space(void) {
  os_init();
  size_t al = (size_t)1 << 20;
  enum { N = 64 };
  char* ptrs[N];
 
  size_t before = vm_size_bytes();
  for (int i = 0; i < N; i++) {
    ptrs[i] = os_map_aligned(al, al);
    CHECK(ptrs[i] != NULL);
  }
  size_t grown = vm_size_bytes() - before;
 
  CHECK(grown <= (size_t)N * al + (2 << 20));
 
  for (int i = 0; i < N; i++)
    if (ptrs[i]) os_unmap_pages(ptrs[i], al);
}

static inline void test_os_map_aligned_invalid(void) {
  os_init();
  size_t ps = os_get_page_size();
  CHECK(os_map_aligned(0, 1 << 20)               == NULL);
  CHECK(os_map_aligned(1 << 20, 0)               == NULL);
  CHECK(os_map_aligned(1 << 20, ps / 2)          == NULL);
  CHECK(os_map_aligned(SIZE_MAX, 1 << 20)        == NULL);
  CHECK(os_map_aligned(SIZE_MAX - ps, ps * 2)    == NULL);
  CHECK(os_map_aligned((size_t)1 << 62, 1 << 20) == NULL);
}

void run_all_os_test(void) {
  test_os_page_size();
  test_os_align_to_page();
  test_os_map_pages_basic();
  test_os_map_pages_large_zeroed_and_writable();
  test_os_map_pages_distinct();
  test_os_map_pages_invalid();
  test_os_unmap_invalid();
  test_os_map_aligned_alignments();
  test_os_map_aligned_size_larger_than_align();
  test_os_map_aligned_tail_is_trimmed();
  test_os_map_aligned_does_not_leak_address_space();
  test_os_map_aligned_invalid();
}
