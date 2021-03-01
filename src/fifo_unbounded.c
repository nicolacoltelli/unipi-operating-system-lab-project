#include <fifo_unbounded.h>
#include <pthread.h>
#include <stdlib.h>
#include <utils.h>
#include <stdio.h>

int fifo_init(fifo_unbounded_t* fifo){

  fifo->head = NULL;
  fifo->tail = NULL;
  fifo->count = 0;

  return 0;

}


void push_fifo(fifo_unbounded_t* fifo, void* elem, pthread_mutex_t* mutex, pthread_cond_t* empty){

  struct __node* new_node = xmalloc(sizeof(struct __node));
  new_node->elem = elem;
  new_node->next = NULL;

  if (mutex) pthread_mutex_lock(mutex);

  //If is empty
  if (!fifo->head){
    fifo->head = new_node;
  } else {
    (fifo->tail)->next = new_node;
  }

  fifo->tail = new_node;

  fifo->count++;

  if (empty) pthread_cond_signal(empty);

  if (mutex) pthread_mutex_unlock(mutex);


}


void* pop_fifo(fifo_unbounded_t* fifo, pthread_mutex_t* mutex, pthread_cond_t* empty){

  if (mutex) pthread_mutex_lock(mutex);

  while (!fifo->head && mutex && empty){
    pthread_cond_wait(empty, mutex);
  }

  if (!fifo->head && (!mutex || !empty)){
    return NULL;
  }

  void* res = (fifo->head)->elem;

  struct __node* temp = fifo->head;
  fifo->head = (fifo->head)->next;
  if (!fifo->head) fifo->tail = NULL;

  fifo->count--;

  if (mutex) pthread_mutex_unlock(mutex);

  free(temp);

  return res;

}


void free_fifo(fifo_unbounded_t* fifo){

  while(fifo->head){
    struct __node* temp = fifo->head;
    fifo->head = fifo->head->next;
    free(temp->elem);
    free(temp);
  }

}


int get_count_fifo(fifo_unbounded_t* fifo, pthread_mutex_t* mutex){

  if (mutex) pthread_mutex_lock(mutex);
  int res = fifo->count;
  if (mutex) pthread_mutex_unlock(mutex);

  return res;

}
