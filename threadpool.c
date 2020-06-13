#include "threadpool.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "list_t.h"
#include "log.h"

int threadpool_init(threadpool_t* pool, int n_threads) {
  // 理论上应该使用动态的函数初始化,而不是静态的宏(宏一般是用在全局变量初始化),不过这里想试一下宏的写法,就这样写了
  LOG(_LOG_INFO, "Begin thread pool init");
  pool->cond_var = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
  pool->mutx = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
  pool->rw_lock = (pthread_rwlock_t)PTHREAD_RWLOCK_INITIALIZER;

  pool->idle_size = 0;
  list_init(&pool->task_list, sizeof(task_t));
  list_init(&pool->thread_list, sizeof(thread_t));

  memset(pool->tid_list, 0, sizeof(pthread_t) * 128);

  for (int i = 0; i < n_threads; ++i) {
    pthread_t tid = pool_add_thread(pool);
    pool->tid_list[i] = tid;
  }
  sleep(1);
  LOG(_LOG_INFO, "Thread_pool init OK\n----------------------------");
  sleep(2);
}

int threadpool_wait(threadpool_t* pool) {
  int size = get_list_size(&pool->thread_list);
  for (int i = 0; i < size; i++) {
    pthread_join(pool->tid_list[i], NULL);
  }
}

int threadpool_destroy(threadpool_t* pool) {
  int size = get_list_size(&pool->thread_list);
  for (int i = 0; i < size; i++) {
    // 需要到cancel point
    // 有没有强制退出的?
    pthread_cancel(pool->tid_list[i]);
  }
}

int thread_init(thread_t* t, pthread_t* tid) {
  t->state = THREAD_INIT;
  t->tid = *tid;
}

int task_init(task_t* t) {
  t->arg = NULL;
  t->fn = NULL;
  memset(t->fn_name, 0, sizeof(t->fn_name));
}

pthread_t pool_add_thread(threadpool_t* pool) {
  pthread_t tid;
  pthread_create(&tid, NULL, thread_routine, pool);
  return tid;
};

int pool_add_task(threadpool_t* pool, task_t* task) {
  LOG(_LOG_INFO, "加入任务:%s", task->fn_name);
  push(&pool->task_list, task);
  // 不能直接唤醒,因为如果线程池没有在wait,本次唤醒就无效了
  // ->
  // 改为直接唤醒,因为即使唤醒无效,但是只要有线程结束一次唤醒后可以不进入阻塞而直接执行下一个任务
  // while (!pool->idle_size)
  //   ;
  pthread_cond_signal(&pool->cond_var);
}

void* thread_routine(void* arg) {
  threadpool_t* pool = (threadpool_t*)arg;
  pthread_t tid = pthread_self();
  thread_t thread;
  thread_init(&thread, &tid);
  thread_t* t = (thread_t*)push(&pool->thread_list, &thread);
  while (1) {
    // pthread_mutex_lock(&thread_log_mutex);
    if (t->state == THREAD_DELETE) {
      LOG(_LOG_INFO, "线程:%d 退出", tid);
      return 0;
    }
    pthread_mutex_lock(&pool->mutx);
    char thread_state_buf[20] = {0};
    str_thread_state(t->state, thread_state_buf);
    LOG(_LOG_DEBUG,
        "线程:%ld task size:%d pool size:%d idle size:%d thread_state:%s",
        pthread_self(), pool->task_list.total_size,
        get_list_size(&pool->thread_list), get_pool_idle_size(pool),
        thread_state_buf);

    incre_pool_idle_size(pool);
    int wake_flag = 0;
    while (pool->task_list.total_size == 0) {
      // 可能惊群? 不过是内核实现,不管他了
      if (wake_flag == 1) {
        LOG(_LOG_DEBUG, "线程:%ld 阻塞  无效唤醒", pthread_self());
      } else {
        LOG(_LOG_DEBUG, "线程:%ld 阻塞  等待唤醒", pthread_self());
      }
      // pthread_mutex_unlock(&thread_log_mutex);

      pthread_cond_wait(&pool->cond_var, &pool->mutx);
      // 理想的实现是wait的同时,idle_size--;
      wake_flag = 1;
    }
    wake_flag = 0;
    // pthread_mutex_lock(&thread_log_mutex);
    thread_set_state(t, THREAD_RUN);
    decre_pool_idle_size(pool);
    LOG(_LOG_DEBUG, "线程:%ld 唤醒", pthread_self());

    consume_taskq(&pool->task_list);

    print_list(&pool->task_list);

    pthread_mutex_unlock(&pool->mutx);
    thread_set_state(t, THREAD_IDLE);

    // sleep(1);
    // pthread_mutex_unlock(&thread_log_mutex);
  }
}

//消费任务队列
void* consume_taskq(list_t* list) {
  task_t task;
  if (pop(list, &task) == (void*)-1) {
    LOG(_LOG_WARN, "task_list pop error");
    return (void*)-1;
  }
  task.fn(task.arg);
  LOG(_LOG_INFO, "线程:%ld, 消费任务:%s 完成\n-----------------------",
      pthread_self(), task.fn_name)
}

int thread_set_state(thread_t* thread, thread_state_t state) {
  thread->state = state;
}

int task_set(task_t* task, _work_func fn, void* args, char* fn_name) {
  task->fn = fn;
  if (strlen(fn_name) >= sizeof(task->fn_name)) {
    LOG(_LOG_WARN, "strlen(fn_name) >= %ld", sizeof(task->fn_name));
  }
  strncpy(task->fn_name, fn_name, sizeof(task->fn_name));

  task->arg = args;
}

// concurrency safe
int get_pool_idle_size(threadpool_t* pool) {
  pthread_rwlock_rdlock(&pool->rw_lock);
  int size = pool->idle_size;
  pthread_rwlock_unlock(&pool->rw_lock);
  return size;
}

int str_thread_state(int thread_state, char* buf) {
  char* s;
  switch (thread_state) {
    case THREAD_IDLE:
      s = "THREAD_IDLE";
      break;
    case THREAD_RUN:
      s = "THREAD_RUN";
      break;
    case THREAD_DELETE:
      s = "THREAD_DELETE";
      break;
    case THREAD_INIT:
      s = "THREAD_INIT";
      break;
    default:
      s = "???";
      break;
  }

  strcpy(buf, s);
  return strcmp(s, "???") == 0 ? -1 : 0;
}

int decre_pool_idle_size(threadpool_t* pool) {
  pthread_rwlock_wrlock(&pool->rw_lock);
  pool->idle_size--;
  pthread_rwlock_unlock(&pool->rw_lock);
}

int incre_pool_idle_size(threadpool_t* pool) {
  pthread_rwlock_wrlock(&pool->rw_lock);
  pool->idle_size++;
  pthread_rwlock_unlock(&pool->rw_lock);
}