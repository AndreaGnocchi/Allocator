#include "../src/small.h"
#include "../src/os.h"
#include "test_main.h"
#include "test_util.h"
#include <limits.h>
#include <stdalign.h>
#include <string.h>

static const size_t k_small_sizes[SMALL_NUM_CLASSES] = {
  32,  48,  64,  80,  96,  112, 128, 144,  160,  176,  192,  208,  224,  240,  256,
  288, 320, 352, 384, 416, 448, 480, 512,  576,  640,  704,  768,  832,  896,  960,
  1024, 1152, 1280, 1408, 1536, 1664, 1792, 1920, 2048, 2304, 2560, 2816, 3072, 3328,
  3584, 3840, 4096
};

static small_heap_t H;

static inline void init(void) {
  os_init();
  small_init_heap(&H, &H);
}

static alignas(16) unsigned char g_arena[4096];

static inline block_t* fake(int i) {
  return (block_t*)(g_arena + 8 + 64 * i);
}

static inline void make_fake(int i, size_t size, bool allocated) {
  memset(fake(i), 0, sizeof(block_t));
  block_set_hdr(fake(i), size, allocated, false);
}

static inline void test_small_class_sizes_match_design(void) {
  for (int i = 0; i < SMALL_NUM_CLASSES; i++)
    CHECK(get_small_class_size(i) == k_small_sizes[i]);
 
  CHECK(get_small_class_size(-1)                == 0);
  CHECK(get_small_class_size(SMALL_NUM_CLASSES) == 0);
  CHECK(get_small_class_size(1000)              == 0);
  CHECK(get_small_class_size(INT_MIN)           == 0);
  CHECK(get_small_class_size(INT_MAX)           == 0);
}

static inline void test_small_get_class_is_tight_for_every_size(void) {
  for (size_t n = 1; n <= SMALL_MAX_SIZE; n++) {
    int c = get_small_class(n);
    CHECK(c >= 0 && c < SMALL_NUM_CLASSES);
    if (c < 0 || c >= SMALL_NUM_CLASSES) continue;
    CHECK(get_small_class_size(c) >= n);
    CHECK(c == 0 || get_small_class_size(c - 1) < n);
  }
}

static inline void test_small_get_class_out_of_range(void) {
  CHECK(get_small_class(0)                  == -1);
  CHECK(get_small_class(SMALL_MAX_SIZE + 1) == -1);
  CHECK(get_small_class(1 << 20)            == -1);
  CHECK(get_small_class(SIZE_MAX)           == -1);
}

static inline void test_small_init_heap(void) {
  small_heap_t h;
  memset(&h, 0xFF, sizeof h);
  small_init_heap(&h, (void*)0x1234);
  for (int i = 0; i < SMALL_NUM_CLASSES; i++) CHECK(h.bins[i].head == NULL);
  CHECK(h.cur == NULL);
  CHECK(h.left == 0);
  CHECK(h.owner == (void*)0x1234);
 
  small_init_heap(NULL, NULL);
}




static inline void test_small_bins_add_pop(void) {
  init();
  for (int i = 0; i < 3; i++) make_fake(i, 32, false);

  CHECK(small_add_free_block(&H, fake(0), 0));
  CHECK(small_add_free_block(&H, fake(1), 0));
  CHECK(small_add_free_block(&H, fake(2), 0));

  CHECK(H.bins[0].head == fake(2));
  CHECK(fake(2)->next == fake(1));
  CHECK(fake(1)->next == fake(0));
  CHECK(fake(0)->next == NULL);

  CHECK(small_pop_free(&H, 0) == fake(2));
  CHECK(H.bins[0].head == fake(1));
  CHECK(small_pop_free(&H, 0) == fake(1));
  CHECK(small_pop_free(&H, 0) == fake(0));
  CHECK(H.bins[0].head == NULL);
  CHECK(small_pop_free(&H, 0) == NULL);
}

