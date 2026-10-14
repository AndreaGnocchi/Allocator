#include "../src/block.h"
#include "test_main.h"

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

void run_all_block_h_tests() {
  test_request_to_block_size();
}
