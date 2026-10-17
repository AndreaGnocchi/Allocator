#define _GNU_SOURCE
#include <stddef.h>

extern void* my_malloc(size_t size);
extern void  my_free(void* ptr);
extern void* my_calloc(size_t nmemb, size_t size);
extern void* my_realloc(void* ptr, size_t size);

void* malloc(size_t size) {
    return my_malloc(size);
}

void free(void* ptr) {
    my_free(ptr);
}

void* calloc(size_t nmemb, size_t size) {
    return my_calloc(nmemb, size);
}

void* realloc(void* ptr, size_t size) {
    return my_realloc(ptr, size);
}
