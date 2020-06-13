#include "list_t.h"

#include <stdio.h>
#include <string.h>

#include "log.h"
#include "threadpool.h"
#define MAX_Q_SIZE 64

// 使用复制的方式构造elem,而不能直接使用elem的指针
// 浅复制,需要保证elem各字段是一直生存的
// elem是list_node_t的data字段
void* push(list_t* list, void* elem) {
  pthread_mutex_lock(&(list->mutx));

  list_node_t* new_node = calloc(1, sizeof(list_node_t));
  void* data = calloc(1, list->data_size);
  memcpy(data, elem, list->data_size);
  new_node->data = data;
  new_node->next = NULL;

  list->tail->next = new_node;
  list->tail = new_node;
  list->total_size++;

  print_list(list);

  pthread_mutex_unlock(&(list->mutx));

  if (list->total_size >= MAX_Q_SIZE) {
    LOG(_LOG_WARN, "%s List Size > %d",
        sizeof(*elem) == sizeof(task_t) ? "Task" : "Thread", MAX_Q_SIZE);
  }
  return new_node->data;
}

void* pop(list_t* list, void* elem) {
  pthread_mutex_lock(&(list->mutx));

  if (list->total_size == 0) {
    LOG(_LOG_ERR, "POP ERROR");
    return (void*)-1;
  }

#ifdef DEBUG
  if (list->total_size == 0 && list->head != list->tail) {
    LOG(_LOG_ERR, "list size == 0 while head != tail");
  }
  if (list->head == list->tail && list->total_size != 0) {
    LOG(_LOG_ERR, "list head == tail while size != 0");
  }
#endif

  list_node_t* pop_elem = list->head->next;
  list->head->next = pop_elem->next;
  list->total_size--;

  if (list->total_size == 0) {
    list->tail = list->head;
  }

  if (elem)
    memcpy(elem, pop_elem->data, list->data_size);

  free(pop_elem->data);
  free(pop_elem);

  pthread_mutex_unlock(&(list->mutx));
  return (void*)0;
}

void* list_init(list_t* list, int data_size) {
  pthread_mutex_init(&list->mutx, NULL);
  pthread_rwlock_init(&list->rw_lock, NULL);
  list->data_size = data_size;
  list->head = calloc(1, sizeof(data_size));
  list->head->next = NULL;
  list->tail = list->head;
  list->total_size = 0;
}

// concurrency safe
int get_list_size(list_t* list) {
  pthread_rwlock_rdlock(&list->rw_lock);
  int size = list->total_size;
  pthread_rwlock_unlock(&list->rw_lock);
  return size;
}

void print_list(list_t* list) {
  char buf[2048] = {0};
  sprintf(buf, "线程/任务列表:");
  if (list->total_size == 0) {
    sprintf(buf + strlen(buf), " 空");
  } else {
    list_node_t* ptr = list->head;
    while (ptr->next) {
      if (list->data_size == sizeof(task_t)) {
        sprintf(buf + strlen(buf), "%s  ", ((task_t*)ptr->next->data)->fn_name);
      } else {
        thread_t* thread = (thread_t*)ptr->next->data;
        char state_buf[20];
        str_thread_state(thread->state, state_buf);
        sprintf(buf + strlen(buf), "tid:%ld state:%s  | ", thread->tid,
                state_buf);
      }
      ptr = ptr->next;
    }
  }
  LOG(_LOG_DEBUG_2, buf);
}