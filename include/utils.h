#ifndef UTILS_H_
#define UTILS_H_

#include <stdio.h>
#include <fifo_unbounded.h>
#include <signal.h>

typedef struct __queue{
  fifo_unbounded_t* fifo;
  pthread_mutex_t* mutex;
  pthread_cond_t* empty;
}queue_t;

struct __xlog{
  FILE* file;
  pthread_mutex_t* mutex;
};

#define ERROR_AT fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);

#define XLOCK(mutex_address) CHECK_ERR(pthread_mutex_lock(mutex_address), "lock");
#define XUNLOCK(mutex_address) CHECK_ERR(pthread_mutex_unlock(mutex_address), "unlock");
#define XWAIT(cond_address, mutex_address) CHECK_ERR(pthread_cond_wait(cond_address, mutex_address), "cond wait");
#define XSIGNAL(cond_address) CHECK_ERR(pthread_cond_signal(cond_address), "cond signal");


#define SYS_CALL_RETURN(r,c,e) {         \
            errno = 0;                   \
            if ((r=c)==-1) {             \
              perror(e);                 \
              ERROR_AT;                  \
              exit(errno);               \
            }                            \
          }

#define SYS_CALL(c,e) {                  \
            errno = 0;                   \
            if (c==-1){                  \
              perror(e);                 \
              ERROR_AT;                  \
              exit(errno);               \
            } \
          }

#define CHECK_PTR(ptr, string, post) {   \
            if(ptr==NULL){               \
              puts(#string);             \
              ERROR_AT;                  \
              post;                      \
            }                            \
          }

#define CHECK_ERR(func, string) {        \
            if(func){                    \
              perror(#string);           \
              ERROR_AT;                  \
              exit(EXIT_FAILURE);        \
            }                            \
          }

#define CHECK_PTHREAD_CREATE(func, thread, post) {                           \
            if(func){                                                        \
              perror(NULL);                                                  \
              ERROR_AT;                                                      \
              fprintf(stderr, "Error while creating %s thread\n", thread);   \
              post;                                                          \
            }                                                                \
          }

#define CHECK_PTHREAD_JOIN(func, thread, post) {                             \
            if(func){                                                        \
              perror(NULL);                                                  \
              ERROR_AT;                                                      \
              fprintf(stderr, "Error while joining %s thread\n", thread);    \
              post;                                                          \
            }                                                                \
          }

#ifndef DEBUG
#define DEBUG 0
#endif

#define CLOSE 0
#define OPEN 1

#define THOUSAND 1000
#define MILLION 1000000
#define BILLION 1000000000L

void* xmalloc(size_t bytes);

int my_strtoi(char* string);

void nanotimer(int microsecs);

void timespec_diff(struct timespec* start, struct timespec* stop, struct timespec* result);

#endif
