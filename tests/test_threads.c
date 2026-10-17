#include "../include/allocator.h"
#include "test_main.h"
#include "test_util.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static inline size_t mixed_size(uint64_t* rng) {
  uint64_t r = rng_next(rng) % 1000;
  if (r < 700) return rng_range(rng, 1, 600);
  if (r < 900) return rng_range(rng, 601, 4200);
  if (r < 995) return rng_range(rng, 4201, 131072);
  return rng_range(rng, 131073, 400000);
}

typedef struct {
  uint64_t seed;
  int      bad;
} local_arg_t;
 
static inline void* thread_local_workload(void* v) {
  local_arg_t* a = v;
  uint64_t rng   = a->seed;
  enum { SLOTS = 128 };
  alloc_rec_t s[SLOTS];
  memset(s, 0, sizeof s);
 
  for (int op = 0; op < 40000; op++) {
    int i = (int)(rng_next(&rng) % SLOTS);
    if (!s[i].p) {
      size_t n = mixed_size(&rng);
      s[i].p   = my_malloc(n);
      if (!s[i].p) { a->bad++; continue; }
      s[i].n    = n;
      s[i].seed = (uint32_t)(rng_next(&rng) | 1);
      fill_pattern(s[i].p, n, s[i].seed);
    } else if (rng_next(&rng) % 3 == 0) {
      size_t n = mixed_size(&rng);
      void*  q = my_realloc(s[i].p, n);
      if (!q) { a->bad++; continue; }
      size_t keep = s[i].n < n ? s[i].n : n;
      if (!check_pattern(q, keep, s[i].seed)) a->bad++;
      s[i].p    = q;
      s[i].n    = n;
      s[i].seed = (uint32_t)(rng_next(&rng) | 1);
      fill_pattern(q, n, s[i].seed);
    } else {
      if (!check_pattern(s[i].p, s[i].n, s[i].seed)) a->bad++;
      my_free(s[i].p);
      s[i].p = NULL;
    }
  }
  for (int i = 0; i < SLOTS; i++)
    if (s[i].p) {
      if (!check_pattern(s[i].p, s[i].n, s[i].seed)) a->bad++;
      my_free(s[i].p);
    }
  
  return NULL;
}

static inline void test_threads_independent_random_workloads(void) {
  enum { N = 8 };
  pthread_t   th[N];
  local_arg_t a[N];
  for (int i = 0; i < N; i++) {
    a[i] = (local_arg_t){0x1000ULL + (uint64_t)i * 7919, 0};
    pthread_create(&th[i], NULL, thread_local_workload, &a[i]);
  }
  for (int i = 0; i < N; i++)
    pthread_join(th[i], NULL);
  for (int i = 0; i < N; i++)
    CHECK(a[i].bad == 0);
}

typedef struct {
  alloc_rec_t*    items;
  size_t          cap;
  size_t          head;
  size_t          tail;
  int             producers_left;
  pthread_mutex_t mu;
  pthread_cond_t  cv;
} queue_t;
 
static inline void q_push(queue_t* q, alloc_rec_t r) {
  pthread_mutex_lock(&q->mu);

  while (q->tail - q->head == q->cap)
    pthread_cond_wait(&q->cv, &q->mu);
  
  q->items[q->tail++ % q->cap] = r;
  pthread_cond_broadcast(&q->cv);
  pthread_mutex_unlock(&q->mu);
}
 
static inline bool q_pop(queue_t* q, alloc_rec_t* out) {
  pthread_mutex_lock(&q->mu);
  
  while (q->tail == q->head && q->producers_left > 0)
    pthread_cond_wait(&q->cv, &q->mu);
  
  if (q->tail == q->head) {
    pthread_mutex_unlock(&q->mu);
    return false;
  }
  
  *out = q->items[q->head++ % q->cap];
  pthread_cond_broadcast(&q->cv);
  pthread_mutex_unlock(&q->mu);
  
  return true;
}
 
typedef struct {
  queue_t* q;
  uint64_t seed;
  int      count;
  int      bad;
} pc_arg_t;
 
static inline void* producer_fn(void* v) {
  pc_arg_t* a  = v;
  uint64_t rng = a->seed;
  for (int i = 0; i < a->count; i++) {
    size_t n = mixed_size(&rng);
    void*  p = my_malloc(n);
    if (!p) {
      a->bad++;
      continue;
    }
    
    alloc_rec_t r = {p, n, (uint32_t)(rng_next(&rng) | 1)};
    fill_pattern(p, n, r.seed);
    q_push(a->q, r);
  }
  
  pthread_mutex_lock(&a->q->mu);
  a->q->producers_left--;
  pthread_cond_broadcast(&a->q->cv);
  pthread_mutex_unlock(&a->q->mu);
  
  return NULL;
}
 
static inline void* consumer_fn(void* v) {
  pc_arg_t*   a = v;
  alloc_rec_t r;
  while (q_pop(a->q, &r)) {
    if (!check_pattern(r.p, r.n, r.seed)) a->bad++;
    my_free(r.p);
  }
  
  return NULL;
}
 
