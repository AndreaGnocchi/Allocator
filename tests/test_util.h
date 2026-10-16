#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdbool.h>
#include <stddef.h>

bool   is_zero(const void* p, size_t n);
size_t vm_size_bytes(void);

#endif // TEST_UTIL_H
