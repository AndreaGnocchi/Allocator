#include "../include/allocator.h"
#include "../src/small.h"
#include "../src/medium.h"
#include "test_main.h"
#include "test_util.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
  void*    p;
  size_t   n;
  uint32_t seed;
} slot_t;
 
static inline size_t pick_size(uint64_t* rng) {
  uint64_t r = rng_next(rng) % 1000;
  if (r < 650) return rng_range(rng, 1, 512);
  if (r < 880) return rng_range(rng, 513, 4200);
  if (r < 975) return rng_range(rng, 4201, 131072);
  if (r < 990) return rng_range(rng, 130000, 135500);
  return rng_range(rng, 135501, 600000);
}

static inline bool all_slots_ok(slot_t* s, size_t n) {
  bool ok = true;
  for (size_t i = 0; i < n; i++) {
    if (!s[i].p) continue;
    if (!check_pattern(s[i].p, s[i].n, s[i].seed)) {
      ok = false;
    }
  }
  return ok;
}
 
static inline void verify_heap(slot_t* s, size_t n) {
  alloc_rec_t* recs = malloc(n * sizeof *recs);
  void**       ptrs = malloc(n * sizeof *ptrs);
  size_t       live = 0;
  for (size_t i = 0; i < n; i++)
    if (s[i].p) {
      recs[live] = (alloc_rec_t){s[i].p, s[i].n, s[i].seed};
      ptrs[live] = s[i].p;
      live++;
    }
  
  CHECK(all_slots_ok(s, n));
  CHECK(recs_disjoint(recs, live));
  free(recs);
  free(ptrs);
}
 
static inline void random_ops(uint64_t seed, size_t nslots, size_t ops, size_t verify_every) {
  slot_t* slots = calloc(nslots, sizeof *slots);
  uint64_t rng  = seed;
 
  for (size_t op = 0; op < ops; op++) {
    size_t  i = rng_next(&rng) % nslots;
    slot_t* s = &slots[i];
    int     r = (int)(rng_next(&rng) % 100);
 
    if (!s->p) {
      size_t n = pick_size(&rng);
      bool   z = r < 15;
      s->p     = z ? my_calloc(1, n) : my_malloc(n);
      CHECK(s->p != NULL);
      if (!s->p) continue;
      
      CHECK((uintptr_t)s->p % 16 == 0);
      CHECK(test_usable_size(s->p) >= n);
      if (z)
        CHECK(is_zero(s->p, n));
      
      s->n    = n;
      s->seed = (uint32_t)(rng_next(&rng) | 1);
      fill_pattern(s->p, n, s->seed);
    } else if (r < 45) {
      CHECK(check_pattern(s->p, s->n, s->seed));
      my_free(s->p);
      s->p = NULL;
    } else {
      size_t n = pick_size(&rng);
      if (r > 90) {
        n = s->n + rng_range(&rng, 1, 64);
      } else if (r > 80 && s->n > 40) {
        n = s->n - rng_range(&rng, 1, 30);
      }
 
      void* q = my_realloc(s->p, n);
      CHECK(q != NULL);
      if (!q) continue;
      
      size_t keep = s->n < n ? s->n : n;
      CHECK(check_pattern(q, keep, s->seed));
      CHECK(test_usable_size(q) >= n);
      s->p    = q;
      s->n    = n;
      s->seed = (uint32_t)(rng_next(&rng) | 1);
      fill_pattern(q, n, s->seed);
    }
 
    if (verify_every && op % verify_every == verify_every - 1)
      verify_heap(slots, nslots);
  }
 
  verify_heap(slots, nslots);
  for (size_t i = 0; i < nslots; i++)
    if (slots[i].p)
      my_free(slots[i].p);
  free(slots);
}

static inline void test_stress_random_ops_seed_1(void) {
  random_ops(0x9E3779B97F4A7C15ULL, 400, 60000, 3000);
}

static inline void test_stress_random_ops_seed_2(void) {
  random_ops(0xD1B54A32D192ED03ULL, 64, 60000, 3000);
}

static inline void test_stress_random_ops_seed_3(void) {
  random_ops(0x2545F4914F6CDD1DULL, 1500, 80000, 5000);
}

static inline void test_stress_random_ops_tiny_slot_count(void) {
  random_ops(0xABCDEF12345ULL, 3, 30000, 1000);
}

static inline void test_stress_every_size_alloc_free(void) {
  for (size_t n = 1; n < 140000; n += 13) {
    void* p = my_malloc(n);
    CHECK(p != NULL && (uintptr_t)p % 16 == 0);
    if (!p) return;
    
    ((char*)p)[0] = 1;
    ((char*)p)[n - 1] = 2;
    my_free(p);
  }
}

