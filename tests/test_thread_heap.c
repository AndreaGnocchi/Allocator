#include "../src/thread_heap.h"
#include "../src/os.h"
#include "../include/allocator.h"
#include "test_main.h"
#include <pthread.h>
#include <string.h>

static inline void test_theap_create_is_clean(void) {
  os_init();
  thread_heap_t* h = heap_create();
  CHECK(h != NULL);
  if (!h) return;
 
  CHECK(h->small.owner == h);
  CHECK(h->medium.owner == h);
  CHECK(h->small.cur == NULL);
  CHECK(h->small.left == 0);
  CHECK(h->medium.bitmap == 0);
  CHECK(h->pool_next == NULL);
  CHECK(atomic_load(&h->remote_head) == NULL);
  for (int i = 0; i < SMALL_NUM_CLASSES; i++)
    CHECK(h->small.bins[i].head == NULL);
  for (int i = 0; i < MEDIUM_NUM_CLASSES; i++)
    CHECK(h->medium.bins[i].head == NULL);
 
  CHECK((uintptr_t)&h->remote_head % 64 == 0);
}

static inline void test_theap_create_returns_distinct_heaps(void) {
  os_init();
  thread_heap_t* a = heap_create();
  thread_heap_t* b = heap_create();
  CHECK(a && b && a != b);
  CHECK((char*)a + sizeof(thread_heap_t) <= (char*)b || (char*)b + sizeof(thread_heap_t) <= (char*)a);
  CHECK(a->small.owner == a && b->small.owner == b);
}

static inline void test_theap_thread_local_starts_null(void) {
  CHECK(t_heap == NULL);
}

static inline void test_theap_attach_sets_thread_local(void) {
  thread_heap_t* h = heap_attach();
  CHECK(h != NULL);
  CHECK(t_heap == h);
  CHECK(get_heap() == h);
  CHECK(get_heap() == h);
}

static inline void* get_heap_lazily_thread(void* arg) {
  (void)arg;
  CHECK(t_heap == NULL);
  thread_heap_t* h = get_heap();
  CHECK(h != NULL);
  CHECK(t_heap == h);

  return NULL;
}

static inline void test_theap_get_heap_attaches_lazily(void) {
  pthread_t t;
  pthread_create(&t, NULL, get_heap_lazily_thread, NULL);
  pthread_join(t, NULL);
}

static inline void* get_heap_malloc_thread(void* arg) {
  (void)arg;
  CHECK(t_heap == NULL);
  void* p = my_malloc(100);
  CHECK(p != NULL);
  CHECK(t_heap != NULL);
  if (p && t_heap)
    CHECK(chunk_of(p, SMALL_CHUNK_SIZE)->owner == t_heap);

  return NULL;
}

static inline void test_theap_malloc_attaches_heap(void) {
  pthread_t t;
  pthread_create(&t, NULL, get_heap_malloc_thread, NULL);
  pthread_join(t, NULL);
}

static inline void test_theap_large_malloc_does_not_need_a_heap(void) {
  void* p = my_malloc(1 << 20);
  CHECK(p != NULL);
  my_free(p);
}

static inline void test_theap_local_free_dispatches_by_size(void) {
  os_init();
  thread_heap_t* h = heap_create();
  void* s = small_malloc(&h->small, 64);
  void* m = medium_malloc(&h->medium, 8192);
  CHECK(s && m);
 
  CHECK(local_free(h, s, 64));
  CHECK(h->small.bins[get_small_class(64)].head == ptr_to_block(s));
  CHECK(local_free(h, m, 8192));
  CHECK(h->medium.bitmap & (1ull << MEDIUM_LAST_CLASS));
}

static inline void test_theap_remote_free_pushes_onto_owner_list(void) {
  os_init();
  thread_heap_t* h  = heap_create();
  void*          p1 = small_malloc(&h->small, 64);
  void*          p2 = small_malloc(&h->small, 64);
  void*          p3 = small_malloc(&h->small, 64);
 
  remote_free(h, p1);
  CHECK(atomic_load(&h->remote_head) == (remote_node_t*)p1);
  remote_free(h, p2);
  remote_free(h, p3);
  CHECK(atomic_load(&h->remote_head) == (remote_node_t*)p3);
  CHECK(((remote_node_t*)p3)->next == (remote_node_t*)p2);
  CHECK(((remote_node_t*)p2)->next == (remote_node_t*)p1);
  CHECK(((remote_node_t*)p1)->next == NULL);
 
  CHECK(h->small.bins[get_small_class(64)].head == NULL);
}

static inline void test_theap_drain_frees_all_remote_blocks(void) {
  os_init();
  thread_heap_t* h = heap_create();
  void*          p[3];
  for (int i = 0; i < 3; i++)
    p[i] = small_malloc(&h->small, 64);
  void* m = medium_malloc(&h->medium, 8192);
  for (int i = 0; i < 3; i++)
    remote_free(h, p[i]);
  remote_free(h, m);
 
  heap_drain_remote(h);
  CHECK(atomic_load(&h->remote_head) == NULL);
 
  void* got[3];
  for (int i = 0; i < 3; i++)
    got[i] = small_malloc(&h->small, 64);
  for (int i = 0; i < 3; i++) {
    int found = 0;
    for (int j = 0; j < 3; j++)
      found += got[i] == p[j];
    CHECK(found);
  }
  CHECK(medium_malloc(&h->medium, 8192) == m);
}

