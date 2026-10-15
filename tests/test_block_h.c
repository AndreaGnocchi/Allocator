#include "../src/block.h"
#include "test_main.h"
#include <stdalign.h>
#include <string.h>

static inline void test_request_to_block_size(void) {
  CHECK(request_to_block_size(1)             == 32);
  CHECK(request_to_block_size(24)            == 32);
  CHECK(request_to_block_size(25)            == 48);
  CHECK(request_to_block_size(4088)          == 4096);
  CHECK(request_to_block_size(4089)          == 4112);
  CHECK(request_to_block_size(131064)        == 131072);
  CHECK(request_to_block_size(131065)        == 131088);
  CHECK(request_to_block_size(SIZE_MAX - 23) == SIZE_MAX - 15);
  
  for (int i = 22; i >= 0; --i) {
    CHECK(request_to_block_size(SIZE_MAX - i) == 0); 
  }
}

static inline void test_block_request_to_block_size_properties(void) {
  size_t prev = 0;
  
  for (size_t n = 0; n <= 300000; n++) {
    size_t sz = request_to_block_size(n);
    CHECK(sz % ALIGNMENT == 0);
    CHECK(sz >= MIN_BLOCK_SIZE);
    CHECK(sz >= n + HDR_SIZE);
    CHECK(sz == MIN_BLOCK_SIZE || sz < n + HDR_SIZE + ALIGNMENT);
    CHECK(sz >= prev);
    prev = sz;
  }

}

static inline void test_block_constants(void) {
  CHECK(ALIGNMENT ==  16);
  CHECK((ALIGNMENT & (ALIGNMENT - 1)) == 0);
  CHECK(HDR_SIZE % ALIGNMENT == 8); 
  CHECK(MIN_BLOCK_SIZE >= HDR_SIZE + 2 * sizeof(void*) + FTR_SIZE);
  CHECK(MIN_BLOCK_SIZE % ALIGNMENT ==  0);
  CHECK(sizeof(block_t) <= MIN_BLOCK_SIZE);
  CHECK((BLOCK_FLAGS_MASK & BLOCK_SIZE_MASK) == 0);
  CHECK((BLOCK_FLAG_PREV_FREE & BLOCK_FLAG_ALLOCATED) == 0);
  CHECK((BLOCK_FLAG_PREV_FREE | BLOCK_FLAG_ALLOCATED) <= BLOCK_FLAGS_MASK);
  CHECK(ALIGNMENT - 1 <= BLOCK_FLAGS_MASK);
}

static inline void test_block_align_up(void) {
  CHECK(align_up(0)             == 0);
  CHECK(align_up(1)             == 16);
  CHECK(align_up(15)            == 16);
  CHECK(align_up(16)            == 16);
  CHECK(align_up(17)            == 32);
  CHECK(align_up(SIZE_MAX - 15) == SIZE_MAX - 15);
  
  for (size_t n = 0; n < 2000; n++) {
    size_t r = align_up(n);
    CHECK(r % 16 == 0 && r >= n && r < n + 16);
  }
}

static inline void test_block_hdr_roundtrip(void) {
  static const size_t sizes[] = {32, 48, 4096, 131072, (size_t)1 << 20, (size_t)1 << 40};
  block_t b;
  
  for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); i++)
    for (int a = 0; a < 2; a++)
      for (int pf = 0; pf < 2; pf++) {
        block_set_hdr(&b, sizes[i], a, pf);
        CHECK(block_size(&b) == sizes[i]);
        CHECK(block_is_allocated(&b) == a);
        CHECK(block_prev_is_free(&b) == pf);
      }
}

static inline void test_block_hdr_masks_low_bits_of_size(void) {
  block_t b;
  block_set_hdr(&b, 0x1235, false, false);
  CHECK(block_size(&b) == 0x1230);
  CHECK(!block_is_allocated(&b));
  CHECK(!block_prev_is_free(&b));
}