static inline void test_threads_producer_consumer_remote_frees(void) {
  enum { P = 4, C = 4 };
  queue_t q = {malloc(512 * sizeof(alloc_rec_t)), 512, 0, 0, P, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER};
  pthread_t pt[P], ct[C];
  pc_arg_t  pa[P], ca[C];
  
  for (int i = 0; i < P; i++) {
    pa[i] = (pc_arg_t){&q, 0xF00DULL + (uint64_t)i * 31, 15000, 0};
    pthread_create(&pt[i], NULL, producer_fn, &pa[i]);
  }
  for (int i = 0; i < C; i++) {
    ca[i] = (pc_arg_t){&q, 0, 0, 0};
    pthread_create(&ct[i], NULL, consumer_fn, &ca[i]);
  }
  for (int i = 0; i < P; i++)
    pthread_join(pt[i], NULL);
  for (int i = 0; i < C; i++)
    pthread_join(ct[i], NULL);
 
  for (int i = 0; i < P; i++)
    CHECK(pa[i].bad == 0);
  for (int i = 0; i < C; i++)
    CHECK(ca[i].bad == 0);
  free(q.items);
 
  for (int i = 0; i < 1000; i++) {
    void* p = my_malloc(100 + (size_t)i * 7);
    CHECK(p != NULL);
    my_free(p);
  }
}

enum { RING_BATCH = 400 };
 
typedef struct {
  int                id;
  int                n_threads;
  int                rounds;
  pthread_barrier_t* bar;
  alloc_rec_t**      batches;
  int                bad;
} ring_arg_t;
 
static inline void* ring_fn(void* v) {
  ring_arg_t* a = v;
  uint64_t rng  = 0xBEEF00ULL + (uint64_t)a->id;
  for (int round = 0; round < a->rounds; round++) {
    alloc_rec_t* mine = a->batches[a->id];
    for (int i = 0; i < RING_BATCH; i++) {
      size_t n = mixed_size(&rng);
      if (n > 150000) n = 150000;
      mine[i].p    = my_malloc(n);
      mine[i].n    = n;
      mine[i].seed = (uint32_t)(rng_next(&rng) | 1);
      if (!mine[i].p) { a->bad++; continue; }
      fill_pattern(mine[i].p, n, mine[i].seed);
    }
    pthread_barrier_wait(a->bar);
 
    alloc_rec_t* theirs = a->batches[(a->id + 1) % a->n_threads];
    for (int i = 0; i < RING_BATCH; i++) {
      if (!theirs[i].p) continue;
      if (!check_pattern(theirs[i].p, theirs[i].n, theirs[i].seed)) a->bad++;
      my_free(theirs[i].p);
    }
    pthread_barrier_wait(a->bar);
  }
  
  return NULL;
}
 
static inline void test_threads_ring_of_cross_thread_frees(void) {
  enum { N = 6, ROUNDS = 12 };
  pthread_barrier_t bar;
  pthread_barrier_init(&bar, NULL, N);
 
  alloc_rec_t* batches[N];
  pthread_t    th[N];
  ring_arg_t   a[N];
  for (int i = 0; i < N; i++)
    batches[i] = calloc(RING_BATCH, sizeof(alloc_rec_t));
  for (int i = 0; i < N; i++) {
    a[i] = (ring_arg_t){i, N, ROUNDS, &bar, batches, 0};
    pthread_create(&th[i], NULL, ring_fn, &a[i]);
  }
  for (int i = 0; i < N; i++)
    pthread_join(th[i], NULL);
  for (int i = 0; i < N; i++)
    CHECK(a[i].bad == 0);
  for (int i = 0; i < N; i++)
    free(batches[i]);
  
  pthread_barrier_destroy(&bar);
}

typedef struct {
  uint64_t seed;
  int      bad;
} large_arg_t;
 
static inline void* large_fn(void* v) {
  large_arg_t* a = v;
  uint64_t rng   = a->seed;
  for (int i = 0; i < 300; i++) {
    size_t n = rng_range(&rng, 131073, 2 << 20);
    char*  p = my_malloc(n);
    if (!p) { a->bad++; continue; }
    p[0]     = 1;
    p[n - 1] = 2;
    if (p[0] != 1 || p[n - 1] != 2) a->bad++;
    my_free(p);
  }
  
  return NULL;
}
 
static inline void test_threads_large_allocations(void) {
  enum { N = 8 };
  pthread_t   th[N];
  large_arg_t a[N];
  for (int i = 0; i < N; i++) {
    a[i] = (large_arg_t){0x77ULL + (uint64_t)i * 101, 0};
    pthread_create(&th[i], NULL, large_fn, &a[i]);
  }
  for (int i = 0; i < N; i++)
    pthread_join(th[i], NULL);
  for (int i = 0; i < N; i++)
    CHECK(a[i].bad == 0);
}
 
typedef struct {
  int bad;
} short_arg_t;
 
static inline void* short_lived_fn(void* v) {
  short_arg_t* a = v;
  void* p[60];
  for (int i = 0; i < 40; i++) {
    p[i] = my_malloc(100 + (size_t)i * 50);
    if (!p[i]) { a->bad++; continue; }
    memset(p[i], i, 100);
  }
  for (int i = 40; i < 60; i++) {
    p[i] = my_malloc(10000 + (size_t)i * 100);
    if (!p[i]) a->bad++;
  }
  for (int i = 0; i < 60; i++)
    my_free(p[i]);
  
  return NULL;
}

