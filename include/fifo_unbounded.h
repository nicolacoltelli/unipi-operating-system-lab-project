#ifndef FIFO_UNBOUNDED_H_
#define FIFO_UNBOUNDED_H_

#include <pthread.h>

typedef struct __linked_list{
  struct __node* head;
  struct __node* tail;
  int count;
} fifo_unbounded_t;

struct __node{
    void* elem;
    struct __node* next;
};


//Macro to statically initialize fifo_unbounded_t
#define FIFO_INITIALIZER {NULL, NULL, 0}

/*
 * \brief Dynamic initialization of fifo_unbounded_t
 * \param fifo : pointer to double_ended linked list
 */
int fifo_init(fifo_unbounded_t* fifo);

/*
 * \brief Insert a new element at the tail of the linked list
 * \param fifo : pointer to indexes of double_ended linked list
 * \param elem : new element to be inserted
 * \param mutex: mutual exclusion for sharing fifo between threads
 * \param empty: condition variable to allow pop_fifo() to wait for
 *                push when fifo is empty
 */
void push_fifo(fifo_unbounded_t* fifo, void* elem, pthread_mutex_t* mutex, pthread_cond_t* empty);

/*
 * \brief Remove and return the element at the head of the linked list
 * \param fifo : pointer to indexes of double_ended linked list
 * \param mutex: mutual exclusion for sharing fifo between threads
 * \param empty: condition variable to allow pop_fifo() to wait for
 *                push when fifo is empty
 */
void* pop_fifo(fifo_unbounded_t* fifo, pthread_mutex_t* mutex, pthread_cond_t* empty);

/*
 * \brief Remove, if present, remaining elements
 * \param fifo : pointer to pointer to double_ended linked list
 */
void free_fifo(fifo_unbounded_t* fifo);

/*
 * \brief returns the number of elements inside the fifo
 * \param fifo : pointer to pointer to double_ended linked list
 * \param mutex: mutual exclusion for sharing fifo between threads
 */
int get_count_fifo(fifo_unbounded_t* fifo, pthread_mutex_t* mutex);

#endif
