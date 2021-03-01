#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <cashier.h>
#include <customer.h>
#include <supermarket.h>
#include <utils.h>


customer_t* customer_init(int id, struct __all_cashiers* all_cashiers,
           struct __customers_counter* customers_counter, queue_t* director_permissions_list,
           struct __xlog* log, int max_fixed_time_to_shop, int max_fixed_products_count,
           unsigned int* supermarket_seed, struct __xlog* supermarket_log){

  struct __customer_args* args = xmalloc(sizeof(struct __customer_args));
  args->id = id;
  args->time_to_shop = MIN_FIXED_TIME_TO_SHOP + ( rand_r(supermarket_seed)
                    % (max_fixed_time_to_shop-MIN_FIXED_TIME_TO_SHOP) );
  args->products_count = rand_r(supermarket_seed) % max_fixed_products_count;
  args->all_cashiers = all_cashiers;
  args->customers_counter = customers_counter;
  args->director_permissions_list = director_permissions_list;
  args->log = log;
  args->supermarket_log = supermarket_log;

  customer_t* res = xmalloc(sizeof(customer_t));
  res->id = id;
  res->thread = 0;

  CHECK_PTHREAD_CREATE( pthread_create(&(res->thread), NULL, customer, args),
              "customer", free(res); return res );

  XLOCK(customers_counter->mutex);
  (*(customers_counter->count))++;
  XUNLOCK(customers_counter->mutex);

  return res;

}


void customer_cleanup(void* args_pointer){

  struct __customer_args* args = (struct __customer_args*)args_pointer;

  struct __customers_counter* cc = args->customers_counter;
  free(args);

  XLOCK(cc->mutex);
  (*(cc->count))--;
  XSIGNAL(cc->full);
  XUNLOCK(cc->mutex);

}


