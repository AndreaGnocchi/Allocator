#include "../src/medium.h"
#include "../src/os.h"
#include "test_main.h"
#include "test_util.h"
#include <errno.h>
#include <limits.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

#define WHOLE_CHUNK_BLOCK (MEDIUM_CHUNK_SIZE - MEDIUM_CHUNK_HDR - MEDIUM_CHUNK_SENTINEL)
 
static medium_heap_t H;
 
static inline void init(void) {
  os_init();
  medium_init_heap(&H, &H);
}

static inline void* mm(size_t block_sz) {
  return medium_malloc(&H, block_sz);
}

typedef enum { ST_ALLOC, ST_FREE, ST_LIMBO } st_t;

typedef struct { size_t size; st_t st; } spec_t;

static const uint32_t medium_class_size[MEDIUM_NUM_CLASSES] = {
  // 4KB to 8KB (step 512)
  4608, 5120, 5632, 6144, 6656, 7168, 7680, 8192,
  // 8KB to 16KB (step 1024)
  9216, 10240, 11264, 12288, 13312, 14336, 15360, 16384,
  // 16KB to 32KB (step 2048)
  18432, 20480, 22528, 24576, 26624, 28672, 30720, 32768,
  // 32KB to 64KB (step 4096)
  36864, 40960, 45056, 49152, 53248, 57344, 61440, 65536,
  // 64KB to 128KB (step 8192)
  73728, 81920, 90112, 98304, 106496, 114688, 122880, 131072
};

static const uint8_t medium_class_of[248] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 16,
  16, 17, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22,
  22, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 25, 26, 26, 26,
  26, 26, 26, 26, 26, 27, 27, 27, 27, 27, 27, 27, 27, 28, 28, 28, 28, 28, 28, 28, 28, 29, 29, 29,
  29, 29, 29, 29, 29, 30, 30, 30, 30, 30, 30, 30, 30, 31, 31, 31, 31, 31, 31, 31, 31, 32, 32, 32,
  32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33,
  33, 33, 33, 33, 33, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 35, 35, 35,
  35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
  36, 36, 36, 36, 36, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 38, 38, 38,
  38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39,
  39, 39, 39, 39, 39
};

static inline int medium_floor_class(size_t sz) {
  if (sz >= MEDIUM_MAX_SIZE) return MEDIUM_LAST_CLASS; // catch-all bin
  int c = medium_class_of[((sz + 511) >> 9) - 9];
  if (medium_class_size[c] != sz) c--;
  return c;
}

static char* arena_build(const spec_t* sp, size_t n, block_t** out) {
  size_t sum = 0;
  for (size_t i = 0; i < n; i++) sum += sp[i].size;
 
  char* buf = NULL;
  if (posix_memalign((void**)&buf, 64, sum + 64) != 0) abort();
  memset(buf, 0, sum + 64);
 
  char* p         = buf + 8;
  bool  prev_free = false;
  for (size_t i = 0; i < n; i++) {
    block_t* b = (block_t*)p;
    out[i]     = b;
    block_set_hdr(b, sp[i].size, sp[i].st == ST_ALLOC, prev_free);
    if (sp[i].st == ST_FREE) {
      block_write_ftr(b);
      medium_add_free_block(&H, b, medium_floor_class(sp[i].size));
    }
    prev_free = (sp[i].st == ST_FREE);
    p += sp[i].size;
  }
  block_set_hdr((block_t*)p, 0, true, prev_free);
  return buf;
}


static inline int bins_nonempty(void) {
  int n = 0;
  for (int c = 0; c < MEDIUM_NUM_CLASSES; c++)
    n += H.bins[c].head != NULL;
  return n;
}

static inline void test_medium_class_sizes_follow_design(void) {
  int idx = 0;
  for (int p = 12; p <= 16; p++) {
    size_t base = (size_t)1 << p, step = base / 8;
    for (int k = 1; k <= 8; k++, idx++)
      CHECK(get_medium_class_size(idx) == base + k * step);
  }
  CHECK(idx == MEDIUM_NUM_CLASSES);
  CHECK(get_medium_class_size(0)                  == 4608);
  CHECK(get_medium_class_size(MEDIUM_LAST_CLASS)  == MEDIUM_MAX_SIZE);
  CHECK(get_medium_class_size(-1)                 == 0);
  CHECK(get_medium_class_size(MEDIUM_NUM_CLASSES) == 0);
  CHECK(get_medium_class_size(INT_MIN)            == 0);
  CHECK(get_medium_class_size(INT_MAX)            == 0);
}

