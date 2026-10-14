#include "test_main.h"

size_t test_passed = 0;

static inline void run_all_tests(void) {
  run_all_block_h_tests();
}

int main(void) {
  const size_t test_count = 0; // TODO update to the right number
  
  printf("Start Testing:\n");
  
  run_all_tests();
  
  printf("\n---\nEnd Testing\n---\n");
  printf("\n%zu/%zu tests succeeded\n", test_passed, test_count);
  
  return (test_passed == test_count) ? 0 : 1;
}
