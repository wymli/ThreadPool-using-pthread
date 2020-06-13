#ifndef LIST_H
#define LIST_H
#include <pthread.h>


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

// deprecated; because cant let tail points to head in static init
#define LIST_INITIALIZER(data_size)                        \
  {                                                        \
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_RWLOCK_INITIALIZER, \
        calloc(1, (data_size)), 0, 0, (data_size)          \
  }

#endif