static inline void test_medium_get_class_is_tight_for_every_size(void) {
  for (size_t n = MEDIUM_MIN_SIZE + 1; n <= MEDIUM_MAX_SIZE; n++) {
    int c = get_medium_class(n);
    if (!(c >= 0 && c < MEDIUM_NUM_CLASSES)) continue;
    
    size_t sz = get_medium_class_size(c);
    CHECK(sz >= n);
    CHECK(c == 0 || get_medium_class_size(c - 1) < n);
  }
}

static inline void test_medium_get_class_out_of_range(void) {
  CHECK(get_medium_class(0)                   == -1);
  CHECK(get_medium_class(1)                   == -1);
  CHECK(get_medium_class(MEDIUM_MIN_SIZE)     == -1);
  CHECK(get_medium_class(MEDIUM_MAX_SIZE + 1) == -1);
  CHECK(get_medium_class(SIZE_MAX)            == -1);
  CHECK(get_medium_class(MEDIUM_MIN_SIZE + 1) == 0);
  CHECK(get_medium_class(MEDIUM_MAX_SIZE)     == MEDIUM_LAST_CLASS);
}

static inline void test_medium_init_heap(void) {
  medium_heap_t h;
  memset(&h, 0xFF, sizeof h);
  medium_init_heap(&h, (void*)0x42);
  for (int i = 0; i < MEDIUM_NUM_CLASSES; i++) CHECK(h.bins[i].head == NULL);
  CHECK(h.bitmap == 0);
  CHECK(h.owner == (void*)0x42);
  medium_init_heap(NULL, NULL);
}

static alignas(16) unsigned char g_arena[8192];

static inline block_t* fake(int i) {
  return (block_t*)(g_arena + 8 + 64 * i);
}

static inline void make_fake(int i, size_t size, bool allocated) {
  memset(fake(i), 0, sizeof(block_t));
  block_set_hdr(fake(i), size, allocated, false);
}

static void test_medium_bins_add_remove_and_bitmap(void) {
  init();
  for (int i = 0; i < 3; i++) make_fake(i, 5120, false);
 
  CHECK(medium_add_free_block(&H, fake(0), 1));
  CHECK(H.bitmap == 1u << 1);
  CHECK(medium_add_free_block(&H, fake(1), 1));
  CHECK(medium_add_free_block(&H, fake(2), 1));
  CHECK(H.bins[1].head == fake(2));
  CHECK(fake(2)->prev == NULL    && fake(2)->next == fake(1));
  CHECK(fake(1)->prev == fake(2) && fake(1)->next == fake(0));
 
  CHECK(medium_remove_free_block(&H, fake(1), 1));
  CHECK(fake(2)->next == fake(0) && fake(0)->prev == fake(2));
  CHECK(H.bitmap == 1u << 1);
  CHECK(!medium_remove_free_block(&H, fake(1), 1));
 
  CHECK(medium_remove_free_block(&H, fake(2), 1));
  CHECK(H.bins[1].head == fake(0));
  CHECK(H.bitmap == 1u << 1);
 
  CHECK(medium_remove_free_block(&H, fake(0), 1));
  CHECK(H.bins[1].head == NULL);
  CHECK(H.bitmap == 0);
}

static inline void test_medium_bins_bitmap_per_class(void) {
  init();
  make_fake(0, 5120, false);
  make_fake(1, 6144, false);
  CHECK(medium_add_free_block(&H, fake(0), 1));
  CHECK(medium_add_free_block(&H, fake(1), MEDIUM_LAST_CLASS));
  CHECK(H.bitmap == ((1ull << 1) | (1ull << MEDIUM_LAST_CLASS)));
  CHECK(medium_remove_free_block(&H, fake(1), MEDIUM_LAST_CLASS));
  CHECK(H.bitmap == 1ull << 1);
}

static inline void test_medium_bins_reject_invalid_class(void) {
  init();
  make_fake(0, 5120, false);
  CHECK(!medium_add_free_block(&H, fake(0), -1));
  CHECK(!medium_remove_free_block(&H, fake(0), -1));
  CHECK(H.bitmap == 0);
}

