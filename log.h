#ifndef LOG_H
#define LOG_H

#include <error.h>
#include <pthread.h>

#define DEBUG_2
#define DEBUG

/*deprecated*/
/* 单独一个线程的log锁(基于thread_routine)没有意义,因为还有主线程要记录log,如果完善的话,可能需要记录log来源,然后分类*/
/* 现在可以做到LOG函数是原子的*/
/* 接下来可能希望对LOG的内容按来源排一下序之类的*/
extern pthread_mutex_t thread_log_mutex;

enum LOG_SEVERITY {
  _LOG_INFO = 1,
  _LOG_WARN = 2,
  _LOG_ERR = 3,
  _LOG_DEBUG = 4,
  _LOG_DEBUG_2 = 5,  // 详细debug,默认关闭
};

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

#endif

// 解决并行问题