#ifndef BLOCK_H
#define BLOCK_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define ALIGNMENT      16
#define HDR_SIZE       8
#define FTR_SIZE       8
#define MIN_BLOCK_SIZE 32 // hdr + ftr + prev + next

// Flags live in the low 4 bits of hdr
#define BLOCK_FLAG_PREV_FREE 0x1
#define BLOCK_FLAG_ALLOCATED 0x2
#define BLOCK_FLAGS_MASK     ((size_t)0xF)
#define BLOCK_SIZE_MASK      (~BLOCK_FLAGS_MASK)

typedef struct block_t {
  size_t          hdr;
  struct block_t* prev;
  struct block_t* next;
} block_t;

typedef struct {
  block_t* head;
} bin_t;

typedef struct { void* owner; } chunk_hdr_t;

static inline chunk_hdr_t* chunk_of(const void* p, size_t chunk_size) {
  return (chunk_hdr_t*)((uintptr_t)p & ~(uintptr_t)(chunk_size - 1));
}

static inline size_t align_up(size_t n) {
  return (n + (ALIGNMENT - 1)) & ~(size_t)(ALIGNMENT - 1);
}

static inline size_t request_to_block_size(size_t n) {
  if (n > SIZE_MAX - HDR_SIZE - (ALIGNMENT - 1)) return 0;
  
  size_t sz = align_up(n + HDR_SIZE);
  return sz < MIN_BLOCK_SIZE ? MIN_BLOCK_SIZE : sz;
}

// HDR

static inline size_t block_size(const block_t* b) {
  return b->hdr & BLOCK_SIZE_MASK;
}

static inline bool block_is_allocated(const block_t* b) {
  return (b->hdr & BLOCK_FLAG_ALLOCATED) != 0;
}

static inline bool block_prev_is_free(const block_t* b) {
  return (b->hdr & BLOCK_FLAG_PREV_FREE) != 0;
}

static inline void block_set_hdr(block_t* b, size_t size,
                            bool allocated, bool prev_free) {
  b->hdr = (size & BLOCK_SIZE_MASK)
           | (allocated ? BLOCK_FLAG_ALLOCATED : 0)
           | (prev_free ? BLOCK_FLAG_PREV_FREE : 0);
}

static inline void block_set_prev_free(block_t* b, bool prev_free) {
  if (prev_free) {
    b->hdr |= BLOCK_FLAG_PREV_FREE;
  } else {
    b->hdr &= (size_t)~BLOCK_FLAG_PREV_FREE;
  }
}

// FTR (only if is free)

static inline size_t* block_ftr(const block_t* b) {
  return (size_t*)((char*)b + block_size(b) - FTR_SIZE);
}

static inline void block_write_ftr(block_t* b) {
  *block_ftr(b) = block_size(b);
}

// Neighbours

static inline block_t* block_next(const block_t* b) {
  return (block_t*)((char*)b + block_size(b));
}

// Only legal if block prev is free
static inline block_t* block_prev(const block_t* b) {
  size_t prev_size = *(const size_t*)((const char*)b - FTR_SIZE);
  return (block_t*)((char*)b - prev_size);
}

// Payload <-> block

static inline block_t* ptr_to_block(void* ptr) {
  return (block_t*)((char*)ptr - HDR_SIZE);
}
 
static inline void* block_to_ptr(block_t* b) {
  return (void*)((char*)b + HDR_SIZE);
}

#endif // BLOCK_H
