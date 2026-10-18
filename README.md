# Allocator

A from-scratch malloc implementation in C for Linux x86-64.

It provides `my_malloc`, `my_free`, `my_calloc` and `my_realloc` (declared in [include/allocator.h](include/allocator.h)), is thread-safe through per-thread heaps, and can be built either as a static library or as a shared library that replaces the system `malloc` via `LD_PRELOAD`.

## Goals

- Correct, safe and robust first, fast second.
- Benchmarked against glibc's ptmalloc using a custom benchmark and the benchmarks from [mimalloc-bench](https://github.com/daanx/mimalloc-bench).
- Performance target: being within an order of magnitude of ptmalloc is the baseline; beating it is the dream.

## Usage

```c
#include "allocator.h"

int main(void) {
  int* a = my_malloc(100 * sizeof *a);
  a = my_realloc(a, 200 * sizeof *a);
  my_free(a);
  return 0;
}
```

Link against the static library:

```bash
make
gcc -I include main.c build/liballocator.a -pthread -o main
```

Or replace the system allocator of an existing program, without recompiling it:

```bash
make shared
LD_PRELOAD=$PWD/build/liballocator.so ./your_program
```

The shared library exports the standard `malloc`, `free`, `calloc` and `realloc` symbols (see [bench/wrapper.c](bench/wrapper.c)).

## How it works

Every request is rounded up to a 16-byte-aligned block (8-byte header + payload, minimum 32 bytes) and routed to one of three heaps by size.

| Heap   | Request size      | Strategy                                                                                  |
|--------|-------------------|-------------------------------------------------------------------------------------------|
| Small  | up to 4 KiB       | 47 exact size classes, bump allocation inside 512 KiB chunks, per-class free lists        |
| Medium | 4 KiB to 128 KiB  | 40 log-linear size classes (8 steps per power of two), split on allocate, coalesce on free |
| Large  | above 128 KiB     | one `mmap` per allocation, `munmap` on free                                               |

**Block layout.** Each block starts with an 8-byte header holding the block size plus two flag bits, `ALLOCATED` and `PREV_FREE`. Free medium blocks also carry a footer, so the previous block can be found for coalescing only when `PREV_FREE` is set.

**Small heap.** Chunks are 512 KiB, aligned to their own size. Fresh blocks are carved from a bump pointer. Freed blocks are pushed onto a singly linked list for their exact size class. A lookup table maps a size to its class, and small blocks are never coalesced.

**Medium heap.** Chunks are 2 MiB, aligned to their own size, and end with a sentinel block. Free blocks live in doubly linked bins, and a 64-bit bitmap of non-empty bins makes finding a fit O(1) with a single count-trailing-zeros. A block is split when allocated and merged with both neighbours when freed. The last bin is a catch-all for blocks of 128 KiB and larger.

**Large heap.** Each allocation gets its own page-aligned mapping with no shared state or lists.

**Threads.** Every thread owns a heap (small + medium) reached through a thread-local pointer, so the fast path takes no locks. The first word of each chunk records its owner. When a thread frees a block that belongs to another thread's heap, it pushes the block onto that heap's lock-free `remote_head` stack. The owner drains it at the start of its next `my_malloc`. When a thread exits, its heap goes back to a mutex-protected pool and is reused by the next new thread.

**`realloc`.** It tries to resize in place first. Small blocks stay put if the new size falls in the same size class. Medium blocks are split, or grow into a free next block, when called by the owning thread. Large blocks stay put if the new size fits and is at least half of the current usable size. Otherwise it allocates, copies and frees.

**`calloc`.** It checks the multiplication for overflow, and skips the `memset` for large blocks because fresh `mmap` pages are already zeroed.

## Known limitations

- No protection against double free or invalid pointers; freeing a pointer twice is undefined behavior.
- `my_malloc(0)` and `my_calloc` with a zero total size return `NULL`.
- Small and medium chunks are never returned to the OS, only reused. Large allocations are unmapped on free.
- A block freed by a thread other than its owner is only reclaimed when the owner next calls `my_malloc` (or exits).
- Only `malloc`, `free`, `calloc` and `realloc` are provided. There is no `memalign` or `posix_memalign`, and alignment is fixed at 16 bytes.
- Linux x86-64 only.

## Documentation

Design notes live in [docs/DESIGN.md](docs/DESIGN.md).
Alongside them there is the documentation used to build this project [docs/DOCUMENTATION.md](docs/DOCUMENTATION.md).