static inline void test_small_bins_are_independent(void) {
  init();
  make_fake(0, 32, false);
  make_fake(1, 48, false);
  CHECK(small_add_free_block(&H, fake(0), 0));
  CHECK(small_add_free_block(&H, fake(1), 1));
  CHECK(H.bins[0].head == fake(0));
  CHECK(H.bins[1].head == fake(1));
  CHECK(small_pop_free(&H, 0) == fake(0));
  CHECK(H.bins[0].head == NULL);
  CHECK(H.bins[1].head == fake(1));
}

static inline void test_small_pop_free(void) {
  init();
  CHECK(small_pop_free(&H, 0)                     == NULL);
  CHECK(small_pop_free(&H, SMALL_NUM_CLASSES - 1) == NULL);
 
  make_fake(0, 48, false);
  make_fake(1, 48, false);
  small_add_free_block(&H, fake(0), 1);
  small_add_free_block(&H, fake(1), 1);
 
  CHECK(small_pop_free(&H, 1) == fake(1));
  CHECK(H.bins[1].head == fake(0));
  CHECK(small_pop_free(&H, 1) == fake(0));
  CHECK(H.bins[1].head == NULL);
  CHECK(small_pop_free(&H, 1) == NULL);
}


static inline void test_small_malloc_first_block_layout(void) {
  init();
  void* p = small_malloc(&H, 32);
  CHECK(p != NULL);
  if (!p) return;
 
  CHECK((uintptr_t)p % 16 == 0);
  block_t* b = ptr_to_block(p);
  CHECK(block_size(b) == 32);
  CHECK(block_is_allocated(b));
  CHECK(!block_prev_is_free(b));
 
  char* base = (char*)chunk_of(p, SMALL_CHUNK_SIZE);
  CHECK((uintptr_t)base % SMALL_CHUNK_SIZE == 0);
  CHECK(chunk_of(p, SMALL_CHUNK_SIZE)->owner == &H);
  CHECK((char*)p - base == 16);
  CHECK(H.left == SMALL_CHUNK_SIZE - SMALL_CHUNK_HDR - 32);
  CHECK((char*)H.cur == (char*)b + 32);
}

static inline void test_small_malloc_consecutive_blocks_are_adjacent(void) {
  init();
  void* a = small_malloc(&H, 32);
  void* b = small_malloc(&H, 48);
  void* c = small_malloc(&H, 64);
  CHECK(a && b && c);
  CHECK((char*)b == (char*)a + 32);
  CHECK((char*)c == (char*)b + 48);
}

static inline void test_small_malloc_every_class(void) {
  init();
  void* ptrs[SMALL_NUM_CLASSES];
  for (int c = 0; c < SMALL_NUM_CLASSES; c++) {
    size_t sz = k_small_sizes[c];
    ptrs[c]   = small_malloc(&H, sz);
    CHECK(ptrs[c] != NULL);
    if (!ptrs[c]) continue;
    CHECK((uintptr_t)ptrs[c] % 16 == 0);
    CHECK(block_size(ptr_to_block(ptrs[c])) == sz);
    fill_pattern(ptrs[c], sz - HDR_SIZE, (uint32_t)c + 1);
  }
  for (int c = 0; c < SMALL_NUM_CLASSES; c++)
    if (ptrs[c]) CHECK(check_pattern(ptrs[c], k_small_sizes[c] - HDR_SIZE, (uint32_t)c + 1));
}

static inline void test_small_free_then_malloc_reuses_block(void) {
  init();
  void* p = small_malloc(&H, 64);
  CHECK(small_free(&H, p));
  CHECK(!block_is_allocated(ptr_to_block(p)));
  CHECK(block_size(ptr_to_block(p)) == 64);
  CHECK(H.bins[get_small_class(64)].head == ptr_to_block(p));
 
  void* q = small_malloc(&H, 64);
  CHECK(q == p);
  CHECK(block_is_allocated(ptr_to_block(q)));
}

static inline void test_small_free_is_lifo_and_class_exact(void) {
  init();
  void* a = small_malloc(&H, 64);
  void* b = small_malloc(&H, 64);
  void* c = small_malloc(&H, 64);
  CHECK(small_free(&H, a) && small_free(&H, b) && small_free(&H, c));
 
  void* other = small_malloc(&H, 80);
  CHECK(other != a && other != b && other != c);
 
  CHECK(small_malloc(&H, 64) == c);
  CHECK(small_malloc(&H, 64) == b);
  CHECK(small_malloc(&H, 64) == a);
}

