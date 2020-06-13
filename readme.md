
# 基于pthread的linux c 并发线程库
---
- 概览:
  - 线程借助pthread_cond_t, 通过pthread_cond_wait睡眠,通过pthread_cond_signal唤醒
  - 通过链表作为容器容纳 thread和task
  - 特点:
    - 简答解决task并发(线程池并发)
    - 简单解决log并发(mutex和tls)
    - 简单实现list泛型
    - posix api风格
- 编译: `gcc threadpool.c main.c log.c list_t.c -o main -lpthread`
- 主要线程函数循环(简化版):
  ```c
  void* thread_routine(void* arg) {
    threadpool_t* pool = (threadpool_t*)arg;
    pthread_t tid = pthread_self();
    thread_t thread;
    thread_init(&thread, &tid);
    thread_t* t = (thread_t*)push(&pool->thread_list, &thread);
    while (1) {
      pthread_mutex_lock(&pool->mutx);

      int wake_flag = 0;
      while (pool->task_list.total_size == 0) {
        // 可能惊群? 不过是库实现,不管他了
        if (wake_flag == 1) {
          LOG(_LOG_DEBUG, "线程:%ld 阻塞  无效唤醒", pthread_self());
        } else {
          LOG(_LOG_DEBUG, "线程:%ld 阻塞  等待唤醒", pthread_self());
        }
        pthread_cond_wait(&pool->cond_var, &pool->mutx);
        // 理想的实现是wait的同时,idle_size--; -->因此,弃用idle_size
        wake_flag = 1;
      }
      wake_flag = 0;
      thread_set_state(t, THREAD_RUN);
      consume_taskq(&pool->task_list);
      pthread_mutex_unlock(&pool->mutx);
      thread_set_state(t, THREAD_IDLE);
    }
  }
  ```
- 测试函数(线程库的用法):
  ```c
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
  ```

- 文件组织
  ```
  |-threadpool.h  
  |-log.h
  |-list_t.h
  ```
- threadpool.h
  - 用于实现线程库主功能函数
  - 各个主要数据类型
    ```c
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
      size_t idle_size;
      pthread_t tid_list[128];

      list_t thread_list;
      list_t task_list;

      pthread_mutex_t mutx;
      pthread_rwlock_t rw_lock;
      pthread_cond_t cond_var;
    } threadpool_t;
    ```
  - 辅助的常量,宏,类型别名等
    ```c
    typedef void* (*_work_func)(void* arg);

    typedef enum THREAD_STATE {
      THREAD_IDLE = 1,
      THREAD_RUN = 2,
      THREAD_DELETE = 3,
      THREAD_INIT = 4,
    } thread_state_t;
    ```

  - 函数原型
    ```c
    int threadpool_init(threadpool_t* pool, int n_threads);
    int threadpool_wait(threadpool_t* pool);
    int threadpool_destroy(threadpool_t* pool);

    int thread_init(thread_t* t, pthread_t* tid);
    int thread_set_state(thread_t* thread, thread_state_t state);

    int task_init(task_t* t);
    int task_set(task_t* task, _work_func fn, void* args, char* fn_name);

    pthread_t pool_add_thread(threadpool_t* pool);
    int pool_add_task(threadpool_t* pool, task_t* task);

    void* thread_routine(void* arg);
    void* consume_taskq(list_t* list);
    
    int get_pool_idle_size(threadpool_t* pool);
    int decre_pool_idle_size(threadpool_t* pool);
    int incre_pool_idle_size(threadpool_t* pool);
    int str_thread_state(int thread_state, char* buf);
    ```
- log.h/.c
  - 实现一个并发安全的log
    - 即每次调用log函数是原子的
    - 改进方向:按log来源进行分类排序
  - 技术要点
    - 一方面是log锁,另一方面是tls,绝对不能共用log_buffer,很容易错
      ```c
      static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
      __thread char log_buffer[LOG_BUFFER_MAXSZ] = {0};
      ```
  - 辅助的枚举常量,宏
    ```c
    #define DEBUG_2
    #define DEBUG

    enum LOG_SEVERITY {
      _LOG_INFO = 1,
      _LOG_WARN = 2,
      _LOG_ERR = 3,
      _LOG_DEBUG = 4,
      _LOG_DEBUG_2 = 5,  // 详细debug
    };
    ```
  - 函数:
    - 改进:应该将几个内置宏(func,line...)作为参数传进去log_helper的,这样就不用写LOG宏了
    ```c
    /* n: serverity code  */
    #define LOG(n, fmt, ...)                                                       \
      do {                                                                         \
        log_helper(n, get_prefix("[%s:%d:%s]", __func__, __LINE__, __FILE__), fmt, \
                  ##__VA_ARGS__);                                                 \
      } while (0);

    const char* get_prefix(const char* fmt, ...);
    void log_helper(enum LOG_SEVERITY severity,
                    const char* prefix,
                    const char* fmt,
                    ...);
    ```
