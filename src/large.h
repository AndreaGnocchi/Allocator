#ifndef LARGE_H
#define LARGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "block.h"

#define LARGE_MIN_SIZE ((size_t)(128 << 10) + 1)
#define LARGE_OVERHEAD (ALIGNMENT)

/* Layout of one mapping (page aligned, one mapping per allocation):
 *
 *   base                 base+8       base+16
 *   [ 8 bytes padding ]  [ hdr 8 ]    [ payload ... ]
 *
 * hdr = total mapped size | ALLOCATED. The padding keeps the payload 16-byte aligned.
 * No lists or shared state: free is just munmap(ptr - 16, size).
 */

void*  large_malloc     (size_t size);
bool   large_free       (void*  ptr);
size_t large_usable_size(const void* ptr);

#endif // LARGE_H