static inline void test_small_free_rejects_misaligned(void) {
  init();
  void* p = small_malloc(&H, 64);
  CHECK(!small_free(&H, (char*)p + 8));
  CHECK(!small_free(&H, (char*)p + 1));
  CHECK(block_is_allocated(ptr_to_block(p)));
}

static inline void test_small_double_free_is_rejected(void) {
  init();
  void* p = small_malloc(&H, 64);
  CHECK(small_free(&H, p));
  CHECK(!small_free(&H, p));
 
  void* a = small_malloc(&H, 64);
  void* b = small_malloc(&H, 64);
  CHECK(a != b);
}

static inline void test_small_free_rejects_bad_header_size(void) {
  init();
  make_fake(0, 272, true);
  CHECK(!small_free(&H, block_to_ptr(fake(0))));
  make_fake(1, 0, true);
  CHECK(!small_free(&H, block_to_ptr(fake(1))));
  make_fake(2, 8192, true);
  CHECK(!small_free(&H, block_to_ptr(fake(2))));
  for (int i = 0; i < SMALL_NUM_CLASSES; i++) CHECK(H.bins[i].head == NULL);
}

static inline void test_small_chunk_refill_across_many_chunks(void) {
  init();
  enum { N = 6000 };
  static void* ptrs[N];
  uintptr_t first_chunk = 0;
  size_t    chunks      = 0;
  uintptr_t last        = 0;
 
  for (int i = 0; i < N; i++) {
    ptrs[i] = small_malloc(&H, 64);
    CHECK(ptrs[i] != NULL);
    if (!ptrs[i]) return;
    fill_pattern(ptrs[i], 56, (uint32_t)i);
    uintptr_t ch = (uintptr_t)chunk_of(ptrs[i], SMALL_CHUNK_SIZE);
    if (!first_chunk) first_chunk = ch;
    if (ch != last) { chunks++; last = ch; }
    CHECK(chunk_of(ptrs[i], SMALL_CHUNK_SIZE)->owner == &H);
  }
  
  size_t blk       = block_size(ptr_to_block(ptrs[0]));
  size_t per_chunk = (SMALL_CHUNK_SIZE - SMALL_CHUNK_HDR) / blk;
  size_t expected  = (N + per_chunk - 1) / per_chunk;
  CHECK(chunks + 1 >= expected && chunks <= expected + 1);
 
  for (int i = 0; i < N; i++)
    CHECK(check_pattern(ptrs[i], 56, (uint32_t)i));
}

static inline void test_small_refill_carves_leftover_tail_into_free_blocks(void) {
  init();
  /* 4096-byte blocks that fit whole in a chunk; the tail is left at 4088 */
  enum { NFULL = (SMALL_CHUNK_SIZE - SMALL_CHUNK_HDR) / 4096 };
  void* first[NFULL];
  for (int i = 0; i < NFULL; i++) {
    first[i] = small_malloc(&H, 4096);
    CHECK(first[i] != NULL);
  }
  uintptr_t chunk1 = (uintptr_t)chunk_of(first[0], SMALL_CHUNK_SIZE);
  CHECK(H.left == 4088);
 
  void* sixteenth = small_malloc(&H, 4096);
  CHECK(sixteenth != NULL);
  CHECK((uintptr_t)chunk_of(sixteenth, SMALL_CHUNK_SIZE) != chunk1);
 
  int c3840 = get_small_class(3840), c240 = get_small_class(240);
  CHECK(H.bins[c3840].head != NULL);
  CHECK(H.bins[c240].head != NULL);
 
  void* t = small_malloc(&H, 3840);
  void* u = small_malloc(&H, 240);
  CHECK((uintptr_t)chunk_of(t, SMALL_CHUNK_SIZE) == chunk1);
  CHECK((uintptr_t)chunk_of(u, SMALL_CHUNK_SIZE) == chunk1);
  CHECK(block_size(ptr_to_block(t)) == 3840);
  CHECK(block_size(ptr_to_block(u)) == 240);
  CHECK((char*)u == (char*)t + 3840);
 
  void* all[NFULL + 3];
  memcpy(all, first, sizeof first);
  all[NFULL] = sixteenth; all[NFULL + 1] = t; all[NFULL + 2] = u;
}

