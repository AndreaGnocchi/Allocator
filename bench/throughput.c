#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>

#define NUM_THREADS 4
#define ITERATIONS  500000
#define MAX_POINTERS 1024

typedef struct {
    int      thread_id;
    uint64_t ops_completed;
} thread_arg_t;

void* worker(void* arg) {
  thread_arg_t* t_arg              = (thread_arg_t*)arg;
  void*         ptrs[MAX_POINTERS] = {0};
  uint64_t      ops                = 0;

  unsigned int seed = 1337 + t_arg->thread_id;

  for (int i = 0; i < ITERATIONS; i++) {
    int idx    = rand_r(&seed) % MAX_POINTERS;
    int action = rand_r(&seed) % 100;
    
    if (!ptrs[idx]) {
      size_t size = (rand_r(&seed) % ((size_t)1 << 20)) + 1;
      
      if (action < 30) {
        ptrs[idx] = calloc(1, size);
      } else {
        ptrs[idx] = malloc(size);
      }
      
      if (ptrs[idx]) ops++;
    } else {
      if (action < 20) {
        size_t new_size = (rand_r(&seed) % ((size_t)2 << 20)) + 1;
        void*  new_ptr  = realloc(ptrs[idx], new_size);
        
        if (new_ptr) {
          ptrs[idx] = new_ptr;
          ops++;
        }
      } else {
        free(ptrs[idx]);
        ptrs[idx] = NULL;
        ops++;
      }
    }
  }

  for (int i = 0; i < MAX_POINTERS; i++) {
    if (ptrs[i] != NULL) {
      free(ptrs[i]);
      ptrs[i] = NULL;
      ops++;
    }
  }

  t_arg->ops_completed = ops;
  return NULL;
}

int main(void) {
  pthread_t    threads[NUM_THREADS];
  thread_arg_t t_args[NUM_THREADS];

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  for (int i = 0; i < NUM_THREADS; i++) {
    t_args[i].thread_id     = i;
    t_args[i].ops_completed = 0;
    pthread_create(&threads[i], NULL, worker, &t_args[i]);
  }

  uint64_t total_ops = 0;
  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_join(threads[i], NULL);
    total_ops += t_args[i].ops_completed;
  }

  clock_gettime(CLOCK_MONOTONIC, &end);

  double elapsed = (end.tv_sec - start.tv_sec) + 
                   (end.tv_nsec - start.tv_nsec) / 1e9;

  printf("Throughput Benchmark Complete:\n");
  printf("  Threads      : %d\n", NUM_THREADS);
  printf("  Total Ops    : %lu\n", total_ops);
  printf("  Time Elapsed : %.4f seconds\n", elapsed);
  printf("  Throughput   : %.2f ops/sec\n", total_ops / elapsed);

  return 0;
}
