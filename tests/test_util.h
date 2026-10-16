#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#if defined(__SANITIZE_THREAD__)
#define SKIP_IF_NO_ADDRESS_LIMIT() do { return; } while (0)
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define SKIP_IF_NO_ADDRESS_LIMIT() do { return; } while (0)
#endif
#endif
#ifndef SKIP_IF_NO_ADDRESS_LIMIT
#define SKIP_IF_NO_ADDRESS_LIMIT() do { } while (0)
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  void*    p;
  size_t   n;
  uint32_t seed;
} alloc_rec_t;

typedef enum { KIND_SMALL, KIND_MEDIUM, KIND_LARGE } kind_t;

bool   recs_disjoint        (const alloc_rec_t* recs, size_t count);
bool   is_zero              (const void*        p,    size_t n);
bool   check_pattern        (const void*        p,    size_t n, uint32_t seed);
void   fill_pattern         (      void*        p,    size_t n, uint32_t seed);
kind_t test_kind            (      void*        p);
size_t test_usable_size     (      void*        p);
bool   is_unmapped          (      void*        addr);
void   limit_address_space  (size_t      extra);
void   unlimit_address_space(void);
size_t vm_size_bytes        (void);

#endif // TEST_UTIL_H
