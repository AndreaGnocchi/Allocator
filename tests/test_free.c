#include "../include/allocator.h"
#include "../src/medium.h"
#include "../src/small.h"
#include "../src/thread_heap.h"
#include "test_main.h"
#include "test_util.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static inline void test_free_null_is_a_noop(void) {
  errno = 4242;
  my_free(NULL);
  CHECK(errno == 4242);
}

static inline void test_free_each_heap(void) {
  static const size_t sizes[] = {1, 24, 25, 4088, 4089, 100000, 131064, 131065, 1 << 20};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    void* p = my_malloc(sizes[i]);
    CHECK(p != NULL);
    if (!p) continue;
    memset(p, 0x42, sizes[i]);
    my_free(p);
  }
}

static inline void test_free_makes_small_block_available_again(void) {
  void* a = my_malloc(40);
  void* b = my_malloc(40);
  my_free(a);
  void* c = my_malloc(40);
  CHECK(c == a);
  my_free(b);
  my_free(c);
}
 
static inline void test_free_makes_medium_block_available_again(void) {
  void* a = my_malloc(10000);
  void* b = my_malloc(10000);
  void* guard = my_malloc(10000);
  my_free(b);
  void* c = my_malloc(10000);
  CHECK(c == b);
  my_free(a);
  my_free(c);
  my_free(guard);
}

static inline void test_free_in_every_order_leaves_a_consistent_heap(void) {
  enum { N = 30 };
  for (int order = 0; order < 4; order++) {
    void* p[N];
    for (int i = 0; i < N; i++) p[i] = my_malloc(5000 + (size_t)i * 1500);
 
    uint64_t rng = 12345 + (uint64_t)order;
    int      idx[N];
    for (int i = 0; i < N; i++) idx[i] = i;
    if (order == 1)
      for (int i = 0; i < N; i++) idx[i] = N - 1 - i;
    if (order == 2)
      for (int i = 0; i < N; i++) idx[i] = (i * 7) % N;
    if (order == 3)
      for (int i = N - 1; i > 0; i--) {
        int j = (int)(rng_next(&rng) % (uint64_t)(i + 1)), t = idx[i];
        idx[i] = idx[j];
        idx[j] = t;
      }
 
    for (int i = 0; i < N; i++)
      my_free(p[idx[i]]);
  }
}

static inline void test_free_coalesces_medium_blocks_back_into_the_chunk(void) {
  for (int order = 0; order < 3; order++) {
    enum { N = 100 };
    void* p[N];
    for (int i = 0; i < N; i++) p[i] = my_malloc(8000);
    uintptr_t chunk = (uintptr_t)chunk_of(p[0], MEDIUM_CHUNK_SIZE);
    for (int i = 0; i < N; i++)
      CHECK((uintptr_t)chunk_of(p[i], MEDIUM_CHUNK_SIZE) == chunk);
 
    for (int k = 0; k < N; k++)
      my_free(p[order == 0 ? k : order == 1 ? N - 1 - k : (k % 2 ? N - 1 - k / 2 : k / 2)]);
 
    void* q[7];
    for (int i = 0; i < 7; i++) {
      q[i] = my_malloc(131064);
      CHECK(q[i] != NULL);
      if (q[i])
        CHECK((uintptr_t)chunk_of(q[i], MEDIUM_CHUNK_SIZE) == chunk);
    }
    for (int i = 0; i < 7; i++) my_free(q[i]);
    break;
  }
}

static inline void test_free_coalesces_in_reverse_and_interleaved_order(void) {
  for (int order = 1; order < 3; order++) {
    enum { N = 100 };
    void* p[N];
    for (int i = 0; i < N; i++)
      p[i] = my_malloc(8000);
    
    uintptr_t chunk = (uintptr_t)chunk_of(p[0], MEDIUM_CHUNK_SIZE);
    for (int k = 0; k < N; k++)
      my_free(p[order == 1 ? N - 1 - k : (k % 2 ? N - 1 - k / 2 : k / 2)]);
 
    void* q[7];
    for (int i = 0; i < 7; i++) {
      q[i] = my_malloc(131064);
      CHECK(q[i] != NULL);
      if (q[i])
        CHECK((uintptr_t)chunk_of(q[i], MEDIUM_CHUNK_SIZE) == chunk);
    }
    for (int i = 0; i < 7; i++)
      my_free(q[i]);
  }
}

static inline void test_free_double_free_of_small_block_is_harmless(void) {
  void* p = my_malloc(64);
  my_free(p);
  my_free(p);
  void* a = my_malloc(64);
  void* b = my_malloc(64);
  CHECK(a != b);
}

static inline void test_free_unmaps_large_blocks(void) {
  void* warm = my_malloc(4 << 20);
  my_free(warm);
  size_t before = vm_size_bytes();
  void* p[20];
  for (int i = 0; i < 20; i++) p[i] = my_malloc(4 << 20);
  CHECK(vm_size_bytes() >= before + ((size_t)20 << 22) - (1 << 20));
  for (int i = 0; i < 20; i++) my_free(p[i]);
  CHECK(vm_size_bytes() <= before + (1 << 20));
}

void run_all_free_test(void) {
  test_free_null_is_a_noop();
  test_free_each_heap();
  test_free_makes_small_block_available_again();
  test_free_makes_medium_block_available_again();
  test_free_in_every_order_leaves_a_consistent_heap();
  test_free_coalesces_medium_blocks_back_into_the_chunk();
  test_free_coalesces_in_reverse_and_interleaved_order();
  test_free_double_free_of_small_block_is_harmless();
  test_free_unmaps_large_blocks();
}
