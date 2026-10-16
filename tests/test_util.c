#define _GNU_SOURCE

#include "test_util.h"
#include "../src/medium.h"
#include "../src/large.h"
#include "../src/small.h"
#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdlib.h>

bool is_zero(const void* p, size_t n) {
  const unsigned char* b = p;

  for (size_t i = 0; i < n; i++)
    if (b[i] != 0) return false;
  
  return true;
}

size_t vm_size_bytes(void) {
  FILE* f = fopen("/proc/self/statm", "r");
  if (!f) return 0;
  
  unsigned long pages = 0;
  if (fscanf(f, "%lu", &pages) != 1) pages = 0;
  fclose(f);
  return (size_t)pages * (size_t)sysconf(_SC_PAGESIZE);
}

void fill_pattern(void* p, size_t n, uint32_t seed) {
  unsigned char* b = p;
  for (size_t i = 0; i < n; i++)
    b[i] = (unsigned char)(seed * 31u + i * 7u + (i >> 9));
}

bool check_pattern(const void* p, size_t n, uint32_t seed) {
  const unsigned char* b = p;
  for (size_t i = 0; i < n; i++)
    if (b[i] != (unsigned char)(seed * 31u + i * 7u + (i >> 9))) return false;
  return true;
}

static struct rlimit saved_as;
static int           saved_valid;
 
void limit_address_space(size_t extra) {
  if (!saved_valid) {
    getrlimit(RLIMIT_AS, &saved_as);
    saved_valid = 1;
  }
  struct rlimit rl = saved_as;
  rl.rlim_cur      = (rlim_t)(vm_size_bytes() + extra);
  setrlimit(RLIMIT_AS, &rl);
}
 
void unlimit_address_space(void) {
  if (saved_valid) setrlimit(RLIMIT_AS, &saved_as);
}

bool is_unmapped(void* addr) {
  unsigned char vec[16];
  return mincore(addr, 4096, vec) == -1 && errno == ENOMEM;
}

size_t test_usable_size(void* p) {
  size_t sz = block_size(ptr_to_block(p));
  if (sz > MEDIUM_MAX_SIZE) return large_usable_size(p);
  return sz - HDR_SIZE;
}

static inline int cmp_rec(const void* a, const void* b) {
  uintptr_t pa = (uintptr_t)((const alloc_rec_t*)a)->p;
  uintptr_t pb = (uintptr_t)((const alloc_rec_t*)b)->p;
  return (pa > pb) - (pa < pb);
}


bool recs_disjoint(const alloc_rec_t* recs, size_t count) {
  if (count < 2) return true;
 
  alloc_rec_t* copy = malloc(count * sizeof *copy);
  for (size_t i = 0; i < count; i++) copy[i] = recs[i];
  qsort(copy, count, sizeof *copy, cmp_rec);
 
  bool ok = true;
  for (size_t i = 0; i + 1 < count; i++) {
    if ((uintptr_t)copy[i].p + copy[i].n > (uintptr_t)copy[i + 1].p) {
      ok = false;
      break;
    }
  }
  free(copy);
  return ok;
}

kind_t test_kind(void* p) {
  size_t sz = block_size(ptr_to_block(p));
  if (sz <= SMALL_MAX_SIZE) return KIND_SMALL;
  if (sz <= MEDIUM_MAX_SIZE) return KIND_MEDIUM;
  return KIND_LARGE;
}