static inline void test_theap_drain_on_empty_list_is_noop(void) {
  os_init();
  thread_heap_t* h = heap_create();
  heap_drain_remote(h);
  CHECK(atomic_load(&h->remote_head) == NULL);
}

static inline void test_theap_my_malloc_drains_remote_frees(void) {
  thread_heap_t* h = get_heap();
  void*          p = my_malloc(200);
  CHECK(p != NULL);
  remote_free(h, p);
  CHECK(atomic_load(&h->remote_head) != NULL);
  void* q = my_malloc(200);
  CHECK(atomic_load(&h->remote_head) == NULL);
  CHECK(q == p);
}

typedef struct {
  thread_heap_t* heap;
  void*          block;
} life_t;
 
static inline void* thread_alloc_and_report(void* arg) {
  life_t* l = arg;
  l->block  = my_malloc(100);
  l->heap   = t_heap;
  
  return NULL;
}

static inline void test_theap_each_thread_gets_its_own_heap(void) {
  enum { N = 8 };
  pthread_t th[N];
  life_t    l[N];
  pthread_barrier_t* bar = NULL;
  (void)bar;
  
  for (int i = 0; i < N; i++)
    pthread_create(&th[i], NULL, thread_alloc_and_report, &l[i]);
  for (int i = 0; i < N; i++)
    pthread_join(th[i], NULL);
  for (int i = 0; i < N; i++) {
    CHECK(l[i].heap != NULL);
    if (l[i].block) CHECK(chunk_of(l[i].block, SMALL_CHUNK_SIZE)->owner == l[i].heap);
  }
}

typedef struct {
  pthread_barrier_t* bar;
  thread_heap_t*     heap;
} conc_t;

static inline void* thread_hold_heap(void* arg) {
  conc_t* c = arg;
  void* p   = my_malloc(64);
  c->heap   = t_heap;
  pthread_barrier_wait(c->bar);
  pthread_barrier_wait(c->bar);
  my_free(p);
  
  return NULL;
}

static inline void test_theap_concurrent_threads_have_distinct_heaps(void) {
  enum { N = 8 };
  pthread_barrier_t bar;
  pthread_barrier_init(&bar, NULL, N + 1);
  pthread_t th[N];
  conc_t    c[N];
  for (int i = 0; i < N; i++) {
    c[i].bar = &bar;
    pthread_create(&th[i], NULL, thread_hold_heap, &c[i]);
  }
  pthread_barrier_wait(&bar);
  for (int i = 0; i < N; i++)
    for (int j = i + 1; j < N; j++)
      CHECK(c[i].heap != c[j].heap);
  pthread_barrier_wait(&bar);
  for (int i = 0; i < N; i++)
    pthread_join(th[i], NULL);
}

static inline void test_theap_exited_thread_heap_is_reused(void) {
  life_t a, b;
  pthread_t t;
  pthread_create(&t, NULL, thread_alloc_and_report, &a);
  pthread_join(t, NULL);
  pthread_create(&t, NULL, thread_alloc_and_report, &b);
  pthread_join(t, NULL);
  CHECK(a.heap != NULL);
  CHECK(a.heap == b.heap);
}

static inline void test_theap_many_sequential_threads_share_one_heap(void) {
  thread_heap_t* first = NULL;
  for (int i = 0; i < 200; i++) {
    life_t    l;
    pthread_t t;
    pthread_create(&t, NULL, thread_alloc_and_report, &l);
    pthread_join(t, NULL);
    if (!first) first = l.heap;
    CHECK(l.heap == first);
  }
}
 
static inline void test_theap_blocks_outlive_their_thread(void) {
  life_t    l;
  pthread_t t;
  pthread_create(&t, NULL, thread_alloc_and_report, &l);
  pthread_join(t, NULL);
  CHECK(l.block != NULL);
 
  memset(l.block, 0x5A, 100);
  my_free(l.block);
 
  life_t    l2;
  pthread_create(&t, NULL, thread_alloc_and_report, &l2);
  pthread_join(t, NULL);
  CHECK(l2.heap == l.heap);
  CHECK(l2.block == l.block);
}

void run_all_theap_test(void) {
  test_theap_create_is_clean();
  test_theap_create_returns_distinct_heaps();
  test_theap_thread_local_starts_null();
  test_theap_attach_sets_thread_local();
  test_theap_get_heap_attaches_lazily();
  test_theap_malloc_attaches_heap();
  test_theap_large_malloc_does_not_need_a_heap();
  test_theap_local_free_dispatches_by_size();
  test_theap_remote_free_pushes_onto_owner_list();
  test_theap_drain_frees_all_remote_blocks();
  test_theap_drain_on_empty_list_is_noop();
  test_theap_my_malloc_drains_remote_frees();
  test_theap_each_thread_gets_its_own_heap();
  test_theap_concurrent_threads_have_distinct_heaps();
  test_theap_exited_thread_heap_is_reused();
  test_theap_many_sequential_threads_share_one_heap();
  test_theap_blocks_outlive_their_thread();
}