- list
  - 实现了一个简单泛型list,用于装task和thread
    - 技术要点:
      - 利用void*指针
      - 利用data_size
  - 头文件:
    ```c
    #ifndef NULL
    #define NULL ((void*)0)
    #endif

    typedef struct list_node_t {
      void* data;
      struct list_node_t* next;
    } list_node_t;

    typedef struct list_t {
      pthread_mutex_t mutx;
      pthread_rwlock_t rw_lock;  //访问total_size
      list_node_t* head;
      list_node_t* tail;
      int total_size;  //计数,增加一个node,total_size++
      int data_size;
    } list_t;

    void* list_init(list_t* list, int data_size);
    void* push(list_t* list, void* elem);
    void* pop(list_t* list, void* elem);

    void print_list(list_t* list);

    int get_list_size(list_t* list);
    ```
---
# Thread Pool
### 非并发状态
  - 可以看出每次唤醒的线程都是不一样的!
测试代码:
```c
int main() {
  threadpool_t pool;
  threadpool_init(&pool, 10);
  char fn_name[20] = {0};

  LOG(_LOG_DEBUG, "Begin Add Task");
  for (int i = 0; i < 5; ++i) {
    snprintf(fn_name, sizeof(fn_name), "test_%d", i);
    task_t task;
    task_init(&task);
    task_set(&task, test_sum_fn, &i, fn_name);
    pool_add_task(&pool, &task);
    sleep(1);
  }
  sleep(2);
  LOG(_LOG_DEBUG, "最终线程状态:");
  print_list(&pool.thread_list);
  // threadpool_wait(&pool);
  threadpool_destroy(&pool);
}
```
```java
liwm29@lwm:~/pthreadpool$ gcc threadpool.c main.c log.c list_t.c -o main -lpthread
liwm29@lwm:~/pthreadpool$ ./main 
[INFO] [threadpool_init:14:threadpool.c] Begin thread pool init
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:tid:140447441553152 state:THREAD_INIT  | 
[DEBUG] [thread_routine:97:threadpool.c] 线程:140447441553152 task size:0 pool size:1 idle size:0 thread_state:THREAD_INIT
[DEBUG] [thread_routine:106:threadpool.c] 线程:140447441553152 阻塞  等待唤醒
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:tid:140447441553152 state:THREAD_INIT  | tid:140447433099008 state:THREAD_INIT  | 
[DEBUG] [thread_routine:97:threadpool.c] 线程:140447433099008 task size:0 pool size:2 idle size:1 thread_state:THREAD_INIT
[DEBUG] [thread_routine:106:threadpool.c] 线程:140447433099008 阻塞  等待唤醒
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:tid:140447441553152 state:THREAD_INIT  | tid:140447433099008 state:THREAD_INIT  | tid:140447424644864 state:THREAD_INIT  | 
[DEBUG] [thread_routine:97:threadpool.c] 线程:140447424644864 task size:0 pool size:3 idle size:2 thread_state:THREAD_INIT
[DEBUG] [thread_routine:106:threadpool.c] 线程:140447424644864 阻塞  等待唤醒
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:tid:140447441553152 state:THREAD_INIT  | tid:140447433099008 state:THREAD_INIT  | tid:140447424644864 state:THREAD_INIT  | tid:140447416190720 state:THREAD_INIT  | 
[DEBUG] [thread_routine:97:threadpool.c] 线程:140447416190720 task size:0 pool size:4 idle size:3 thread_state:THREAD_INIT
[DEBUG] [thread_routine:106:threadpool.c] 线程:140447416190720 阻塞  等待唤醒
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:tid:140447441553152 state:THREAD_INIT  | tid:140447433099008 state:THREAD_INIT  | tid:140447424644864 state:THREAD_INIT  | tid:140447416190720 state:THREAD_INIT  | tid:140447407736576 state:THREAD_INIT  | 
[DEBUG] [thread_routine:97:threadpool.c] 线程:140447407736576 task size:0 pool size:5 idle size:4 thread_state:THREAD_INIT
[DEBUG] [thread_routine:106:threadpool.c] 线程:140447407736576 阻塞  等待唤醒
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:tid:140447441553152 state:THREAD_INIT  | tid:140447433099008 state:THREAD_INIT  | tid:140447424644864 state:THREAD_INIT  | tid:140447416190720 state:THREAD_INIT  | tid:140447407736576 state:THREAD_INIT  | tid:140447399282432 state:THREAD_INIT  | 
[DEBUG] [thread_routine:97:threadpool.c] 线程:140447399282432 task size:0 pool size:6 idle size:5 thread_state:THREAD_INIT
[DEBUG] [thread_routine:106:threadpool.c] 线程:140447399282432 阻塞  等待唤醒
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:tid:140447441553152 state:THREAD_INIT  | tid:140447433099008 state:THREAD_INIT  | tid:140447424644864 state:THREAD_INIT  | tid:140447416190720 state:THREAD_INIT  | tid:140447407736576 state:THREAD_INIT  | tid:140447399282432 state:THREAD_INIT  | tid:140447390828288 state:THREAD_INIT  | 
[DEBUG] [thread_routine:97:threadpool.c] 线程:140447390828288 task size:0 pool size:7 idle size:6 thread_state:THREAD_INIT
[DEBUG] [thread_routine:106:threadpool.c] 线程:140447390828288 阻塞  等待唤醒
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:tid:140447441553152 state:THREAD_INIT  | tid:140447433099008 state:THREAD_INIT  | tid:140447424644864 state:THREAD_INIT  | tid:140447416190720 state:THREAD_INIT  | tid:140447407736576 state:THREAD_INIT  | tid:140447399282432 state:THREAD_INIT  | tid:140447390828288 state:THREAD_INIT  | tid:140447041128192 state:THREAD_INIT  | 
[DEBUG] [thread_routine:97:threadpool.c] 线程:140447041128192 task size:0 pool size:8 idle size:7 thread_state:THREAD_INIT
[DEBUG] [thread_routine:106:threadpool.c] 线程:140447041128192 阻塞  等待唤醒
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:tid:140447441553152 state:THREAD_INIT  | tid:140447433099008 state:THREAD_INIT  | tid:140447424644864 state:THREAD_INIT  | tid:140447416190720 state:THREAD_INIT  | tid:140447407736576 state:THREAD_INIT  | tid:140447399282432 state:THREAD_INIT  | tid:140447390828288 state:THREAD_INIT  | tid:140447041128192 state:THREAD_INIT  | tid:140447032674048 state:THREAD_INIT  | 
[DEBUG] [thread_routine:97:threadpool.c] 线程:140447032674048 task size:0 pool size:9 idle size:8 thread_state:THREAD_INIT
[DEBUG] [thread_routine:106:threadpool.c] 线程:140447032674048 阻塞  等待唤醒
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:tid:140447441553152 state:THREAD_INIT  | tid:140447433099008 state:THREAD_INIT  | tid:140447424644864 state:THREAD_INIT  | tid:140447416190720 state:THREAD_INIT  | tid:140447407736576 state:THREAD_INIT  | tid:140447399282432 state:THREAD_INIT  | tid:140447390828288 state:THREAD_INIT  | tid:140447041128192 state:THREAD_INIT  | tid:140447032674048 state:THREAD_INIT  | tid:140447024219904 state:THREAD_INIT  | 
[DEBUG] [thread_routine:97:threadpool.c] 线程:140447024219904 task size:0 pool size:10 idle size:9 thread_state:THREAD_INIT
[DEBUG] [thread_routine:106:threadpool.c] 线程:140447024219904 阻塞  等待唤醒
[INFO] [threadpool_init:30:threadpool.c] Thread_pool init OK
----------------------------
[DEBUG] [main:21:main.c] Begin Add Task
[INFO] [pool_add_task:68:threadpool.c] 加入任务:test_0
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:test_0  
[DEBUG] [thread_routine:118:threadpool.c] 线程:140447441553152 唤醒
线程:140447441553152   sum= 4950   i=0
[INFO] [consume_taskq:141:threadpool.c] 线程:140447441553152, 消费任务:test_0 完成
-----------------------
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表: 空
[DEBUG] [thread_routine:97:threadpool.c] 线程:140447441553152 task size:0 pool size:10 idle size:9 thread_state:THREAD_IDLE
[DEBUG] [thread_routine:106:threadpool.c] 线程:140447441553152 阻塞  等待唤醒
[INFO] [pool_add_task:68:threadpool.c] 加入任务:test_1
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:test_1  
[DEBUG] [thread_routine:118:threadpool.c] 线程:140447433099008 唤醒
线程:140447433099008   sum= 4950   i=1
[INFO] [consume_taskq:141:threadpool.c] 线程:140447433099008, 消费任务:test_1 完成
-----------------------
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表: 空
[DEBUG] [thread_routine:97:threadpool.c] 线程:140447433099008 task size:0 pool size:10 idle size:9 thread_state:THREAD_IDLE
[DEBUG] [thread_routine:106:threadpool.c] 线程:140447433099008 阻塞  等待唤醒
[INFO] [pool_add_task:68:threadpool.c] 加入任务:test_2
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:test_2  
[DEBUG] [thread_routine:118:threadpool.c] 线程:140447424644864 唤醒
线程:140447424644864   sum= 4950   i=2
[INFO] [consume_taskq:141:threadpool.c] 线程:140447424644864, 消费任务:test_2 完成
-----------------------
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表: 空
[DEBUG] [thread_routine:97:threadpool.c] 线程:140447424644864 task size:0 pool size:10 idle size:9 thread_state:THREAD_IDLE
[DEBUG] [thread_routine:106:threadpool.c] 线程:140447424644864 阻塞  等待唤醒
[INFO] [pool_add_task:68:threadpool.c] 加入任务:test_3
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:test_3  
[DEBUG] [thread_routine:118:threadpool.c] 线程:140447416190720 唤醒
线程:140447416190720   sum= 4950   i=3
[INFO] [consume_taskq:141:threadpool.c] 线程:140447416190720, 消费任务:test_3 完成
-----------------------
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表: 空
[DEBUG] [thread_routine:97:threadpool.c] 线程:140447416190720 task size:0 pool size:10 idle size:9 thread_state:THREAD_IDLE
[DEBUG] [thread_routine:106:threadpool.c] 线程:140447416190720 阻塞  等待唤醒
[INFO] [pool_add_task:68:threadpool.c] 加入任务:test_4
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:test_4  
[DEBUG] [thread_routine:118:threadpool.c] 线程:140447407736576 唤醒
线程:140447407736576   sum= 4950   i=4
[INFO] [consume_taskq:141:threadpool.c] 线程:140447407736576, 消费任务:test_4 完成
-----------------------
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表: 空
[DEBUG] [thread_routine:97:threadpool.c] 线程:140447407736576 task size:0 pool size:10 idle size:9 thread_state:THREAD_IDLE
[DEBUG] [thread_routine:106:threadpool.c] 线程:140447407736576 阻塞  等待唤醒
[DEBUG] [main:32:main.c] 最终线程状态:
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:tid:140447441553152 state:THREAD_IDLE  | tid:140447433099008 state:THREAD_IDLE  | tid:140447424644864 state:THREAD_IDLE  | tid:140447416190720 state:THREAD_IDLE  | tid:140447407736576 state:THREAD_IDLE  | tid:140447399282432 state:THREAD_INIT  | tid:140447390828288 state:THREAD_INIT  | tid:140447041128192 state:THREAD_INIT  | tid:140447032674048 state:THREAD_INIT  | tid:140447024219904 state:THREAD_INIT  |
```