static inline void test_medium_bins_detect_corruption(void) {
  init();
  for (int i = 0; i < 3; i++) make_fake(i, 5120, false);
  CHECK(medium_add_free_block(&H, fake(0), 1));
  fake(0)->prev = fake(1);
  CHECK(!medium_add_free_block(&H, fake(2), 1));
  CHECK(H.bins[1].head == fake(0));
 
  fake(0)->prev = NULL;
  CHECK(medium_add_free_block(&H, fake(2), 1));
  fake(0)->prev = fake(1);
  CHECK(!medium_remove_free_block(&H, fake(0), 1));
}

static void test_medium_pop_fit_picks_smallest_sufficient_bin(void) {
  init();
  CHECK(medium_pop_fit(&H, 0) == NULL);
 
  make_fake(0, get_medium_class_size(1), false);
  make_fake(1, get_medium_class_size(10), false);
  medium_add_free_block(&H, fake(0), 1);
  medium_add_free_block(&H, fake(1), 10);
 
  CHECK(medium_pop_fit(&H, 11) == NULL);
  CHECK(medium_pop_fit(&H, 0)  == fake(0));
  CHECK(H.bitmap == 1u << 10);
  CHECK(medium_pop_fit(&H, 2) == fake(1));
  CHECK(H.bitmap == 0);
  CHECK(medium_pop_fit(&H, 0) == NULL);
}

static inline void test_medium_pop_fit_exact_class(void) {
  init();
  make_fake(0, get_medium_class_size(5), false);
  medium_add_free_block(&H, fake(0), 5);
  CHECK(medium_pop_fit(&H, 5) == fake(0));
}

static inline void test_medium_pop_fit_rejects_bad_blocks(void) {
  init();
  make_fake(0, 4608, false);
  medium_add_free_block(&H, fake(0), 1);
  CHECK(medium_pop_fit(&H, 1) == NULL);
  CHECK(H.bins[1].head == fake(0));
  medium_remove_free_block(&H, fake(0), 1);
 
  make_fake(1, 5120, true);
  medium_add_free_block(&H, fake(1), 1);
  CHECK(medium_pop_fit(&H, 1) == NULL);
}

static inline void test_medium_pop_fit_last_bin_never_returns_unsplittable_oversized_block(void) {
  init();
  make_fake(0, MEDIUM_MAX_SIZE + 512, false);
  medium_add_free_block(&H, fake(0), MEDIUM_LAST_CLASS);
  CHECK(medium_pop_fit(&H, MEDIUM_LAST_CLASS) == NULL);
}

static inline void test_medium_pop_fit_last_bin_skips_to_usable_block(void) {
  init();
  make_fake(1, MEDIUM_MAX_SIZE, false);
  make_fake(2, MEDIUM_MAX_SIZE + 4608, false);
  make_fake(0, MEDIUM_MAX_SIZE + 512, false);
  medium_add_free_block(&H, fake(1), MEDIUM_LAST_CLASS);
  medium_add_free_block(&H, fake(2), MEDIUM_LAST_CLASS);
  medium_add_free_block(&H, fake(0), MEDIUM_LAST_CLASS);
 
  block_t* a = medium_pop_fit(&H, MEDIUM_LAST_CLASS);
  CHECK(a == fake(2) || a == fake(1));
  block_t* b = medium_pop_fit(&H, MEDIUM_LAST_CLASS);
  CHECK(b == fake(2) || b == fake(1));
  CHECK(a != b);
  CHECK(medium_pop_fit(&H, MEDIUM_LAST_CLASS) == NULL);
  CHECK(H.bins[MEDIUM_LAST_CLASS].head == fake(0));
  CHECK(H.bitmap & (1ull << MEDIUM_LAST_CLASS));
}

static inline void test_medium_pop_fit_smaller_request_can_use_last_bin(void) {
  init();
  make_fake(0, MEDIUM_MAX_SIZE + 512, false);
  medium_add_free_block(&H, fake(0), MEDIUM_LAST_CLASS);
  CHECK(medium_pop_fit(&H, 10) == fake(0));
}

static inline void test_medium_coalesce_no_free_neighbours(void) {
  init();
  spec_t sp[] = {{4608, ST_ALLOC}, {5120, ST_LIMBO}, {4608, ST_ALLOC}};
  block_t* b[3];
  char* buf = arena_build(sp, 3, b);
 
  block_t* r = medium_coalesce(&H, b[1]);
  CHECK(r == b[1]);
  CHECK(block_size(r) == 5120);
  CHECK(!block_is_allocated(r));
  CHECK(*block_ftr(r) == 5120);
  CHECK(block_prev_is_free(b[2]));
  CHECK(H.bitmap == 0);
  free(buf);
}

