#include <utils.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

void* xmalloc(size_t bytes){

  void* res;

  if ((res = malloc(bytes))==NULL){
    fprintf(stderr, "Malloc couldn't allocate %ld bytes\n", bytes);
    exit(EXIT_FAILURE);
  }

  return res;

}

int my_strtoi(char* string){

  int res = 0;
  char* endPtr;
  errno = 0;
  res = strtol(string, &endPtr, 10);
  if (errno!=0){
    perror("strtol");
    return -1;
  }

  if (endPtr == string) puts("my_strtoi found no digits");

  return res;

}

void nanotimer(int microsecs){

  //Scaling microsecond requested as specific to nanoseconds
  struct timespec sleep_time;
  sleep_time.tv_sec = microsecs/THOUSAND;
  sleep_time.tv_nsec = (microsecs%THOUSAND)*MILLION;

  errno = 0;
  while(nanosleep(&sleep_time, &sleep_time)){
    if (errno != EINTR){
      fprintf(stderr, "Error during customer nanosleep. Some "
                      "timinig may not have been accurate\n");
      break;
    }
    errno = 0;
  }

}

void timespec_diff(struct timespec* start, struct timespec* stop, struct timespec* result){

  if ((stop->tv_nsec - start->tv_nsec) < 0) {
    result->tv_sec = stop->tv_sec - start->tv_sec - 1;
    result->tv_nsec = BILLION + stop->tv_nsec - start->tv_nsec;
  } else {
    result->tv_sec = stop->tv_sec - start->tv_sec;
    result->tv_nsec = stop->tv_nsec - start->tv_nsec;
  }

}