static inline void test_small_try_resize_in_place(void) {
  init();
  void* p = small_malloc(&H, 112);
  size_t hdr_before = ptr_to_block(p)->hdr;
 
  CHECK(small_try_resize_in_place(p, 112));
  CHECK(small_try_resize_in_place(p, 97));
  CHECK(!small_try_resize_in_place(p, 96));
  CHECK(!small_try_resize_in_place(p, 113));
  CHECK(!small_try_resize_in_place(p, 4096));
  CHECK(ptr_to_block(p)->hdr == hdr_before);
}

static inline void test_small_try_resize_invalid_arguments(void) {
  init();
  void* p = small_malloc(&H, 64);
  CHECK(!small_try_resize_in_place(NULL, 64));
  CHECK(!small_try_resize_in_place(p, 0));
  CHECK(!small_try_resize_in_place(p, 31));
  CHECK(!small_try_resize_in_place(p, SMALL_MAX_SIZE + 1));
  CHECK(!small_try_resize_in_place((char*)p + 8, 64));
  small_free(&H, p);
  CHECK(!small_try_resize_in_place(p, 64));
}

static inline void test_small_two_heaps_are_independent(void) {
  os_init();
  small_heap_t a, b;
  small_init_heap(&a, &a);
  small_init_heap(&b, &b);
  void* pa = small_malloc(&a, 64);
  void* pb = small_malloc(&b, 64);
  CHECK(pa && pb);
  CHECK(chunk_of(pa, SMALL_CHUNK_SIZE)->owner == &a);
  CHECK(chunk_of(pb, SMALL_CHUNK_SIZE)->owner == &b);
  CHECK(chunk_of(pa, SMALL_CHUNK_SIZE) != chunk_of(pb, SMALL_CHUNK_SIZE));
  small_free(&a, pa);
  CHECK(b.bins[get_small_class(64)].head == NULL);
  CHECK(small_malloc(&b, 64) != pa);
}

static inline void test_small_out_of_memory(void) {
  SKIP_IF_NO_ADDRESS_LIMIT();
  
  init();
  void* warm = small_malloc(&H, 32);
  CHECK(warm != NULL);
 
  limit_address_space(1 << 20);
  enum { MAX = 200000 };
  static void* ptrs[MAX];
  int n = 0;
  while (n < MAX) {
    void* p = small_malloc(&H, 4096);
    if (!p) break;
    ptrs[n++] = p;
  }
  CHECK(n < MAX);
  CHECK(n > 0);
 
  CHECK(small_malloc(&H, 4096) == NULL);
 
  for (int i = 0; i < n; i++) CHECK(small_free(&H, ptrs[i]));
  void* again = small_malloc(&H, 4096);
  CHECK(again != NULL);
 
  unlimit_address_space();
  CHECK(small_malloc(&H, 3000) != NULL);
}

void run_all_small_test(void) {
  test_small_class_sizes_match_design();
  test_small_get_class_is_tight_for_every_size();
  test_small_get_class_out_of_range();
  test_small_init_heap();
  test_small_bins_add_pop();
  test_small_bins_are_independent();
  test_small_pop_free();
  test_small_malloc_first_block_layout();
  test_small_malloc_consecutive_blocks_are_adjacent();
  test_small_malloc_every_class();
  test_small_free_then_malloc_reuses_block();
  test_small_free_is_lifo_and_class_exact();
  test_small_free_rejects_misaligned();
  test_small_double_free_is_rejected();
  test_small_free_rejects_bad_header_size();
  test_small_chunk_refill_across_many_chunks();
  test_small_refill_carves_leftover_tail_into_free_blocks();
  test_small_try_resize_in_place();
  test_small_try_resize_invalid_arguments();
  test_small_two_heaps_are_independent();
  test_small_out_of_memory();
}
