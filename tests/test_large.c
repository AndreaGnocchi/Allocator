#include "test_main.h"
#include "test_util.h"
#include "../src/os.h"
#include "../src/large.h"
#include <string.h>
#include <stdalign.h>

static inline size_t page(void) {
  os_init();
  return os_get_page_size();
}

static inline void test_large_rejects_sizes_below_minimum(void) {
  page();
  CHECK(large_malloc(0)                   == NULL);
  CHECK(large_malloc(1)                   == NULL);
  CHECK(large_malloc(4096)                == NULL);
  CHECK(large_malloc((size_t)(128 << 10)) == NULL);
  CHECK(large_malloc(LARGE_MIN_SIZE - 1)  == NULL);
}

static void test_large_minimum_size_layout(void) {
  size_t ps = page();
  void*  p  = large_malloc(LARGE_MIN_SIZE);
  CHECK(p != NULL);
  if (!p) return;
 
  CHECK( (uintptr_t)p % ALIGNMENT            == 0);
  CHECK(((uintptr_t)p - LARGE_OVERHEAD) % ps == 0);
 
  block_t* b = ptr_to_block(p);
  CHECK(block_is_allocated(b));
  CHECK(block_size(b) % ps == 0);
  CHECK(block_size(b)      == os_align_to_page(LARGE_MIN_SIZE + LARGE_OVERHEAD));
  CHECK(large_usable_size(p) >= LARGE_MIN_SIZE);
  CHECK(large_usable_size(p) == block_size(b) - LARGE_OVERHEAD);
 
  CHECK(is_zero(p, LARGE_MIN_SIZE));
  memset(p, 0xCD, large_usable_size(p));
  CHECK(large_free(p));
  CHECK(is_unmapped((char*)p - LARGE_OVERHEAD));
}

static inline void test_large_page_rounding_boundaries(void) {
  size_t ps = page();

  for (size_t k = 33; k < 40; k++) {
    size_t exact = k * ps - LARGE_OVERHEAD;
    for (int d = -1; d <= 2; d++) {
      size_t n = exact + (size_t)(d + 0);
      if (n < LARGE_MIN_SIZE) continue;
      char* p = large_malloc(n);
      CHECK(p != NULL);
      if (!p) continue;
      size_t expect_mapped = os_align_to_page(n + LARGE_OVERHEAD);
      CHECK(block_size(ptr_to_block(p)) == expect_mapped);
      CHECK(large_usable_size(p) >= n);
      p[0] = 1;
      p[n - 1] = 2;
      CHECK(large_free(p));
    }
  }
}

static inline void test_large_many_sizes_writable(void) {
  page();
  size_t sizes[] = {LARGE_MIN_SIZE, LARGE_MIN_SIZE + 1, 200000, 262144, 1000000, (size_t)1 << 20,
                    ((size_t)1 << 20) + 1, 5000000, (size_t)16 << 20, (size_t)64 << 20};
  
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]) ; i++) {
    char* p = large_malloc(sizes[i]);
    CHECK(p != NULL);
    if (!p) continue;
    CHECK((uintptr_t)p % 16 == 0);
    fill_pattern(p, sizes[i], (uint32_t)i + 1);
    CHECK(check_pattern(p, sizes[i], (uint32_t)i + 1));
    CHECK(large_free(p));
  }
}

static inline void test_large_allocations_are_disjoint(void) {
  page();
  enum { N = 12 };
  char* p[N];
  for (int i = 0; i < N; i++) {
    p[i] = large_malloc(300000 + (size_t)i * 12345);
    CHECK(p[i] != NULL);
    if (p[i])
      fill_pattern(p[i], 300000, (uint32_t)i + 1);
  }
  for (int i = 0; i < N; i++)
    if (p[i])
      CHECK(check_pattern(p[i], 300000, (uint32_t)i + 1));
  for (int i = 0; i < N; i++)
    if (p[i])
      CHECK(large_free(p[i]));
}
static inline void test_large_free_unmaps_everything(void) {
  page();
  char* p = large_malloc(1 << 20);
  CHECK(p != NULL);
  if (!p) return;
  char* base = p - LARGE_OVERHEAD;
  size_t len = block_size(ptr_to_block(p));
  CHECK(large_free(p));
  for (size_t off = 0; off < len; off += 4096 * 37)
    CHECK(is_unmapped(base + off));
  CHECK(is_unmapped(base + len - 4096));
}

static void test_large_alloc_free_loop_does_not_leak_address_space(void) {
  page();
  void* warm = large_malloc(1 << 20);
  large_free(warm);
  size_t before = vm_size_bytes();
  for (int i = 0; i < 2000; i++) {
    void* p = large_malloc((size_t)1 << 20);
    CHECK(p != NULL);
    if (!p) return;
    ((char*)p)[0] = 1;
    large_free(p);
  }
  size_t after = vm_size_bytes();
  CHECK(after <= before + (1 << 20));
}

static inline void test_large_free_rejects_invalid_pointers(void) {
  page();
  CHECK(!large_free(NULL));
 
  alignas(4096) static char fake[8192];
  block_t* b = (block_t*)(fake + 16 - HDR_SIZE);
  block_set_hdr(b, 4096, false, false);
  CHECK(!large_free(fake + 16));
  CHECK(!large_free(fake + 17));
  CHECK(!large_free(fake + 24));
}
 
static inline void test_large_overflow_and_oom(void) {
  page();
  CHECK(large_malloc(SIZE_MAX) == NULL);
  CHECK(large_malloc(SIZE_MAX - LARGE_OVERHEAD + 1) == NULL);
  CHECK(large_malloc(SIZE_MAX - 4096) == NULL);
  CHECK(large_malloc(SIZE_MAX / 2) == NULL);
  CHECK(large_malloc((size_t)1 << 62) == NULL);
}
 
static inline void test_large_out_of_memory_with_limit(void) {
  SKIP_IF_NO_ADDRESS_LIMIT();
  page();
  void* warm = large_malloc(LARGE_MIN_SIZE);
  large_free(warm);
 
  limit_address_space(8 << 20);
  void* a = large_malloc(4 << 20);
  CHECK(a != NULL);
  CHECK(large_malloc(16 << 20) == NULL);
  CHECK(a && large_free(a));
  CHECK(large_malloc(4 << 20) != NULL);
  unlimit_address_space();
}

void run_all_large_test(void) {
  test_large_rejects_sizes_below_minimum();
  test_large_minimum_size_layout();
  test_large_page_rounding_boundaries();
  test_large_many_sizes_writable();
  test_large_allocations_are_disjoint();
  test_large_free_unmaps_everything();
  test_large_alloc_free_loop_does_not_leak_address_space();
  test_large_free_rejects_invalid_pointers();
  test_large_overflow_and_oom();
  test_large_out_of_memory_with_limit();
}
