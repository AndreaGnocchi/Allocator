#include "../include/allocator.h"
#include "../src/block.h"
#include "../src/medium.h"
#include "../src/small.h"
#include "test_main.h"
#include "test_util.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static inline size_t* make_boundary_sizes(size_t* count) {
  size_t  cap = 4096, n = 0;
  size_t* v   = malloc(cap * sizeof *v);
#define ADD(x) do { if (n == cap) { cap *= 2; v = realloc(v, cap * sizeof *v); } v[n++] = (x); } while (0)
 
  for (size_t s = 1; s <= 80; s++)
    ADD(s);
  for (int c = 0; c < SMALL_NUM_CLASSES; c++) {
    size_t sz = get_small_class_size(c);
    ADD(sz - HDR_SIZE - 1); ADD(sz - HDR_SIZE); ADD(sz - HDR_SIZE + 1);
  }
  for (int c = 0; c < MEDIUM_NUM_CLASSES; c++) {
    size_t sz = get_medium_class_size(c);
    ADD(sz - HDR_SIZE - 1); ADD(sz - HDR_SIZE); ADD(sz - HDR_SIZE + 1);
  }
  for (size_t s = 4080; s <= 4112; s++)
    ADD(s);
  for (size_t s = 131040; s <= 131100; s++)
    ADD(s);
  for (size_t s = 135130; s <= 135180; s++)
    ADD(s);
  for (int p = 4; p <= 24; p++) { ADD(((size_t)1 << p) - 1); ADD((size_t)1 << p); ADD(((size_t)1 << p) + 1); }
#undef ADD
  *count = n;
  return v;
}

static inline void test_malloc_zero_returns_null(void) {
  CHECK(my_malloc(0) == NULL);
}

static inline void test_malloc_one_byte(void) {
  char* p = my_malloc(1);
  CHECK(p != NULL);
  if (!p) return;
  CHECK((uintptr_t)p % 16 == 0);
  *p = 'x';
  CHECK(*p == 'x');
  my_free(p);
}

static inline void test_malloc_alignment_every_size_up_to_10000(void) {
  for (size_t n = 1; n <= 10000; n++) {
    void* p = my_malloc(n);
    CHECK(p != NULL);
    if (!p) continue;
    CHECK((uintptr_t)p % 16 == 0);
    my_free(p);
  }
}

static inline void test_malloc_usable_size_covers_request(void) {
  size_t  cnt;
  size_t* sizes = make_boundary_sizes(&cnt);
  for (size_t i = 0; i < cnt; i++) {
    void* p = my_malloc(sizes[i]);
    CHECK(p != NULL);
    if (!p) continue;
    CHECK(test_usable_size(p) >= sizes[i]);
    my_free(p);
  }
  free(sizes);
}

static inline void test_malloc_boundary_sizes_hold_data(void) {
  size_t  cnt;
  size_t* sizes = make_boundary_sizes(&cnt);
  for (size_t i = 0; i < cnt; i++) {
    size_t n = sizes[i];
    if (n > ((size_t)4 << 20)) continue;
    unsigned char* p = my_malloc(n);
    CHECK(p != NULL);
    if (!p) continue;
    fill_pattern(p, n, (uint32_t)i + 1);
    CHECK(check_pattern(p, n, (uint32_t)i + 1));
    p[n - 1] ^= 0xFF;
    my_free(p);
  }
  free(sizes);
}

static void test_malloc_live_blocks_never_overlap_or_get_clobbered(void) {
  size_t       cnt;
  size_t*      sizes = make_boundary_sizes(&cnt);
  alloc_rec_t* recs  = malloc(cnt * sizeof *recs);
  size_t       live  = 0;
 
  for (size_t i = 0; i < cnt; i++) {
    size_t n = sizes[i];
    if (n > ((size_t)1 << 20)) continue;
    void* p = my_malloc(n);
    CHECK(p != NULL);
    if (!p) continue;
    recs[live] = (alloc_rec_t){p, n, (uint32_t)i + 1};
    fill_pattern(p, n, recs[live].seed);
    live++;
  }
  CHECK(recs_disjoint(recs, live));
  for (size_t i = 0; i < live; i++)
    CHECK(check_pattern(recs[i].p, recs[i].n, recs[i].seed));
 
  void** ptrs = malloc(live * sizeof *ptrs);
  for (size_t i = 0; i < live; i++)
    ptrs[i] = recs[i].p;
 
  for (size_t i = 0; i < live; i++)
    my_free(recs[i].p);
  free(ptrs);
  free(recs);
  free(sizes);
}

static inline void test_malloc_returns_distinct_pointers_in_each_heap(void) {
  static const size_t sizes[] = {1, 100, 3000, 5000, 100000, 140000, 1 << 20};
  for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
    enum { N = 200 };
    void* p[N];
    for (int i = 0; i < N; i++) p[i] = my_malloc(sizes[s]);
    for (int i = 0; i < N; i++)
      for (int j = i + 1; j < N; j++) {
        if (p[i] == p[j]) {
          CHECK(0);
          i = N;
          break;
        }
      }
    for (int i = 0; i < N; i++)
      my_free(p[i]);
  }
}

