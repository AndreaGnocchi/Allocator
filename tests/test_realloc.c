#include "../include/allocator.h"
#include "test_main.h"
#include "test_util.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static const size_t k_sizes[] = {
  1,      8,      24,      25,     32,     100,    256,    1000,
  4000,   4088,   4089,    5000,   20000,  131064, 131065, 131072,
  131073, 200000, 1 << 20, 5 << 20
};

static inline void test_realloc_null_acts_like_malloc(void) {
  void* p = my_realloc(NULL, 100);
  CHECK(p != NULL);
  if (p) {
    CHECK((uintptr_t)p % 16 == 0);
    CHECK(test_usable_size(p) >= 100);
    my_free(p);
  }
  void* big = my_realloc(NULL, 1 << 20);
  CHECK(big != NULL);
  my_free(big);
}

static inline void test_realloc_null_with_zero_size_returns_null(void) {
  CHECK(my_realloc(NULL, 0) == NULL);
}

static inline void test_realloc_to_zero_frees_and_returns_null(void) {
  static const size_t sizes[] = {10, 3000, 50000, 1 << 20};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    void* p = my_malloc(sizes[i]);
    CHECK(p != NULL);
    CHECK(my_realloc(p, 0) == NULL);
  }
}

static inline void test_realloc_zero_releases_memory_for_reuse(void) {
  void* a = my_malloc(64);
  my_realloc(a, 0);
  void* b = my_malloc(64);
  CHECK(a == b);
}

static inline void test_realloc_every_size_pair_preserves_data(void) {
  for (size_t i = 0; i < sizeof(k_sizes) / sizeof(k_sizes[0]); i++)
    for (size_t j = 0; j < sizeof(k_sizes) / sizeof(k_sizes[0]); j++) {
      size_t from = k_sizes[i], to = k_sizes[j];
      unsigned char* p = my_malloc(from);
      CHECK(p != NULL);
      if (!p) continue;
      fill_pattern(p, from, 17);
 
      unsigned char* q = my_realloc(p, to);
      CHECK(q != NULL);
      if (!q) {
        my_free(p);
        continue;
      }
 
      size_t keep = from < to ? from : to;
      CHECK(check_pattern(q, keep, 17));
      CHECK(test_usable_size(q) >= to);
      CHECK((uintptr_t)q % 16 == 0);
 
      fill_pattern(q, to, 99);
      CHECK(check_pattern(q, to, 99));
      my_free(q);
    }
}

static inline void test_realloc_same_size_keeps_data(void) {
  for (size_t i = 0; i < sizeof(k_sizes) / sizeof(k_sizes[0]); i++) {
    unsigned char* p = my_malloc(k_sizes[i]);
    fill_pattern(p, k_sizes[i], 5);
    unsigned char* q = my_realloc(p, k_sizes[i]);
    CHECK(q != NULL && check_pattern(q, k_sizes[i], 5));
    my_free(q);
  }
}

static inline void test_realloc_within_same_small_class_keeps_pointer(void) {
  char* p = my_malloc(100);
  fill_pattern(p, 100, 3);
  char* q = my_realloc(p, 104);
  CHECK(q == p);
  CHECK(check_pattern(q, 100, 3));
  char* r = my_realloc(q, 90);
  CHECK(r == q);
  my_free(r);
}

static inline void test_realloc_grow_past_class_moves_and_frees_old_block(void) {
  char* p = my_malloc(100);
  fill_pattern(p, 100, 3);
  char* q = my_realloc(p, 2000);
  CHECK(q != NULL && q != p);
  CHECK(check_pattern(q, 100, 3));
  char* again = my_malloc(100);
  CHECK(again == p);
  my_free(again);
  my_free(q);
}

static void test_realloc_doubling_chain_up_to_16mb_and_back(void) {
  size_t chain[64], len = 0;
  size_t n = 1;
  unsigned char* p = my_malloc(n);
  fill_pattern(p, n, 1);
  chain[len++] = n;
 
  while (n < ((size_t)16 << 20)) {
    size_t m = n * 2 + 1;
    unsigned char* q = my_realloc(p, m);
    CHECK(q != NULL);
    if (!q) {
      my_free(p);
      return;
    }
    
    CHECK(check_pattern(q, n, 1));
    fill_pattern(q, m, 1);
    chain[len++] = m;
    p = q;
    n = m;
  }
 
  while (len > 1) {
    len--;
    size_t m = chain[len - 1];
    unsigned char* q = my_realloc(p, m);
    CHECK(q != NULL);
    if (!q) {
      my_free(p);
      return;
    }
    
    CHECK(check_pattern(q, m, 1));
    p = q;
    n = m;
  }
  my_free(p);
}

static inline void test_realloc_small_increments_up_to_20000(void) {
  size_t         n = 1;
  unsigned char* p = my_malloc(n);
  fill_pattern(p, n, 1);
  for (int step = 0; step < 3000 && n < 20000; step++) {
    size_t m = n + 1 + (size_t)(step % 13);
    unsigned char* q = my_realloc(p, m);
    CHECK(q != NULL);
    if (!q) {
      my_free(p);
      return;
    }
    
    CHECK(check_pattern(q, n, 1));
    fill_pattern(q, m, 1);
    p = q;
    n = m;
  }
  my_free(p);
}

static inline void test_realloc_shrinking_steps_keep_prefix(void) {
  size_t         n = 150000;
  unsigned char* p = my_malloc(n);
  fill_pattern(p, n, 8);
  while (n > 16) {
    size_t m = n - n / 7 - 1;
    unsigned char* q = my_realloc(p, m);
    CHECK(q != NULL);
    if (!q) {
      my_free(p);
      return;
    }

    CHECK(check_pattern(q, m, 8));
    p = q;
    n = m;
  }
  my_free(p);
}

