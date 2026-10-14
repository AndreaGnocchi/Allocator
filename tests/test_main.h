#ifndef TEST_MAIN_H
#define TEST_MAIN_H

#include <stddef.h>
#include <stdio.h>

extern size_t test_passed;

#define CHECK(cond) do {                                                         \
    if (cond) {                                                                  \
        test_passed++;                                                           \
    } else {                                                                     \
        fprintf(stderr, "%s:%d: CHECK FAILED: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                            \
} while (0)

// block.h
void run_all_block_h_tests(void);


#endif // TEST_MAIN_H