### 并发状态
- 可以看出只会有一个线程被唤醒
测试代码:
```c
int main() {
  threadpool_t pool;
  threadpool_init(&pool, 10);
  char fn_name[20] = {0};

  LOG(_LOG_DEBUG, "Begin Add Task");
  for (int i = 0; i < 5; ++i) {
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
```
```java
liwm29@lwm:~/pthreadpool$ gcc threadpool.c main.c log.c list_t.c -o main -lpthread
liwm29@lwm:~/pthreadpool$ ./main 

....(略去部分输出)
-----------------------
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表: 空
[DEBUG] [thread_routine:97:threadpool.c] 线程:140019255543552 task size:0 pool size:10 idle size:9 thread_state:THREAD_IDLE
[DEBUG] [thread_routine:106:threadpool.c] 线程:140019255543552 阻塞  等待唤醒
[DEBUG] [thread_routine:104:threadpool.c] 线程:140019238635264 阻塞  无效唤醒
[DEBUG] [thread_routine:104:threadpool.c] 线程:140019155011328 阻塞  无效唤醒
[DEBUG] [thread_routine:104:threadpool.c] 线程:140019146557184 阻塞  无效唤醒
[DEBUG] [thread_routine:104:threadpool.c] 线程:140019247089408 阻塞  无效唤醒
[DEBUG] [main:31:main.c] 最终线程状态:
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:tid:140019255543552 state:THREAD_IDLE  | tid:140019247089408 state:THREAD_INIT  | tid:140019238635264 state:THREAD_INIT  | tid:140019155011328 state:THREAD_INIT  | tid:140019146557184 state:THREAD_INIT  | tid:140019138103040 state:THREAD_INIT  | tid:140019129648896 state:THREAD_INIT  | tid:140019121194752 state:THREAD_INIT  | tid:140019112740608 state:THREAD_INIT  | tid:140019104286464 state:THREAD_INIT  | 
```
---
- 加大任务量,task=20,thread=10
  - 无效唤醒增多,最终还是只有一个线程持续在做任务
