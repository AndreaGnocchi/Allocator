#include "thread_heap.h"
#include "os.h"
#include <pthread.h>

__thread thread_heap_t* t_heap;

static pthread_key_t   g_key;
static pthread_once_t  g_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static thread_heap_t*  g_pool;

static void heap_detach(void* arg) {
  thread_heap_t* h = arg;
  t_heap = NULL;
  heap_drain_remote(h);

  pthread_mutex_lock(&g_pool_lock);
  h->pool_next = g_pool;
  g_pool = h;
  pthread_mutex_unlock(&g_pool_lock);
}

static void global_init(void) {
  os_init();
  pthread_key_create(&g_key, heap_detach);
}

thread_heap_t* heap_create(void) {
  size_t bytes = os_align_to_page(sizeof(thread_heap_t));
  if (bytes == 0) return NULL;

  thread_heap_t* h = os_map_pages(bytes);
  if (!h) return NULL;

  small_init_heap(&h->small, h);
  medium_init_heap(&h->medium, h);
  h->small.owner  = h;
  h->medium.owner = h;
  h->pool_next    = NULL;
  atomic_init(&h->remote_head, NULL);

  return h;
}

thread_heap_t* heap_attach(void) {
  pthread_once(&g_once, global_init);

  pthread_mutex_lock(&g_pool_lock);
  thread_heap_t* h = g_pool;
  if (h) g_pool = h->pool_next;
  pthread_mutex_unlock(&g_pool_lock);

  if (!h) h = heap_create();
  if (!h) return NULL;

  heap_drain_remote(h);
  t_heap = h;
  pthread_setspecific(g_key, h);
  return h;
}
