#include "log.h"

#include <errno.h>
#include <error.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_BUFFER_MAXSZ 2048

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t thread_log_mutex = PTHREAD_MUTEX_INITIALIZER;

__thread char log_buffer[LOG_BUFFER_MAXSZ] = {0};

const char* get_prefix(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(log_buffer, LOG_BUFFER_MAXSZ, fmt, ap);
  va_end(ap);

  return log_buffer;
}

void log_helper(enum LOG_SEVERITY severity,
                const char* prefix,
                const char* fmt,
                ...) {
  int len = strlen(log_buffer);
  log_buffer[len] = ' ';
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(log_buffer + len + 1, LOG_BUFFER_MAXSZ - len - 1, fmt, ap);
  va_end(ap);
  if (errno >= 0) {
    len = strlen(log_buffer);
    if (LOG_BUFFER_MAXSZ - len > 0) {
      if (errno == 0) {
        // 自定义error
        if (severity == _LOG_ERR) {
          snprintf(log_buffer + len, LOG_BUFFER_MAXSZ - len, "\t\t:%s",
                   "Not built-in Error");
        }
      } else {
        snprintf(log_buffer + len, LOG_BUFFER_MAXSZ - len, ":%s",
                 strerror(errno));
      }
    }
  }

  pthread_mutex_lock(&log_mutex);
  switch (severity) {
    case _LOG_INFO:
      fprintf(stdout, "[%s] %s", "INFO", log_buffer);
      fprintf(stdout, "\n");
      break;
    case _LOG_WARN:
      fprintf(stdout, "[%s] %s", "WARN", log_buffer);
      fprintf(stdout, "\n");
      break;
    case _LOG_DEBUG:
#ifdef DEBUG
      fprintf(stdout, "[%s] %s", "DEBUG", log_buffer);
      fprintf(stdout, "\n");
#else
      pthread_mutex_unlock(&log_mutex);
      LOG(_LOG_WARN, "DEBUG Not open");
#endif
      break;
    case _LOG_DEBUG_2:
#ifdef DEBUG_2
      fprintf(stdout, "[%s] %s", "DEBUG_2", log_buffer);
      fprintf(stdout, "\n");
#else
      pthread_mutex_unlock(&log_mutex);
      LOG(_LOG_WARN, "DEBUG_2 Not open");
#endif
      break;
    case _LOG_ERR:
      fprintf(stderr, "[%s] %s", "ERROR", log_buffer);
      exit(EXIT_FAILURE);
    default:
      fprintf(stderr, "[%s] %s", "???", log_buffer);
      exit(EXIT_FAILURE);
  }
  pthread_mutex_unlock(&log_mutex);
}