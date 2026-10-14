# Allocator 

A from-scratch malloc implementaion in C

## Goals

- Corect, safe and robust first, fast second.
- Benchmarked against glibc's ptmalloc using a custom benchmark suite and Benchmarks from [mimalloc-bench](https://github.com/daanx/mimalloc-bench).
- Performance target: Being within an order of magnitude of ptmalloc is the baseline; 
  beating it is the dream.

## Status

Early development. The full public API is implemented (my_malloc, my_free, my_calloc, my_realloc, declared in include/allocator.h), on top of three size-based heaps: small, medium and large.

Known limitations:

- No protection against double free or invalid pointers; freeing a pointer twice is undefined behavior.
- my_malloc(0) and my_calloc with a zero total size return NULL.
- Not benchmarked nor heavily tested yet.

## Documentation

Design notes live in [docs/DESIGN.md](docs/DESIGN.md).
Alongside them there is the documentation used to build this project [docs/DOCUMENTATION.md](docs/DOCUMENTATION.md)

## License

See [LICENSE](LICENSE).
