#include "../include/allocator.h"
#include "../src/small.h"
#include "../src/thread_heap.h"
#include "test_main.h"
#include "test_util.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static inline void test_calloc_zero_total_returns_null(void) {
  CHECK(my_calloc(0, 0)        == NULL);
  CHECK(my_calloc(0, 16)       == NULL);
  CHECK(my_calloc(16, 0)       == NULL);
  CHECK(my_calloc(0, SIZE_MAX) == NULL);
  CHECK(my_calloc(SIZE_MAX, 0) == NULL);
}

static inline void test_calloc_zeroes_memory_for_every_size(void) {
  static const size_t sizes[] = {1, 7, 8, 24, 25, 100, 255, 256, 1000, 4000, 4088, 4089, 5000, 20000,
                                 131064, 131065, 131073, 200000, 1 << 20, 5 << 20};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    unsigned char* p = my_calloc(1, sizes[i]);
    CHECK(p != NULL);
    if (!p) continue;
    CHECK(is_zero(p, sizes[i]));
    my_free(p);
  }
}

static inline void test_calloc_sweep_zeroes_every_size_up_to_9000(void) {
  for (size_t n = 1; n <= 9000; n += 1) {
    unsigned char* p = my_calloc(1, n);
    CHECK(p != NULL);
    if (!p) continue;
    CHECK(is_zero(p, n));
    memset(p, 0xFF, n);
    my_free(p);
  }
}

static inline void test_calloc_zeroes_recycled_dirty_blocks(void) {
  static const size_t sizes[] = {32, 100, 1000, 4000, 5000, 20000, 100000, 131000};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    unsigned char* a = my_malloc(sizes[i]);
    CHECK(a != NULL);
    if (!a) continue;
    memset(a, 0xAB, sizes[i]);
    my_free(a);
 
    unsigned char* b = my_calloc(1, sizes[i]);
    CHECK(b != NULL);
    if (!b) continue;
    CHECK(is_zero(b, sizes[i]));
    my_free(b);
  }
}

static inline void test_calloc_zeroes_a_smaller_request_inside_a_dirty_block(void) {
  unsigned char* a = my_malloc(200);
  memset(a, 0xEE, 200);
  my_free(a);
  unsigned char* b = my_calloc(1, 150);
  CHECK(b != NULL);
  CHECK(is_zero(b, 150));
  my_free(b);
}

static inline void test_calloc_multiplies_count_and_size(void) {
  unsigned char* p = my_calloc(10, 100);
  CHECK(p != NULL);
  if (!p) return;
  CHECK(test_usable_size(p) >= 1000);
  CHECK(is_zero(p, 1000));
  memset(p, 0x11, 1000);
 
  unsigned char* q = my_calloc(1000, 1000);
  CHECK(q != NULL);
  if (q) {
    CHECK(is_zero(q, 1000000));
    my_free(q);
  }
  unsigned char* r = my_calloc(3, 2000);
  CHECK(r != NULL);
  if (r) {
    CHECK(is_zero(r, 6000));
    my_free(r);
  }
  my_free(p);
}

static inline void test_calloc_overflow_returns_null_with_enomem(void) {
  struct { size_t n, s; } cases[] = {
      {SIZE_MAX, 2},
      {2, SIZE_MAX},
      {SIZE_MAX / 2 + 1, 2},
      {SIZE_MAX, SIZE_MAX},
      {(size_t)1 << 32, (size_t)1 << 32},
      {(size_t)1 << 63, 2},
      {SIZE_MAX / 4 + 1, 4},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    errno   = 0;
    void* p = my_calloc(cases[i].n, cases[i].s);
    CHECK(p == NULL);
    CHECK(errno == ENOMEM);
    if (p) my_free(p);
  }
}

static inline void test_calloc_huge_without_overflow_fails_with_enomem(void) {
  errno = 0;
  CHECK(my_calloc(1, SIZE_MAX) == NULL);
  CHECK(errno == ENOMEM);
  errno = 0;
  CHECK(my_calloc(1, SIZE_MAX - 8) == NULL);
  CHECK(errno == ENOMEM);
  errno = 0;
  CHECK(my_calloc(SIZE_MAX / 2, 1) == NULL);
  CHECK(errno == ENOMEM);
}

static inline void test_calloc_alignment(void) {
  for (size_t n = 1; n <= 3000; n += 7) {
    void* p = my_calloc(1, n);
    CHECK(p && (uintptr_t)p % 16 == 0);
    my_free(p);
  }
}

static inline void test_calloc_result_is_a_normal_heap_block(void) {
  void* live[300];
  for (int i = 0; i < 300; i++) {
    live[i] = my_calloc(1, 50 + (size_t)i * 40);
    CHECK(live[i] != NULL);
    if (live[i])
      memset(live[i], i, 50 + (size_t)i * 40);
  }
  for (int i = 0; i < 300; i++)
    my_free(live[i]);
}

static inline void test_calloc_out_of_memory(void) {
  SKIP_IF_NO_ADDRESS_LIMIT();
  void* warm = my_malloc(32);
  my_free(warm);
  limit_address_space(2 << 20);
  errno = 0;
  CHECK(my_calloc(1, 64 << 20) == NULL);
  CHECK(errno == ENOMEM);
  unlimit_address_space();
  void* p = my_calloc(1, 64 << 20);
  CHECK(p != NULL);
  if (p) {
    CHECK(is_zero(p, 4096));
    my_free(p);
  }
}

void run_all_calloc_test(void) {
  test_calloc_zero_total_returns_null();
  test_calloc_zeroes_memory_for_every_size();
  test_calloc_sweep_zeroes_every_size_up_to_9000();
  test_calloc_zeroes_recycled_dirty_blocks();
  test_calloc_zeroes_a_smaller_request_inside_a_dirty_block();
  test_calloc_multiplies_count_and_size();
  test_calloc_overflow_returns_null_with_enomem();
  test_calloc_huge_without_overflow_fails_with_enomem();
  test_calloc_alignment();
  test_calloc_result_is_a_normal_heap_block();
  test_calloc_out_of_memory();
}