static inline void test_realloc_medium_grows_into_freed_neighbour(void) {
  char* a = my_malloc(10000);
  char* b = my_malloc(10000);
  char* c = my_malloc(10000);
  fill_pattern(a, 10000, 21);
  my_free(b);
 
  char* a2 = my_realloc(a, 18000);
  CHECK(a2 != NULL);
  if (a2) {
    CHECK(check_pattern(a2, 10000, 21));
    fill_pattern(a2, 18000, 22);
  }
  
  my_free(a2);
  my_free(c);
}

static inline void test_realloc_large_shrink_slightly_keeps_pointer_and_data(void) {
  size_t n = 1 << 20;
  char*  p = my_malloc(n);
  fill_pattern(p, n, 4);
  char* q = my_realloc(p, n - 1000);
  CHECK(q != NULL);
  CHECK(check_pattern(q, n - 1000, 4));
  my_free(q);
}

static inline void test_realloc_large_shrink_to_small_and_medium(void) {
  size_t n = 4 << 20;
  char*  p = my_malloc(n);
  fill_pattern(p, n, 6);
  char* q = my_realloc(p, 3000);
  CHECK(q != NULL && check_pattern(q, 3000, 6));
  CHECK(q && test_kind(q) == KIND_SMALL);
 
  char* r = my_malloc(n);
  fill_pattern(r, n, 6);
  char* s = my_realloc(r, 50000);
  CHECK(s != NULL && check_pattern(s, 50000, 6));
  CHECK(s && test_kind(s) == KIND_MEDIUM);
  my_free(q);
  my_free(s);
}

static inline void test_realloc_large_grow_preserves_everything(void) {
  size_t n = 300000;
  char*  p = my_malloc(n);
  fill_pattern(p, n, 12);
  char* q = my_realloc(p, 3 * n);
  CHECK(q != NULL);
  if (q) {
    CHECK(check_pattern(q, n, 12));
    memset(q, 0, 3 * n);
    my_free(q);
  }
}

static inline void test_realloc_failure_leaves_original_block_intact(void) {
  static const size_t sizes[] = {50, 5000, 300000};
  size_t huge[] = {SIZE_MAX, SIZE_MAX - 8, SIZE_MAX - 23, SIZE_MAX - 4095, SIZE_MAX / 2, (size_t)1 << 62};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    for (size_t j = 0; j < sizeof(huge) / sizeof(huge[0]); j++) {
      unsigned char* p = my_malloc(sizes[i]);
      fill_pattern(p, sizes[i], 31);
      errno = 0;
      void* q = my_realloc(p, huge[j]);
      CHECK(q == NULL);
      CHECK(errno == ENOMEM);
      CHECK(check_pattern(p, sizes[i], 31));
      my_free(p);
    }
}

static inline void test_realloc_out_of_memory_keeps_original(void) {
  SKIP_IF_NO_ADDRESS_LIMIT();
  unsigned char* p = my_malloc(100000);
  fill_pattern(p, 100000, 40);
  void* warm = my_malloc(32);
  my_free(warm);
 
  limit_address_space(2 << 20);
  errno   = 0;
  void* q = my_realloc(p, 64 << 20);
  CHECK(q == NULL);
  CHECK(errno == ENOMEM);
  CHECK(check_pattern(p, 100000, 40));
  unlimit_address_space();
 
  q = my_realloc(p, 64 << 20);
  CHECK(q != NULL);
  if (q) {
    CHECK(check_pattern(q, 100000, 40));
    my_free(q);
  }
}

typedef struct {
  void*  p;
  size_t n;
} handoff_t;
 
static inline void* thread_make_block(void* arg) {
  handoff_t* h = arg;
  h->p = my_malloc(h->n);
  fill_pattern(h->p, h->n, 77);
  return NULL;
}
 
static void test_realloc_block_owned_by_another_thread(void) {
  static const size_t from[] = {100, 5000, 50000};
  static const size_t to[]   = {120, 90, 6000, 30000, 100000, 400000};
  for (size_t i = 0; i < sizeof(from) / sizeof(from[0]); i++)
    for (size_t j = 0; j < sizeof(to) / sizeof(to[0]); j++) {
      handoff_t h = {NULL, from[i]};
      pthread_t t;
      pthread_create(&t, NULL, thread_make_block, &h);
      pthread_join(t, NULL);
      CHECK(h.p != NULL);
      if (!h.p) continue;
 
      unsigned char* q = my_realloc(h.p, to[j]);
      CHECK(q != NULL);
      if (!q) continue;
      size_t keep = from[i] < to[j] ? from[i] : to[j];
      CHECK(check_pattern(q, keep, 77));
      my_free(q);
    }
}

void run_all_realloc_test(void) {
  test_realloc_null_acts_like_malloc();
  test_realloc_null_with_zero_size_returns_null();
  test_realloc_to_zero_frees_and_returns_null();
  test_realloc_zero_releases_memory_for_reuse();
  test_realloc_every_size_pair_preserves_data();
  test_realloc_same_size_keeps_data();
  test_realloc_within_same_small_class_keeps_pointer();
  test_realloc_grow_past_class_moves_and_frees_old_block();
  test_realloc_doubling_chain_up_to_16mb_and_back();
  test_realloc_small_increments_up_to_20000();
  test_realloc_shrinking_steps_keep_prefix();
  test_realloc_medium_grows_into_freed_neighbour();
  test_realloc_large_shrink_slightly_keeps_pointer_and_data();
  test_realloc_large_shrink_to_small_and_medium();
  test_realloc_large_grow_preserves_everything();
  test_realloc_failure_leaves_original_block_intact();
  test_realloc_out_of_memory_keeps_original();
  test_realloc_block_owned_by_another_thread();
}