static inline void test_block_prev_free_flag(void) {
  block_t b;
  block_set_hdr(&b, 64, true, false);
 
  block_set_prev_free(&b, true);
  CHECK(block_prev_is_free(&b));
  CHECK(block_is_allocated(&b));
  CHECK(block_size(&b) == 64);
 
  block_set_prev_free(&b, true);
  CHECK(block_prev_is_free(&b));
 
  block_set_prev_free(&b, false);
  CHECK(!block_prev_is_free(&b));
  CHECK(block_is_allocated(&b));
  CHECK(block_size(&b) == 64);
 
  block_set_prev_free(&b, false);
  CHECK(!block_prev_is_free(&b));
}

static inline void test_block_footer(void) {
  alignas(16) char buf[256];
  memset(buf, 0, sizeof buf);
  block_t* b = (block_t*)(buf + 8);
 
  block_set_hdr(b, 64, false, false);
  block_write_ftr(b);
 
  CHECK((char*)block_ftr(b) == buf + 8 + 64 - FTR_SIZE);
  CHECK(*block_ftr(b) == 64);
  CHECK(*(size_t*)(buf + 8 + 56) == 64);
}

static inline void test_block_neighbours(void) {
  alignas(16) char buf[512];
  memset(buf, 0, sizeof buf);
 
  block_t* b1 = (block_t*)(buf + 8);
  block_set_hdr(b1, 64, false, false);
  block_write_ftr(b1);
 
  block_t* b2 = block_next(b1);
  CHECK((char*)b2 == buf + 8 + 64);
  block_set_hdr(b2, 32, true, true);
 
  CHECK(block_prev(b2) == b1);
  CHECK((char*)block_next(b2) == buf + 8 + 64 + 32);
 
  block_t* b3 = block_next(b2);
  block_set_hdr(b3, 128, true, false);
  block_t* b4 = block_next(b3);
  block_set_hdr(b4, 32, false, true);
  block_t* b5 = block_next(b4);
  block_set_hdr(b5, 32, true, true);
  block_write_ftr(b4);
  CHECK(block_prev(b5) == b4);
}

static inline void test_block_ptr_conversion(void) {
  alignas(16) char buf[128];
  block_t* b = (block_t*)(buf + 8);
 
  void* p = block_to_ptr(b);
  CHECK((char*)p == (char*)b + HDR_SIZE);
  CHECK(ptr_to_block(p) == b);
  CHECK((uintptr_t)p % ALIGNMENT == 0);
}

static inline void test_block_chunk_of(void) {
  const uintptr_t base64k = 0x7f1234560000u;
  const uintptr_t base1m  = 0x7f1234500000u;
 
  CHECK((uintptr_t)chunk_of((void*)(base64k),         (size_t)64 << 10) == base64k);
  CHECK((uintptr_t)chunk_of((void*)(base64k + 8),     (size_t)64 << 10) == base64k);
  CHECK((uintptr_t)chunk_of((void*)(base64k + 65535), (size_t)64 << 10) == base64k);
  CHECK((uintptr_t)chunk_of((void*)(base64k + 65536), (size_t)64 << 10) == base64k + 65536);
 
  CHECK((uintptr_t)chunk_of((void*)(base1m + 16),        (size_t)1 << 20) == base1m);
  CHECK((uintptr_t)chunk_of((void*)(base1m + 0xFFFF8),   (size_t)1 << 20) == base1m);
  CHECK((uintptr_t)chunk_of((void*)(base1m + 0x100000),  (size_t)1 << 20) == base1m + 0x100000);
}


void run_all_block_h_tests() {
  test_request_to_block_size();
  test_block_request_to_block_size_properties();
  test_block_constants();
  test_block_align_up();
  test_block_hdr_roundtrip();
  test_block_hdr_masks_low_bits_of_size();
  test_block_prev_free_flag();
  test_block_footer();
  test_block_neighbours();
  test_block_ptr_conversion();
  test_block_chunk_of();
}
