# Design Notes

## Scope
- Target: Linux x86-64, single-threaded first, thread safety later.
- API: malloc, free, calloc, realloc

## Memory source
- Small (n <= 4096): segregated free list 
 (32, 48, 64, 80, 96, 112, 128, 
 144, 160, 176, 192, 208, 224, 240, 256, 
 288, 320, 352, 384, 416, 448, 480, 512, 
 576, 640, 704, 768, 832, 896, 960, 1024, 
 1152, 1280, 1408, 1536, 1664, 1792, 1920, 2048, 
 2304, 2560, 2816, 3072, 3328, 3584, 3840, 4096).
- Medium (4096 < n <= 128 KiB): segregated fit with log-linear size classes
  (each power of two split into 8 equal steps) plus a bitmap of non-empty
  classes for O(1) lookup. Blocks are split on allocation and coalesced on free.
- Larger (n > 128Kb): Direct memory pages from the OS.

## Block layout
- Header/footer format, alignment (16 bytes)

## Open questions
- Thread safety (global lock vs per-thread arenas)
- Returning memory to the OS (madvise / munmap)