```java
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表: 空
[DEBUG] [thread_routine:97:threadpool.c] 线程:139911795312384 task size:0 pool size:10 idle size:9 thread_state:THREAD_IDLE
[DEBUG] [thread_routine:106:threadpool.c] 线程:139911795312384 阻塞  等待唤醒
[DEBUG] [thread_routine:104:threadpool.c] 线程:139911176849152 阻塞  无效唤醒
[DEBUG] [thread_routine:104:threadpool.c] 线程:139911803766528 阻塞  无效唤醒
[DEBUG] [thread_routine:104:threadpool.c] 线程:139911705265920 阻塞  无效唤醒
[DEBUG] [thread_routine:104:threadpool.c] 线程:139911696811776 阻塞  无效唤醒
[DEBUG] [thread_routine:104:threadpool.c] 线程:139911679903488 阻塞  无效唤醒
[DEBUG] [thread_routine:104:threadpool.c] 线程:139911713720064 阻塞  无效唤醒
[DEBUG] [thread_routine:104:threadpool.c] 线程:139911688357632 阻塞  无效唤醒
[DEBUG] [thread_routine:104:threadpool.c] 线程:139911671449344 阻塞  无效唤醒
[DEBUG] [thread_routine:104:threadpool.c] 线程:139911662995200 阻塞  无效唤醒
[DEBUG] [main:31:main.c] 最终线程状态:
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:tid:139911803766528 state:THREAD_INIT  | tid:139911795312384 state:THREAD_IDLE  | tid:139911713720064 state:THREAD_INIT  | tid:139911705265920 state:THREAD_INIT  | tid:139911696811776 state:THREAD_INIT  | tid:139911688357632 state:THREAD_INIT  | tid:139911679903488 state:THREAD_INIT  | tid:139911671449344 state:THREAD_INIT  | tid:139911662995200 state:THREAD_INIT  | tid:139911176849152 state:THREAD_INIT  | 
```
---
- 给予线程休息时间(做完一个任务后sleep(1))  task =20, thread =10
  - 结果: 线程全部有效唤醒!
```java
[DEBUG_2] [print_list:110:list_t.c] 线程/任务列表:tid:140132566763264 state:THREAD_IDLE  | tid:140132558309120 state:THREAD_IDLE  | tid:140132549854976 state:THREAD_IDLE  | tid:140132541400832 state:THREAD_IDLE  | tid:140132532946688 state:THREAD_IDLE  | tid:140132524492544 state:THREAD_IDLE  | tid:140132516038400 state:THREAD_IDLE  | tid:140132166338304 state:THREAD_IDLE  | tid:140132157884160 state:THREAD_IDLE  | tid:140132149430016 state:THREAD_IDLE  | 
```