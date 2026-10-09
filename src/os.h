#ifndef OS_H
#define OS_H

#include <stddef.h>
#include <stdbool.h>

size_t os_get_page_size   (void);
size_t os_align_to_page   (size_t n);
void   os_init            (void);
bool   os_unmap_pages     (void*  addr, size_t size);
bool   os_madvise_dontneed(void*  addr, size_t size);
void*  os_map_pages       (size_t size);

#endif // OS_H
