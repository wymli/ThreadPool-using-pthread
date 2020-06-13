#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "threadpool.h"

void* test_sum_fn(void* arg) {
  int sum = 0;
  for (int i = 0; i < 100; i++) {
    sum += i;
  }
  printf("线程:%ld   sum= %d   i=%d\n", pthread_self(), sum, *(int*)arg);
}

int main() {
  threadpool_t pool;
  threadpool_init(&pool, 10);
  char fn_name[20] = {0};

  LOG(_LOG_DEBUG, "Begin Add Task");
  for (int i = 0; i < 20; ++i) {
    snprintf(fn_name, sizeof(fn_name), "test_%d", i);
    task_t task;
    task_init(&task);
    task_set(&task, test_sum_fn, &i, fn_name);
    pool_add_task(&pool, &task);
    // sleep(1);
  }
  sleep(2);
  LOG(_LOG_DEBUG, "最终线程状态:");
  print_list(&pool.thread_list);
  // threadpool_wait(&pool);
  threadpool_destroy(&pool);
}
