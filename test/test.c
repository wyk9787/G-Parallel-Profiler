#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define THREAD_NUM 10
#define MULTIPLIER 1000000

void *thread_fn(void *arg) {
  int num = *(int *)arg;
  printf("Welcome to thread %d\n", num);
  size_t sum = 0;
  for (int i = 0; i < num * MULTIPLIER; i++) {
    sum += rand();
  }
  printf("Done with thread %d, sum = %zu\n", num, sum);
  sleep(20);
  return NULL;
}

int main() {
  srand(0);
  pthread_t threads[THREAD_NUM];
  int count[THREAD_NUM] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  for (int i = 0; i < THREAD_NUM; i++) {
    pthread_create(&threads[i], NULL, thread_fn, (void *)(&count[i]));
  }

  for (int i = 0; i < THREAD_NUM; i++) {
    pthread_join(threads[i], NULL);
  }
}
