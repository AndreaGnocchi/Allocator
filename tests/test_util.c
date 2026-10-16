#include "test_util.h"
#include <stdio.h>
#include <unistd.h>

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