static inline void test_medium_coalesce_with_next(void) {
  init();
  spec_t sp[] = {{4608, ST_ALLOC}, {5120, ST_LIMBO}, {6144, ST_FREE}, {4608, ST_ALLOC}};
  block_t* b[4];
  char* buf = arena_build(sp, 4, b);
 
  block_t* r = medium_coalesce(&H, b[1]);
  CHECK(r == b[1]);
  CHECK(block_size(r) == 5120 + 6144);
  CHECK(!block_is_allocated(r));
  CHECK(*block_ftr(r) == 5120 + 6144);
  CHECK(block_prev_is_free(b[3]));
  CHECK(H.bitmap == 0);
  free(buf);
}

static inline void test_medium_coalesce_with_prev(void) {
  init();
  spec_t sp[] = {{6144, ST_FREE}, {5120, ST_LIMBO}, {4608, ST_ALLOC}};
  block_t* b[3];
  char* buf = arena_build(sp, 3, b);
 
  block_t* r = medium_coalesce(&H, b[1]);
  CHECK(r == b[0]);
  CHECK(block_size(r) == 6144 + 5120);
  CHECK(!block_is_allocated(r));
  CHECK(!block_prev_is_free(r));
  CHECK(*block_ftr(r) == 6144 + 5120);
  CHECK(block_prev_is_free(b[2]));
  CHECK(H.bitmap == 0);
  free(buf);
}

static inline void test_medium_coalesce_with_both_neighbours(void) {
  init();
  spec_t sp[] = {{4608, ST_ALLOC}, {5120, ST_FREE}, {4608, ST_LIMBO}, {8192, ST_FREE}, {4608, ST_ALLOC}};
  block_t* b[5];
  char* buf = arena_build(sp, 5, b);
 
  block_t* r = medium_coalesce(&H, b[2]);
  CHECK(r == b[1]);
  CHECK(block_size(r) == 5120 + 4608 + 8192);
  CHECK(!block_is_allocated(r));
  CHECK(!block_prev_is_free(r));
  CHECK(*block_ftr(r) == 5120 + 4608 + 8192);
  CHECK(block_prev_is_free(b[4]));
  CHECK(H.bitmap == 0);
  free(buf);
}

static inline void test_medium_coalesce_with_sentinel_after(void) {
  init();
  spec_t sp[] = {{4608, ST_ALLOC}, {5120, ST_LIMBO}};
  block_t* b[2];
  char* buf = arena_build(sp, 2, b);
  block_t* r = medium_coalesce(&H, b[1]);
  CHECK(r == b[1]);
  CHECK(block_size(r) == 5120);
  CHECK(block_prev_is_free(block_next(r)));
  free(buf);
}

static inline void test_medium_coalesce_reports_corrupt_free_list(void) {
  init();
  spec_t sp[] = {{4608, ST_ALLOC}, {5120, ST_LIMBO}, {6144, ST_FREE}, {4608, ST_ALLOC}};
  block_t* b[4];
  char* buf = arena_build(sp, 4, b);
  medium_remove_free_block(&H, b[2], medium_floor_class(6144));
  CHECK(medium_coalesce(&H, b[1]) == NULL);
  free(buf);
}

static inline void test_medium_split_creates_free_remainder(void) {
  init();
  spec_t sp[] = {{16384, ST_LIMBO}, {4608, ST_ALLOC}};
  block_t* b[2];
  char* buf = arena_build(sp, 2, b);
 
  CHECK(medium_split(&H, b[0], 5120) == SPLIT_SUCCESS);
  CHECK(block_size(b[0]) == 5120);
 
  block_t* r = block_next(b[0]);
  CHECK(block_size(r) == 16384 - 5120);
  CHECK(!block_is_allocated(r));
  CHECK(*block_ftr(r) == 16384 - 5120);
  CHECK(block_prev_is_free(b[1]));
  CHECK(H.bins[medium_floor_class(16384 - 5120)].head == r);
  CHECK(bins_nonempty() == 1);
  free(buf);
}

static inline void test_medium_split_remainder_too_small(void) {
  init();
  spec_t sp[] = {{16384, ST_LIMBO}, {4608, ST_ALLOC}};
  block_t* b[2];
  char* buf = arena_build(sp, 2, b);
 
  CHECK(medium_split(&H, b[0], 13312) == SPLIT_DONTNEED);
  CHECK(block_size(b[0]) == 16384);
  CHECK(H.bitmap == 0);
  free(buf);
}