void* customer(void* args_pointer){

  //Customers thread are detached because they
  //  terminate asynchronously to the supermarket.
  pthread_detach(pthread_self());

  struct __customer_args* args = (struct __customer_args*)args_pointer;

  XLOCK(args->log->mutex);
  fprintf(args->log->file, "Customer thread %d started (TID: %ld)\n",
              args->id, pthread_self());
  XUNLOCK(args->log->mutex);

  //As specific, we need to keep track of the time the
  //  customer spends inside the supermarket and log it.
  //This clock_gettime() will be paired with the one of the two
  //  below '//*' comment. One in case the customer
  //  has 0 products, the other otherwise.
  struct timespec time_entered;
  SYS_CALL(clock_gettime(CLOCK_REALTIME, &time_entered), "clock_gettime");

  nanotimer(args->time_to_shop);

  //If the customer buys 0 products, it doesn't go to a
  //  cashier and, instead, asks the director for permission. 
  if (args->products_count == 0){

    pthread_cleanup_push(customer_cleanup, args_pointer);

    struct timespec time_queue_in;
    struct timespec time_queue_out;

    int permission_status = 0;
    pthread_mutex_t permission_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t no_permission = PTHREAD_COND_INITIALIZER;

    if (!sigquit_status){

      XLOCK(args->log->mutex);
      fprintf(args->log->file, "Customer %d has 0 products and is asking the "
        "director for permission to exit (TID: %ld)\n", args->id, pthread_self());
      XUNLOCK(args->log->mutex);

      struct __permission_request* new_request = xmalloc(sizeof(struct __permission_request));
      new_request->status = &permission_status;
      new_request->mutex = &permission_mutex;
      new_request->no_permission = &no_permission;
      new_request->time_permission_received = &time_queue_out;

      //As specific, we need to keep track of the time the
      //  customer spends inside the queue(s) and log it.
      //If the customer has 0 products, we log the time we
      //  spend waiting for the director's permission.
      //This clock_gettime() will be paired with the one
      //  below '//**' comment
      SYS_CALL(clock_gettime(CLOCK_REALTIME, &time_queue_in), "clock_gettime");

      //Sending the permission request to the director.
      push_fifo(args->director_permissions_list->fifo, new_request,
        args->director_permissions_list->mutex, args->director_permissions_list->empty);

      XLOCK(&permission_mutex);
      while(!permission_status){
        XWAIT(&no_permission, &permission_mutex);
      }
      XUNLOCK(&permission_mutex);

      //**
      SYS_CALL(clock_gettime(CLOCK_REALTIME, &time_queue_out), "clock_gettime");

      XLOCK(args->log->mutex);
      fprintf(args->log->file, "Customer  %d has received permission from "
                  "director to exit (TID: %ld)\n", args->id, pthread_self());
      XUNLOCK(args->log->mutex);

    }

    //*
    struct timespec time_exited;
    SYS_CALL(clock_gettime(CLOCK_REALTIME, &time_exited), "clock_gettime");

    struct timespec time_in_supermarket;
    timespec_diff(&time_entered, &time_exited, &time_in_supermarket);

    struct timespec time_in_queue;
    if (permission_status){
      timespec_diff(&time_queue_in, &time_queue_out, &time_in_queue);
    } else {
      time_in_queue.tv_sec = 0;
      time_in_queue.tv_nsec = 0;
    }

    //Writing logs requested by specific
    XLOCK(args->supermarket_log->mutex);
    fprintf(args->supermarket_log->file, "C\t%d", args->id);
    fprintf(args->supermarket_log->file, "\t%ld.%03lu",
              time_in_supermarket.tv_sec, time_in_supermarket.tv_nsec/MILLION);
    fprintf(args->supermarket_log->file, "\t%ld.%03lu",
              time_in_queue.tv_sec, time_in_queue.tv_nsec/MILLION);
    //0 queues changed, 0 products bought
    fprintf(args->supermarket_log->file, "\t0\t0\n");
    XUNLOCK(args->supermarket_log->mutex);

    XLOCK(args->log->mutex);
    fprintf(args->log->file, "Customer %d exiting the supermarket... (TID: %ld)\n",
              args->id, pthread_self());
    XUNLOCK(args->log->mutex);

    pthread_cleanup_pop(1);
    return NULL;

  }

  //Copies needed to correctly signal the director
  //Must be declared here beacuse of the implementation
  //  of the cleanup marcro as '{...}'
  struct __customers_counter* customers_counter_copy = args->customers_counter;
  queue_t* temp_director_permissions_list = args->director_permissions_list;
  pthread_cleanup_push(customer_cleanup, args_pointer);

  unsigned int seed = time(NULL);

  int changed_queues_count = 0;
  struct timespec time_queue_in;
  struct timespec time_queue_out;

  //RESPONSE = -2 => sigquit received
  //RESPONSE = -1 => no response yet
  //RESPONSE =  0 => customer changed queue
  //RESPONSE =  1 => customer correctly served
  int response = -1;
  pthread_mutex_t response_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t no_response = PTHREAD_COND_INITIALIZER;

  while (response != 1  && !sigquit_status) {

    //The following struct will be sent to the cashier.
    //Data will be used for computation, signal back the
    //  result, and logging.
    response = -1;
    struct __customer_at_cashier* new_customer = xmalloc(sizeof(struct __customer_at_cashier));
    new_customer->id = args->id;
    new_customer->products_count = args->products_count;
    new_customer->response = &response;
    new_customer->response_mutex = &response_mutex;
    new_customer->no_response = &no_response;
    new_customer->time_queue_out = &time_queue_out;

    //Choosing a random cashier.
    //This algorithm is highly inefficient, specially in the case where
    //  there are only few cashiers open and a lot closed.
    //A better solution (in particular if we want to scale up the program) would
    //  be to map in an array only the opened cashiers and than choose a random index.
    //Another version of the algorithm would be to choose a random starting index,
    //  and then increment it until we find an open cashier. This algorithm is way
    //  faster but has a very high probability to create very long queues inside a
    //  single cashier.
    int index = rand_r(&seed) % args->all_cashiers->count;
    cashier_t* current_cashier = (args->all_cashiers->cashiers_list)[0];

    int found = 0;
    while(!found){

      current_cashier = (args->all_cashiers->cashiers_list)[index];

      XLOCK(current_cashier->status_mutex);
      if (*(current_cashier->status) == OPEN){
        found = 1;
      } else {
        XUNLOCK(current_cashier->status_mutex);
        index = rand_r(&seed) % args->all_cashiers->count;
      }

    }

    XLOCK(args->log->mutex);
    fprintf(args->log->file, "Customer %d going to pay at cash %d (TID: %ld)\n",
                args->id, index, pthread_self());
    XUNLOCK(args->log->mutex);

    queue_t* current_queue = current_cashier->queue;

    //As specific, we need to keep track of the time the customer spends inside the
    //  queue(s) and log it. We only memorize the time the first time we enter a queue.
    //This clock_gettime() will be paired with the one inside the cashier
    //  that will serve this customer. If the customer is not served, 0 will be printed.
    if (changed_queues_count == 0){
      SYS_CALL(clock_gettime(CLOCK_REALTIME, &time_queue_in), "clock_gettime");
    }

    //Sending the data to the choosen queue to be served.
    push_fifo(current_queue->fifo, new_customer, current_queue->mutex, current_queue->empty);

    XUNLOCK(current_cashier->status_mutex);

    //Waiting for the cashier to write a response (or the director if the
    //  the cashier is closed in the meantime)
    XLOCK(&response_mutex);
    while (response == -1){
      XWAIT(&no_response, &response_mutex);
    }
    XUNLOCK(&response_mutex);

    if (response == 0){
      changed_queues_count++;
      XLOCK(args->log->mutex);
      fprintf(args->log->file, "Customer %d has changed queue... (TID: %ld)\n",
                  args->id, pthread_self());
      XUNLOCK(args->log->mutex);
    }

  }

  //*
  struct timespec time_exited;
  SYS_CALL(clock_gettime(CLOCK_REALTIME, &time_exited), "clock_gettime");

  struct timespec time_in_supermarket;
  timespec_diff(&time_entered, &time_exited, &time_in_supermarket);

  struct timespec time_in_queue = {0,0};
  int customer_bought_products_count = 0;

  //Compute the time spent in queue only if the customer has benn server.
  //  Otherwise, 0 is assigned.
  if (response == 1){
    timespec_diff(&time_queue_in, &time_queue_out, &time_in_queue);
    customer_bought_products_count = args->products_count;
  }

  //Writing logs requested by specific.
  XLOCK(args->supermarket_log->mutex);
  fprintf(args->supermarket_log->file, "C\t%d", args->id);
  fprintf(args->supermarket_log->file, "\t%ld.%03lu",
            time_in_supermarket.tv_sec, time_in_supermarket.tv_nsec/MILLION);
  fprintf(args->supermarket_log->file, "\t%ld.%03lu",
            time_in_queue.tv_sec, time_in_queue.tv_nsec/MILLION);
  fprintf(args->supermarket_log->file, "\t%d", changed_queues_count);
  fprintf(args->supermarket_log->file, "\t%d\n", customer_bought_products_count);
  XUNLOCK(args->supermarket_log->mutex);

  XLOCK(args->log->mutex);
  if (response == 1){
    fprintf(args->log->file, "Customer %d exiting the "
              "supermarket... (TID: %ld)\n", args->id, pthread_self());
  } else {
    fprintf(args->log->file, "Customer %d exiting the supermarket "
              "because of sigquit signal... (TID: %ld)\n", args->id, pthread_self());
  }
  XUNLOCK(args->log->mutex);

  pthread_cleanup_pop(1);

  //Even if we have more than 0 products, we still signal
  //  the director so he doesn't get stuck waiting when
  //  every customer got out
  XLOCK(customers_counter_copy->mutex);
  if (!sigquit_status || (sigquit_status && *customers_counter_copy->count > 0) ){
    push_fifo(temp_director_permissions_list->fifo, NULL,
      temp_director_permissions_list->mutex, temp_director_permissions_list->empty);
  }
  XUNLOCK(customers_counter_copy->mutex);

  return NULL;

}
