# Allocator 

A from-scratch malloc implementaion in C

## Goals

- Corect, safe and robust first, fast second.
- Benchmarked against glibc's ptmalloc using a custom benchmark suite and Benchmarks from [mimalloc-bench](https://github.com/daanx/mimalloc-bench).
- Performance target: Being within an order of magnitude of ptmalloc is the baseline; 
  beating it is the dream.

## Status

Early development. Nothing usable yet.

## Documentation

Design notes live in [docs/DESIGN.md](docs/DESIGN.md).
Alongside them there is the documentation used to build this project [docs/DOCUMENTATION.md](docs/DOCUMENTATION.md)

## License

See [LICENSE](LICENSE).