static inline void test_medium_split_remainder_boundary(void) {
  init();
  spec_t sp[] = {{16384, ST_LIMBO}, {4608, ST_ALLOC}};
  block_t* b[2];
  char* buf = arena_build(sp, 2, b);
  CHECK(medium_split(&H, b[0], 16384 - 4608) == SPLIT_SUCCESS);
  CHECK(block_size(b[0])             == 16384 - 4608);
  CHECK(block_size(block_next(b[0])) == 4608);
  free(buf);
 
  init();
  buf = arena_build(sp, 2, b);
  CHECK(medium_split(&H, b[0], 16384 - 4592) == SPLIT_DONTNEED);
  CHECK(block_size(b[0]) == 16384);
  free(buf);
}

static inline void test_medium_split_exact_fit(void) {
  init();
  spec_t sp[] = {{16384, ST_LIMBO}, {4608, ST_ALLOC}};
  block_t* b[2];
  char* buf = arena_build(sp, 2, b);
  CHECK(medium_split(&H, b[0], 16384) == SPLIT_DONTNEED);
  CHECK(block_size(b[0]) == 16384);
  free(buf);
}

static inline void test_medium_split_merges_remainder_with_free_next(void) {
  init();
  spec_t sp[] = {{16384, ST_LIMBO}, {8192, ST_FREE}, {4608, ST_ALLOC}};
  block_t* b[3];
  char* buf = arena_build(sp, 3, b);
 
  CHECK(medium_split(&H, b[0], 5120) == SPLIT_SUCCESS);
  block_t* r = block_next(b[0]);
  CHECK(block_size(r) == (16384 - 5120) + 8192);
  CHECK(*block_ftr(r) == (16384 - 5120) + 8192);
  CHECK(block_prev_is_free(b[2]));
  CHECK(bins_nonempty() == 1);
  CHECK(H.bins[medium_floor_class(block_size(r))].head == r);
  free(buf);
}

static inline void test_medium_split_keeps_allocated_flag(void) {
  init();
  spec_t sp[] = {{16384, ST_ALLOC}, {4608, ST_ALLOC}};
  block_t* b[2];
  char* buf = arena_build(sp, 2, b);
  CHECK(medium_split(&H, b[0], 5120) == SPLIT_SUCCESS);
  CHECK(block_is_allocated(b[0]));
  CHECK(block_size(b[0]) == 5120);
  free(buf);
}

static inline void test_medium_free_invalid_pointers(void) {
  init();
  spec_t sp[] = {{5120, ST_ALLOC}, {64, ST_ALLOC}, {6144, ST_FREE}, {4608, ST_ALLOC}};
  block_t* b[4];
  char* buf = arena_build(sp, 4, b);
  uint64_t bitmap = H.bitmap;
 
  CHECK(!medium_free(&H, (char*)block_to_ptr(b[0]) + 8));
  CHECK(!medium_free(&H,        block_to_ptr(b[1])));
  CHECK(!medium_free(&H,        block_to_ptr(b[2])));
  CHECK(H.bitmap == bitmap);
  CHECK(block_is_allocated(b[0]));
  free(buf);
}

static inline void test_medium_free_coalesces_in_every_order(void) {
  const size_t sizes[4] = {5120, 6144, 4608, 8192};
  size_t total = 0;
  for (int i = 0; i < 4; i++) total += sizes[i];
  
  int perm[4];
  for (perm[0] = 0; perm[0] < 4; perm[0]++)
    for (perm[1] = 0; perm[1] < 4; perm[1]++)
      for (perm[2] = 0; perm[2] < 4; perm[2]++)
        for (perm[3] = 0; perm[3] < 4; perm[3]++) {
          if (perm[0] == perm[1] || perm[0] == perm[2] || perm[0] == perm[3] ||
              perm[1] == perm[2] || perm[1] == perm[3] || perm[2] == perm[3]) continue;
          
          init();
          spec_t sp[] = {{sizes[0], ST_ALLOC}, {sizes[1], ST_ALLOC}, {sizes[2], ST_ALLOC}, {sizes[3], ST_ALLOC}};
          block_t* b[4];
          char* buf = arena_build(sp, 4, b);

          for (int i = 0; i < 4; i++)
            CHECK(medium_free(&H, block_to_ptr(b[perm[i]])));
          
          CHECK(block_size(b[0]) == total && !block_is_allocated(b[0]));
          CHECK(*block_ftr(b[0]) == total);
          CHECK(bins_nonempty() == 1);
          CHECK(H.bins[medium_floor_class(total)].head == b[0] && b[0]->next == NULL);
          CHECK(block_prev_is_free(block_next(b[0])));
          free(buf);
        }
}