static inline void test_threads_many_short_lived_threads_do_not_leak_heaps(void) {
  short_arg_t a = {0};
  pthread_t   t;
  pthread_create(&t, NULL, short_lived_fn, &a);
  pthread_join(t, NULL);
 
  size_t before = vm_size_bytes();
  for (int i = 0; i < 400; i++) {
    pthread_create(&t, NULL, short_lived_fn, &a);
    pthread_join(t, NULL);
  }
  size_t grown = vm_size_bytes() - before;
  CHECK(a.bad == 0);
  CHECK(grown <= ((size_t)16 << 20));
}
 
static inline void test_threads_waves_of_concurrent_short_lived_threads(void) {
  enum { N = 16, WAVES = 12 };
  short_arg_t a[N];
  size_t      after_wave[WAVES];
  memset(a, 0, sizeof a);
 
  for (int w = 0; w < WAVES; w++) {
    pthread_t th[N];
    for (int i = 0; i < N; i++)
      pthread_create(&th[i], NULL, short_lived_fn, &a[i]);
    for (int i = 0; i < N; i++)
      pthread_join(th[i], NULL);
    after_wave[w] = vm_size_bytes();
  }
  for (int i = 0; i < N; i++)
    CHECK(a[i].bad == 0);
  
  CHECK(after_wave[WAVES - 1] - after_wave[2] <= ((size_t)16 << 20));
}
 
typedef struct {
  void* p[200];
} exit_arg_t;
 
static inline void* alloc_and_exit_fn(void* v) {
  exit_arg_t* a = v;
  for (int i = 0; i < 200; i++) {
    a->p[i] = my_malloc(50 + (size_t)(i % 7) * 900);
    if (a->p[i]) fill_pattern(a->p[i], 50, (uint32_t)i + 1);
  }
  
  return NULL;
}
 
static inline void* adopt_and_use_fn(void* v) {
  short_arg_t* a = v;
  for (int round = 0; round < 50; round++) {
    void* p[100];
    for (int i = 0; i < 100; i++) {
      p[i] = my_malloc(50 + (size_t)(i % 7) * 900);
      if (!p[i]) { a->bad++; continue; }
      fill_pattern(p[i], 50, 5);
    }
    for (int i = 0; i < 100; i++) {
      if (p[i] && !check_pattern(p[i], 50, 5)) a->bad++;
      my_free(p[i]);
    }
  }
  
  return NULL;
}
 
static inline void test_threads_blocks_of_exited_thread_are_freed_by_others(void) {
  exit_arg_t* a = calloc(1, sizeof *a);
  pthread_t   t;
  pthread_create(&t, NULL, alloc_and_exit_fn, a);
  pthread_join(t, NULL);
 
  for (int i = 0; i < 200; i++) {
    CHECK(a->p[i] != NULL);
    if (a->p[i])
      CHECK(check_pattern(a->p[i], 50, (uint32_t)i + 1));
  }
  for (int i = 0; i < 100; i++)
    my_free(a->p[i]);
 
  short_arg_t s = {0};
  pthread_create(&t, NULL, adopt_and_use_fn, &s);
  pthread_join(t, NULL);
  CHECK(s.bad == 0);
 
  for (int i = 100; i < 200; i++)
    my_free(a->p[i]);
  
  free(a);
}
 
static pthread_key_t g_dtor_key;
 
static inline void dtor_allocates(void* v) {
  int*  flag = v;
  void* p    = my_malloc(300);
  if (p) {
    memset(p, 1, 300);
    my_free(p);
    (*flag)++;
  }
}
 
static inline void* dtor_thread_fn(void* v) {
  void* a = my_malloc(64);
  void* b = my_malloc(9000);
  pthread_setspecific(g_dtor_key, v);
  my_free(a);
  my_free(b);
  
  return NULL;
}
 
static inline void test_threads_allocation_inside_thread_exit_destructor(void) {
  pthread_key_create(&g_dtor_key, dtor_allocates);
  int       flag = 0;
  pthread_t t;
  pthread_create(&t, NULL, dtor_thread_fn, &flag);
  pthread_join(t, NULL);
  CHECK(flag >= 1);
 
  short_arg_t a = {0};
  pthread_create(&t, NULL, short_lived_fn, &a);
  pthread_join(t, NULL);
  CHECK(a.bad == 0);
  pthread_key_delete(g_dtor_key);
}
 
void run_all_thread_test(void) {
  test_threads_independent_random_workloads();
  test_threads_producer_consumer_remote_frees();
  test_threads_ring_of_cross_thread_frees();
  test_threads_large_allocations();
  test_threads_many_short_lived_threads_do_not_leak_heaps();
  test_threads_waves_of_concurrent_short_lived_threads();
  test_threads_blocks_of_exited_thread_are_freed_by_others();
  test_threads_allocation_inside_thread_exit_destructor();
}