static inline void test_malloc_size_classes_route_to_expected_heap(void) {
  struct { size_t n; kind_t k; } cases[] = {
      {1, KIND_SMALL},       {4088, KIND_SMALL},     {4089, KIND_MEDIUM},
      {131064, KIND_MEDIUM}, {131065, KIND_LARGE},   {131073, KIND_LARGE},
      {1 << 20, KIND_LARGE},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    void* p = my_malloc(cases[i].n);
    CHECK(p != NULL);
    if (!p) continue;
    CHECK(test_kind(p) == cases[i].k);
    my_free(p);
  }
}

static inline void test_malloc_huge_sizes_fail_with_enomem(void) {
  size_t huge[] = {SIZE_MAX, SIZE_MAX - 1, SIZE_MAX - 8, SIZE_MAX - 15, SIZE_MAX - 23, SIZE_MAX - 24,
                   SIZE_MAX - 30, SIZE_MAX - 4095, SIZE_MAX / 2, SIZE_MAX / 2 + 1, (size_t)1 << 62,
                   (size_t)1 << 56};
  for (size_t i = 0; i < sizeof(huge) / sizeof(huge[0]); i++) {
    errno   = 0;
    void* p = my_malloc(huge[i]);
    CHECK(p == NULL);
    CHECK(errno == ENOMEM);
    if (p) my_free(p);
  }
}

static inline void test_malloc_success_leaves_errno_alone(void) {
  errno = 12345;
  void* p = my_malloc(64);
  CHECK(p != NULL);
  CHECK(errno == 12345);
  void* q = my_malloc(1 << 20);
  CHECK(errno == 12345);
  my_free(p);
  my_free(q);
}

static inline void test_malloc_free_loops_do_not_leak_address_space(void) {
  static const size_t sizes[] = {24, 100, 1000, 4000, 5000, 20000, 131000, 300000, 2 << 20};
  for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
    void* warm = my_malloc(sizes[s]);
    my_free(warm);
    size_t before = vm_size_bytes();
    for (int i = 0; i < 20000; i++) {
      void* p = my_malloc(sizes[s]);
      if (!p) {
        CHECK(0);
        return;
      }
      
      ((char*)p)[0] = 1;
      my_free(p);
    }
    size_t grown = vm_size_bytes() - before;
    CHECK(grown <= (2 << 20));
  }
}

static inline void test_malloc_reuses_freed_memory_for_batches(void) {
  enum { N = 2000 };
  static void* p[N];
  for (int round = 0; round < 3; round++)
    for (int i = 0; i < N; i++) p[i] = my_malloc(64 + (size_t)(i % 50) * 16), my_free(p[i]);
 
  size_t before = vm_size_bytes();
  for (int round = 0; round < 30; round++) {
    for (int i = 0; i < N; i++) {
      p[i] = my_malloc(64 + (size_t)(i % 50) * 16);
      if (!p[i]) {
        CHECK(0);
        return;
      }
    }
    for (int i = 0; i < N; i++) my_free(p[i]);
  }
  size_t grown = vm_size_bytes() - before;
  CHECK(grown <= (1 << 20));
}

static inline void test_malloc_out_of_memory_small(void) {
  SKIP_IF_NO_ADDRESS_LIMIT();
  
  enum { MAX = 500000 };
  void** p = malloc(MAX * sizeof *p);
  memset(p, 0, MAX * sizeof *p);
  void* warm = my_malloc(32);
  my_free(warm);
  limit_address_space(2 << 20);
 
  int n = 0;
  errno    = 0;
  while (n < MAX) {
    void* q = my_malloc(2000);
    if (!q) break;
    p[n++] = q;
  }
  CHECK(n < MAX);
  CHECK(errno == ENOMEM);
 
  for (int i = 0; i < n; i++) my_free(p[i]);
  unlimit_address_space();
  void* again = my_malloc(2000);
  CHECK(again != NULL);
  free(p);
}

static inline void test_malloc_out_of_memory_medium_and_large(void) {
  SKIP_IF_NO_ADDRESS_LIMIT();
  
  void* warm = my_malloc(32);
  my_free(warm);
  limit_address_space(6 << 20);
 
  void* p[64];
  int   n = 0;
  errno   = 0;
  while (n < 64) {
    void* q = my_malloc(1 << 20);
    if (!q) break;
    p[n++] = q;
  }
  CHECK(n < 64);
  CHECK(errno == ENOMEM);
 
  errno = 0;
  CHECK(my_malloc(64 << 20) == NULL);
  CHECK(errno == ENOMEM);
 
  for (int i = 0; i < n; i++) my_free(p[i]);
  unlimit_address_space();
  void* big = my_malloc(1 << 20);
  CHECK(big != NULL);
  my_free(big);
}

void run_all_malloc_test(void) {
  test_malloc_zero_returns_null();
  test_malloc_one_byte();
  test_malloc_alignment_every_size_up_to_10000();
  test_malloc_usable_size_covers_request();
  test_malloc_boundary_sizes_hold_data();
  test_malloc_live_blocks_never_overlap_or_get_clobbered();
  test_malloc_returns_distinct_pointers_in_each_heap();
  test_malloc_size_classes_route_to_expected_heap();
  test_malloc_huge_sizes_fail_with_enomem();
  test_malloc_success_leaves_errno_alone();
  test_malloc_free_loops_do_not_leak_address_space();
  test_malloc_reuses_freed_memory_for_batches();
  test_malloc_out_of_memory_small();
  test_malloc_out_of_memory_medium_and_large();
}