static inline void test_medium_malloc_rejects_bad_sizes(void) {
  init();
  CHECK(medium_malloc(NULL, 5000) == NULL);
  CHECK(mm(0)                   == NULL);
  CHECK(mm(MEDIUM_MIN_SIZE)     == NULL);
  CHECK(mm(32)                  == NULL);
  CHECK(mm(MEDIUM_MAX_SIZE + 1) == NULL);
  CHECK(mm(SIZE_MAX)            == NULL);
  CHECK(H.bitmap == 0);
}

static inline void test_medium_malloc_first_block_layout(void) {
  init();
  void* p = mm(8192);
  CHECK(p != NULL);
  if (!p) return;
 
  CHECK((uintptr_t)p % 16 == 0);
  block_t* b = ptr_to_block(p);
  CHECK(block_size(b) == 8192);
  CHECK(block_is_allocated(b));
  CHECK(!block_prev_is_free(b));
 
  char* base = (char*)chunk_of(p, MEDIUM_CHUNK_SIZE);
  CHECK((uintptr_t)base % MEDIUM_CHUNK_SIZE == 0);
  CHECK(chunk_of(p, MEDIUM_CHUNK_SIZE)->owner == &H);
  CHECK((char*)p - base == 16);
}

static inline void test_medium_malloc_consecutive_blocks_are_adjacent(void) {
  init();
  void* a = mm(5000);
  void* b = mm(9000);
  void* c = mm(20000);
  CHECK(a && b && c);
  CHECK((char*)b == (char*)a + block_size(ptr_to_block(a)));
  CHECK((char*)c == (char*)b + block_size(ptr_to_block(b)));
}

static inline void test_medium_malloc_every_class_exact_and_one_over(void) {
  init();
  for (int c = 0; c < MEDIUM_NUM_CLASSES; c++) {
    size_t sz = get_medium_class_size(c);
    void*  p  = mm(sz);
    CHECK(p != NULL);
    if (!p) continue;
    CHECK(block_size(ptr_to_block(p)) == sz);
    CHECK((uintptr_t)p % 16 == 0);
    fill_pattern(p, sz - HDR_SIZE, (uint32_t)c + 1);
    CHECK(check_pattern(p, sz - HDR_SIZE, (uint32_t)c + 1));
    CHECK(medium_free(&H, p));
 
    if (c + 1 < MEDIUM_NUM_CLASSES) {
      void* q = mm(sz + 1);
      CHECK(q && block_size(ptr_to_block(q)) == get_medium_class_size(c + 1));
      if (q) medium_free(&H, q);
    }
  }
}

static inline void test_medium_malloc_free_reuses_exact_block(void) {
  init();
  void* b = mm(8192);
  CHECK(medium_free(&H, b));
  void* d = mm(8192);
  CHECK(d == b);
}

static inline void test_medium_free_all_restores_whole_chunk(void) {
  for (int order = 0; order < 3; order++) {
    init();
    enum { N = 20 };
    void* p[N];
    for (int i = 0; i < N; i++) {
      p[i] = mm(4608 + 512 * (size_t)i);
      CHECK(p[i] != NULL);
    }
    uintptr_t base = (uintptr_t)chunk_of(p[0], MEDIUM_CHUNK_SIZE);
 
    for (int k = 0; k < N; k++) {
      int i = order == 0 ? k : order == 1 ? N - 1 - k : (k % 2 ? N - 1 - k / 2 : k / 2);
      CHECK(medium_free(&H, p[i]));
    }
 
    CHECK(H.bitmap == (1ull << MEDIUM_LAST_CLASS));
    block_t* whole = H.bins[MEDIUM_LAST_CLASS].head;
    CHECK(whole != NULL);
    if (whole) {
      CHECK((uintptr_t)whole  == base + MEDIUM_CHUNK_HDR);
      CHECK(block_size(whole) == WHOLE_CHUNK_BLOCK);
      CHECK(whole->next == NULL);
    }
  }
}

