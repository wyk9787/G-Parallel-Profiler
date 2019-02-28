#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define THREAD_NUM 5
#define MULTIPLIER 10000000

void *thread_fn(void *arg) {
  int num = *(int *)arg;
  printf("Welcome to thread %d with tid %ld\n", num, syscall(SYS_gettid));
  size_t sum = 0;
  for (int i = 0; i < (num + 1) * MULTIPLIER; i++) {
    sum += rand();
  }
  printf("Done with thread %d, sum = %zu\n", num, sum);
  return NULL;
}

int main() {
  srand(0);
  pid_t cur_pid = getpid();
  pthread_t threads[THREAD_NUM];
  printf("%p\n", thread_fn);
  printf("The main pid: %d\n", cur_pid);
  int count[THREAD_NUM];
  for (int i = 0; i < THREAD_NUM; i++) {
    count[i] = i;
  }
  for (int i = 0; i < THREAD_NUM; i++) {
    pthread_create(&threads[i], NULL, thread_fn, (void *)(&count[i]));
  }

  for (int i = 0; i < THREAD_NUM; i++) {
    pthread_join(threads[i], NULL);
  }
}