---
# Tree
``` bash
Allocator
├── bench
│   ├── throughput.c
│   └── wrapper.c
├── docs
│   ├── DESIGN.md
│   └── DOCUMENTATION.md
├── .github
│   └── workflows
│       └── ci.yml
├── .gitignore
├── include
│   └── allocator.h
├── LICENSE
├── Makefile
├── README.md
├── src
│   ├── allocator.c
│   ├── block.h
│   ├── large.c
│   ├── large.h
│   ├── medium.c
│   ├── medium.h
│   ├── os.c
│   ├── os.h
│   ├── small.c
│   ├── small.h
│   ├── thread_heap.c
│   └── thread_heap.h
└── tests
    ├── test_block_h.c
    ├── test_calloc.c
    ├── test_free.c
    ├── test_large.c
    ├── test_main.c
    ├── test_main.h
    ├── test_malloc.c
    ├── test_medium.c
    ├── test_os.c
    ├── test_realloc.c
    ├── test_small.c
    ├── test_stress.c
    ├── test_thread_heap.c
    ├── test_threads.c
    ├── test_util.c
    └── test_util.h
```
# Build

```bash
# Compile the static library (liballocator.a)
make all

# Compile the shared library (liballocator.so)
make shared

# Clean all build artifacts
make clean
```

# Tests

```bash
# Run the core unit and stress test suite
make test

# Run tests with Undefined Behavior Sanitizer
make test-ubsan

# Run tests with Thread Sanitizer
make test-tsan
```

The suite covers the block helpers, the OS layer, each of the three heaps, the thread heaps, `malloc`/`calloc`/`realloc`/`free`, randomized stress tests and multithreaded tests. CI runs `test`, `test-ubsan` and `test-tsan` on every push to `main` and on pull requests.

# Benchmarks

## Mine

```bash
# Run throughput benchmark with your allocator (via LD_PRELOAD) and ptmalloc
make bench-mine-all
```

`bench/throughput.c` runs 4 threads, each doing 500,000 random operations on a pool of 1024 pointers: `malloc`, `calloc`, `realloc` and `free`, with sizes drawn uniformly from 1 byte to 1 MiB (2 MiB for `realloc`). Most of those sizes fall in the large heap.

## Mimalloc's

```bash
# Clone and set up the mimalloc-bench environment
make mimalloc-bench-setup

# Run mimalloc-bench with your custom shared library allocator
make bench-mi
```

You can customize the benchmark run with variables, for example:

```bash
make bench-mi MI_TESTS="larson cfrac" MI_ALLOCS="mi sys je"
```

## Results

Measured on:

| Component | Details                                |
|-----------|----------------------------------------|
| CPU       | Intel Core i5-9400F, 6 cores / 6 threads |
| RAM       | 16 GB DDR4-2666                        |
| OS        | Arch Linux, kernel 7.2.9-arch1-1       |
| Compiler  | gcc 16.2.1                             |
| libc      | glibc 2.44                             |

These are single runs, so small differences are within noise.

### Throughput benchmark (`make bench-mine-all`)

| Allocator | Time (s) | Throughput (ops/s) |
|-----------|---------:|-------------------:|
| my        |    19.62 |            102,035 |
| ptmalloc  |    25.43 |             78,731 |

Both runs perform the same 2,002,329 operations. This allocator finished about 23% faster, roughly 30% more operations per second.

### mimalloc-bench

`my` is this allocator, `mi` is mimalloc and `sys` is glibc's ptmalloc. Time is in seconds and RSS in KB.

| Test    | my time | mi time | sys time | my RSS | mi RSS | sys RSS |
|---------|--------:|--------:|---------:|-------:|-------:|--------:|
| larsonN |   10.02 |    8.81 |     9.77 | 59,964 | 67,152 |  51,692 |
| barnes  |    2.32 |    2.39 |     2.32 | 64,304 | 68,332 |  62,216 |
| espresso|    4.28 |    4.12 |     4.55 |  5,092 |  9,008 |   3,864 |
| cfrac   |    5.89 |    5.56 |     5.59 |  5,664 |  4,848 |   3,864 |

Compared with ptmalloc, this allocator is on par on `barnes`, about 6% faster on `espresso`, and about 3% and 5% slower on `larsonN` and `cfrac`. Compared with mimalloc it is behind on `larsonN`, `espresso` and `cfrac`, and slightly ahead on `barnes`. Peak memory use is a bit higher than ptmalloc's on all four tests.

---
# License
See [LICENSE](LICENSE).