static inline void test_stress_one_block_per_class_live_together(void) {
  enum { N = SMALL_NUM_CLASSES + MEDIUM_NUM_CLASSES };
  slot_t s[N * 3];
  size_t cnt = 0;
  for (int rep = 0; rep < 3; rep++) {
    for (int c = 0; c < SMALL_NUM_CLASSES; c++) {
      size_t n = get_small_class_size(c) - 8;
      s[cnt].p = my_malloc(n); s[cnt].n = n; s[cnt].seed = (uint32_t)cnt + 1;
      CHECK(s[cnt].p != NULL);
      if (s[cnt].p)
        fill_pattern(s[cnt].p, n, s[cnt].seed);
      
      cnt++;
    }
    for (int c = 0; c < MEDIUM_NUM_CLASSES; c++) {
      size_t n = get_medium_class_size(c) - 8;
      s[cnt].p = my_malloc(n); s[cnt].n = n; s[cnt].seed = (uint32_t)cnt + 1;
      CHECK(s[cnt].p != NULL);
      if (s[cnt].p)
        fill_pattern(s[cnt].p, n, s[cnt].seed);
      
      cnt++;
    }
  }
  verify_heap(s, cnt);
 
  for (size_t i = 0; i < cnt; i += 2) {
    my_free(s[i].p);
    s[i].p = NULL;
  }
  
  verify_heap(s, cnt);
 
  for (size_t i = 0; i < cnt; i += 2) {
    s[i].p = my_malloc(s[i].n);
    CHECK(s[i].p != NULL);
    if (s[i].p)
      fill_pattern(s[i].p, s[i].n, s[i].seed);
  }
  verify_heap(s, cnt);
  for (size_t i = 0; i < cnt; i++) my_free(s[i].p);
}

static inline void test_stress_repeated_identical_rounds_reach_steady_state(void) {
  enum { N = 1500 };
  static void* p[N];
  size_t after_round[12];
 
  for (int round = 0; round < 12; round++) {
    uint64_t rng = 0x1234567ULL;
    for (int i = 0; i < N; i++) {
      size_t n = pick_size(&rng);
      if (n > 200000)
        n = 200000;
      p[i] = my_malloc(n);
      if (!p[i]) {
        CHECK(0);
        return;
      }
      
      ((char*)p[i])[0] = 1;
    }
    for (int i = 0; i < N; i++)
      my_free(p[i]);
    after_round[round] = vm_size_bytes();
  }
  size_t grown = after_round[11] - after_round[2];
  CHECK(grown <= (1 << 20));
}

static inline void test_stress_fragmentation_pattern_stays_bounded(void) {
  enum { N = 3000 };
  static void* p[N];
  for (int round = 0; round < 10; round++) {
    for (int i = 0; i < N; i++)
      p[i] = my_malloc(8000 + (size_t)(i % 5) * 4000);
    for (int i = 0; i < N; i += 2)
      my_free(p[i]);
    for (int i = 0; i < N; i += 2)
      p[i] = my_malloc(8000 + (size_t)(i % 5) * 4000);
    for (int i = 0; i < N; i++)
      my_free(p[i]);
  }
  size_t before = vm_size_bytes();
  for (int round = 0; round < 10; round++) {
    for (int i = 0; i < N; i++)
      p[i] = my_malloc(8000 + (size_t)(i % 5) * 4000);
    for (int i = 0; i < N; i += 2)
      my_free(p[i]);
    for (int i = 0; i < N; i += 2)
      p[i] = my_malloc(8000 + (size_t)(i % 5) * 4000);
    for (int i = 0; i < N; i++)
      my_free(p[i]);
  }
  size_t grown = vm_size_bytes() - before;
  CHECK(grown <= (2 << 20));
}

static inline void test_stress_large_churn(void) {
  uint64_t rng = 99;
  void*    p[64] = {0};
  size_t   n[64];
  for (int op = 0; op < 3000; op++) {
    int i = (int)(rng_next(&rng) % 64);
    if (p[i]) {
      CHECK(check_pattern(p[i], n[i], (uint32_t)i + 1));
      my_free(p[i]);
      p[i] = NULL;
    } else {
      n[i] = rng_range(&rng, 131073, 3 << 20);
      p[i] = my_malloc(n[i]);
      CHECK(p[i] != NULL);
      if (p[i]) {
        fill_pattern(p[i], 4096, (uint32_t)i + 1);
        n[i] = 4096;
      }
    }
  }
  for (int i = 0; i < 64; i++) my_free(p[i]);
}

void run_all_stress_test(void) {
  test_stress_random_ops_seed_1();
  test_stress_random_ops_seed_2();
  test_stress_random_ops_seed_3();
  test_stress_random_ops_tiny_slot_count();
  test_stress_every_size_alloc_free();
  test_stress_one_block_per_class_live_together();
  test_stress_repeated_identical_rounds_reach_steady_state();
  test_stress_fragmentation_pattern_stays_bounded();
  test_stress_large_churn();
}
