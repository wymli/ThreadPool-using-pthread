#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "list_t.h"

#define THREADPOLL_MAXSZ 1024

typedef void* (*_work_func)(void* arg);

typedef enum THREAD_STATE {
  THREAD_IDLE = 1,
  THREAD_RUN = 2,
  THREAD_DELETE = 3,
  THREAD_INIT = 4,
} thread_state_t;

typedef struct thread_t {
  pthread_t tid;
  thread_state_t state;
} thread_t;

typedef struct task_t {
  _work_func fn;
  char fn_name[20];
  void* arg;
} task_t;

typedef struct threadpool_t {
  size_t idle_size; // 可以删去
  pthread_t tid_list[128];

  list_t thread_list;
  list_t task_list;

  pthread_mutex_t mutx;
  pthread_rwlock_t rw_lock;
  pthread_cond_t cond_var;
} threadpool_t;


int threadpool_init(threadpool_t* pool, int n_threads);
int threadpool_wait(threadpool_t* pool);
int threadpool_destroy(threadpool_t* pool);

int thread_init(thread_t* t, pthread_t* tid);
int task_init(task_t* t);
pthread_t pool_add_thread(threadpool_t* pool);
int pool_add_task(threadpool_t* pool, task_t* task);
void* thread_routine(void* arg);
void* consume_taskq(list_t* list);
int thread_set_state(thread_t* thread, thread_state_t state);
int task_set(task_t* task, _work_func fn, void* args, char* fn_name);

int get_pool_idle_size(threadpool_t* pool);
int decre_pool_idle_size(threadpool_t* pool);
int incre_pool_idle_size(threadpool_t* pool);
int str_thread_state(int thread_state, char* buf);

#endif