static inline void test_medium_malloc_chunk_exhaustion_and_remainder_reuse(void) {
  init();
  void* p[8];
  for (int i = 0; i < 8; i++) {
    p[i] = mm(MEDIUM_MAX_SIZE);
    CHECK(p[i] != NULL);
    if (p[i])
      CHECK(block_size(ptr_to_block(p[i])) == MEDIUM_MAX_SIZE);
  }
  uintptr_t chunk1 = (uintptr_t)chunk_of(p[0], MEDIUM_CHUNK_SIZE);
  for (int i = 0; i < 7; i++)
    CHECK((uintptr_t)chunk_of(p[i], MEDIUM_CHUNK_SIZE) == chunk1);
  CHECK((uintptr_t)chunk_of(p[7], MEDIUM_CHUNK_SIZE) != chunk1);
 
  void* q = mm(120000);
  CHECK(q != NULL);
  CHECK((uintptr_t)chunk_of(q, MEDIUM_CHUNK_SIZE) == chunk1);
 
  void* live[9];
  memcpy(live, p, sizeof p);
  live[8] = q;
}

static inline void test_medium_malloc_payload_integrity(void) {
  init();
  enum { N = 60 };
  void*  p[N];
  size_t sz[N];
  for (int i = 0; i < N; i++) {
    sz[i] = 4097 + (size_t)i * 2100;
    p[i]  = mm(sz[i]);
    CHECK(p[i] != NULL);
    if (p[i]) fill_pattern(p[i], sz[i] - HDR_SIZE, (uint32_t)i + 1);
  }
  for (int i = 0; i < N; i++)
    if (p[i])
      CHECK(check_pattern(p[i], sz[i] - HDR_SIZE, (uint32_t)i + 1));
}

static inline void test_medium_out_of_memory(void) {
  SKIP_IF_NO_ADDRESS_LIMIT();
  init();
  void* warm = mm(8192);
  CHECK(warm != NULL);
 
  limit_address_space(4 << 20);
  enum { MAX = 100 };
  void* p[MAX];
  int   n = 0;
  while (n < MAX) {
    void* q = mm(MEDIUM_MAX_SIZE);
    if (!q) break;
    p[n++] = q;
  }
  CHECK(n < MAX);
  CHECK(mm(MEDIUM_MAX_SIZE) == NULL);
 
  for (int i = 0; i < n; i++)
    CHECK(medium_free(&H, p[i]));
  CHECK(mm(MEDIUM_MAX_SIZE) != NULL);
 
  unlimit_address_space();
  CHECK(mm(10000) != NULL);
}

static inline void test_medium_resize_invalid_arguments(void) {
  init();
  void* p = mm(8192);
  CHECK(!medium_try_resize_in_place(NULL, p, 8192));
  CHECK(!medium_try_resize_in_place(&H, NULL, 8192));
  CHECK(!medium_try_resize_in_place(&H, p, 0));
  CHECK(!medium_try_resize_in_place(&H, p, MEDIUM_MIN_SIZE));
  CHECK(!medium_try_resize_in_place(&H, p, MEDIUM_MAX_SIZE + 1));
  CHECK(!medium_try_resize_in_place(&H, (char*)p + 8, 8192));
  CHECK(block_size(ptr_to_block(p)) == 8192);
 
  CHECK(medium_free(&H, p));
  CHECK(!medium_try_resize_in_place(&H, p, 8192));
}

static inline void test_medium_resize_same_class_is_noop(void) {
  init();
  void* p = mm(8192);
  CHECK(medium_try_resize_in_place(&H, p, 8192));
  CHECK(medium_try_resize_in_place(&H, p, 7700));
  CHECK(block_size(ptr_to_block(p)) == 8192);
}

static inline void test_medium_resize_shrink_returns_tail_to_free_list(void) {
  init();
  void* p = mm(40960);
  void* q = mm(5120);
  fill_pattern(p, 20000, 7);
 
  CHECK(medium_try_resize_in_place(&H, p, 20480));
  CHECK(block_size(ptr_to_block(p)) == 20480);
  CHECK(check_pattern(p, 20000, 7));
  CHECK(block_prev_is_free(ptr_to_block(q)));
 
  void* tail = mm(20480);
  CHECK((char*)tail == (char*)p + 20480);
}

static inline void test_medium_resize_grow_into_free_neighbour_exact(void) {
  init();
  void* p = mm(8192);
  void* q = mm(5120);
  void* r = mm(4608);
  fill_pattern(p, 8192 - HDR_SIZE, 3);
  CHECK(medium_free(&H, q));
 
  CHECK(medium_try_resize_in_place(&H, p, 13312));
  CHECK(block_size(ptr_to_block(p)) == 13312);
  CHECK(check_pattern(p, 8192 - HDR_SIZE, 3));
  CHECK(!block_prev_is_free(ptr_to_block(r)));
  CHECK(bins_nonempty() == 1);
}

