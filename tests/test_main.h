#ifndef TEST_MAIN_H
#define TEST_MAIN_H

#include <stddef.h>
#include <stdio.h>

extern size_t test_count;
extern size_t test_passed;

#define CHECK(cond) do {                                                       \
  test_count++;                                                                \
  if (cond) {                                                                  \
      test_passed++;                                                           \
  } else {                                                                     \
      fprintf(stderr, "%s:%d: CHECK FAILED: %s\n", __FILE__, __LINE__, #cond); \
  }                                                                            \
} while (0)

// block.h
void run_all_block_h_tests(void);
// os.c & os.h
void run_all_os_test(void);
// small.c & small.h
void run_all_small_test(void);
// medium.c & medium.h
void run_all_medium_test(void);
// large.c & large.h
void run_all_large_test(void);

#endif // TEST_MAIN_H
