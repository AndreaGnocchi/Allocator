#define _GNU_SOURCE

#include "test_util.h"
#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

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