static inline void test_medium_resize_grow_into_free_neighbour_with_remainder(void) {
  init();
  void* p = mm(8192);
  void* q = mm(20480);
  CHECK(medium_free(&H, q));
 
  CHECK(medium_try_resize_in_place(&H, p, 12000));
  CHECK(block_size(ptr_to_block(p)) == 12288);
  void* tail = mm(16384);
  CHECK((char*)tail == (char*)p + 12288);
}


static inline void test_medium_resize_grow_fails_without_enough_room(void) {
  init();
  void* p = mm(8192);
  void* q = mm(5120);
  void* r = mm(4608);
  (void)r;
  CHECK(medium_free(&H, q));
 
  CHECK(!medium_try_resize_in_place(&H, p, 20000));
  CHECK(block_size(ptr_to_block(p)) == 8192);
}
 
static inline void test_medium_resize_grow_fails_when_next_is_allocated(void) {
  init();
  void* p = mm(8192);
  void* q = mm(8192);
  (void)q;
  CHECK(!medium_try_resize_in_place(&H, p, 20000));
  CHECK(block_size(ptr_to_block(p)) == 8192);
}
 
static inline void test_medium_resize_grow_into_chunk_remainder(void) {
  init();
  void* p = mm(8192);
  fill_pattern(p, 8192 - HDR_SIZE, 9);
 
  CHECK(medium_try_resize_in_place(&H, p, 50000));
  CHECK(block_size(ptr_to_block(p)) == get_medium_class_size(get_medium_class(50000)));
  CHECK(check_pattern(p, 8192 - HDR_SIZE, 9));
 
  CHECK(medium_try_resize_in_place(&H, p, MEDIUM_MAX_SIZE));
  CHECK(block_size(ptr_to_block(p)) == MEDIUM_MAX_SIZE);
 
  CHECK(medium_free(&H, p));
  CHECK(H.bitmap == 1ull << MEDIUM_LAST_CLASS);
}

void run_all_medium_test(void) {
  test_medium_class_sizes_follow_design();
  test_medium_get_class_is_tight_for_every_size();
  test_medium_get_class_out_of_range();
  test_medium_init_heap();
  test_medium_bins_add_remove_and_bitmap();
  test_medium_bins_bitmap_per_class();
  test_medium_bins_reject_invalid_class();
  test_medium_bins_detect_corruption();
  test_medium_pop_fit_picks_smallest_sufficient_bin();
  test_medium_pop_fit_exact_class();
  test_medium_pop_fit_rejects_bad_blocks();
  test_medium_pop_fit_last_bin_never_returns_unsplittable_oversized_block();
  test_medium_pop_fit_last_bin_skips_to_usable_block();
  test_medium_pop_fit_smaller_request_can_use_last_bin();
  test_medium_coalesce_no_free_neighbours();
  test_medium_coalesce_with_next();
  test_medium_coalesce_with_prev();
  test_medium_coalesce_with_both_neighbours();
  test_medium_coalesce_with_sentinel_after();
  test_medium_coalesce_reports_corrupt_free_list();
  test_medium_split_creates_free_remainder();
  test_medium_split_remainder_too_small();
  test_medium_split_remainder_boundary();
  test_medium_split_exact_fit();
  test_medium_split_merges_remainder_with_free_next();
  test_medium_split_keeps_allocated_flag();
  test_medium_free_invalid_pointers();
  test_medium_free_coalesces_in_every_order();
  test_medium_malloc_rejects_bad_sizes();
  test_medium_malloc_first_block_layout();
  test_medium_malloc_consecutive_blocks_are_adjacent();
  test_medium_malloc_every_class_exact_and_one_over();
  test_medium_malloc_free_reuses_exact_block();
  test_medium_free_all_restores_whole_chunk();
  test_medium_malloc_chunk_exhaustion_and_remainder_reuse();
  test_medium_malloc_payload_integrity();
  test_medium_out_of_memory();
  test_medium_resize_invalid_arguments();
  test_medium_resize_same_class_is_noop();
  test_medium_resize_shrink_returns_tail_to_free_list();
  test_medium_resize_grow_into_free_neighbour_exact();
  test_medium_resize_grow_into_free_neighbour_with_remainder();
  test_medium_resize_grow_fails_without_enough_room();
  test_medium_resize_grow_fails_when_next_is_allocated();
  test_medium_resize_grow_into_chunk_remainder();
}
