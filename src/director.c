#include <string.h>
#include <stdlib.h>

#include <time.h>
#include <errno.h>
#include <unistd.h>

#include <supermarket.h>
#include <director.h>
#include <cashier.h>
#include <customer.h>
#include <utils.h>
#include <fifo_unbounded.h>


director_t* director_init(struct __all_cashiers* all_cashiers, queue_t* director_permissions_list,
                struct __customers_counter* customers_counter, pthread_t* entrance_thread,
                FILE* log, struct __cashiers_handler_args* cashiers_handler_args){

  struct __director_args* args = xmalloc(sizeof(struct __director_args));
  args->all_cashiers = all_cashiers;
  args->customers_counter = customers_counter;
  args->director_permissions_list = director_permissions_list;
  args->entrance_thread = entrance_thread;
  args->cashiers_handler_args = cashiers_handler_args;
  args->log = log;

  director_t* res = xmalloc(sizeof(director_t));
  res->thread = 0;

  CHECK_PTHREAD_CREATE( pthread_create(&(res->thread), NULL, director, args),
              "director", exit(EXIT_FAILURE) );

  return res;

}


void director_join(director_t* director){

  CHECK_PTHREAD_JOIN(pthread_join(director->thread, NULL),
                "director", exit(EXIT_FAILURE));

  free(director);

}


void* director(void* args_pointer){

  struct __director_args* args = (struct __director_args*)args_pointer;

  pthread_t cashiers_handler_thread;
  CHECK_PTHREAD_CREATE(pthread_create(&cashiers_handler_thread, NULL, cashiers_handler, args),
              "cashier handler", exit(EXIT_FAILURE));

  //This loop is used to give permission to exit the supermarket to customers with
  //  0 products. In case of sigquit, we break out immediately, otherwise
  //  we wait until every customer is out.
  while( !sigquit_status && (!sighup_status || *(args->customers_counter->count) > 0) ){

    struct __permission_request* req = pop_fifo(args->director_permissions_list->fifo,
      args->director_permissions_list->mutex, args->director_permissions_list->empty);

    if (req){

      SYS_CALL(clock_gettime(CLOCK_REALTIME, req->time_permission_received), "clock_gettime");

      XLOCK(req->mutex);
      *(req->status) = 1;
      XSIGNAL(req->no_permission);
      XUNLOCK(req->mutex);
      free(req);

    }

  }

  //Entrance might be waiting for a customer signal to
  //  check if it should allow someone to enter, so we
  //  signal it to wake up before joining
  XLOCK(args->customers_counter->mutex);
  XSIGNAL(args->customers_counter->full);
  XUNLOCK(args->customers_counter->mutex);
  CHECK_PTHREAD_JOIN(pthread_join(*(args->entrance_thread), NULL),
                "entrance", exit(EXIT_FAILURE));

  if (sigquit_status){

    XLOCK(args->customers_counter->mutex);
    while(*(args->customers_counter->count) > 0){
      XWAIT(args->customers_counter->full, args->customers_counter->mutex);
    }
    XUNLOCK(args->customers_counter->mutex);

  }

  CHECK_PTHREAD_JOIN(pthread_join(cashiers_handler_thread, NULL),
                "cashier handler",  exit(EXIT_FAILURE));
  
  free_fifo(args->director_permissions_list->fifo);
  free(args->director_permissions_list->fifo);
  free(args->director_permissions_list->mutex);
  free(args->director_permissions_list->empty);

  free(args);

  return NULL;

}


void* cashiers_handler(void* args_pointer){

  struct __director_args* args = (struct __director_args*)args_pointer;

  unsigned int seed = time(NULL);

  int currently_open = args->cashiers_handler_args->initial_open_cashiers;

  //The cashiers_map will be used to keep track of which cashier are
  //  open at a certain time.
  int* cashiers_map = xmalloc(sizeof(int)*args->all_cashiers->count);
  for (int i = 0; i<args->all_cashiers->count; i++){
    if (i < args->cashiers_handler_args->initial_open_cashiers){
      cashiers_map[i] = OPEN;
    } else {
      cashiers_map[i] = CLOSE;
    }
  }

  while(!sighup_status && !sigquit_status){

    if (DEBUG>=2) printf("-->>Currently open: %d\n"
                         "----------------------\n", currently_open);

    int above_max = 0;
    int below_min = 0;

    for (int i = 0; i<args->all_cashiers->count; i++){

      struct __cashier_director_comm* curr_queue = (args->all_cashiers->cashiers_list)[i]->queue_customers_count;

      //for each cashier, reads the number of customers in queue from
      //  shared memory with cashiers. The value is always up
      //  to date because the cashier overwrites the old one.
      XLOCK(&curr_queue->mutex);
      while (curr_queue->count == -1 && !sighup_status && !sigquit_status){
        XWAIT(&curr_queue->old_value, &curr_queue->mutex);
      }
      int buffer = curr_queue->count;
      curr_queue->count = -1;
      XUNLOCK(&curr_queue->mutex);

      if (sighup_status || sigquit_status) break;

      //If current cashier is close, skip
      if (cashiers_map[i] == OPEN){
        if (buffer<=args->cashiers_handler_args->director_too_few_customers) below_min++;
        if (buffer>=args->cashiers_handler_args->director_too_many_customers) above_max++;
      }

      if (DEBUG>=2){
        if (cashiers_map[i] == OPEN) printf("%d: %d\n", i, buffer);
        else printf("%d: CLOSE\n", i);
      }

    }

    if (sighup_status || sigquit_status) break;

    if (DEBUG>=2) printf("\nbelow_min: %d\tabove_max: %d\n", below_min, above_max);
    if (DEBUG>=2) puts("====================\n");

    //To make the supermarket more dynamic, on each turn this
    //  "cashiers scheduler" can only close or open a cash desk.
    //It wouldn't be a rational decision to both close a cash desk
    //  and open another one at the same time
    //We give priority to open a new cash desk.
    if (above_max>=below_min){

      //Choose a random queue to open (it they are not all open)
      //One new queue is also opened if less then DIRECTOR_ABOVE_MAX_LIMIT
      //  queues are above_max (but one is) and the total of open queue is
      //  less then DIRECTOR_ABOVE_MAX_LIMIT
      if (( above_max>=args->cashiers_handler_args->director_above_max_limit
                && currently_open!=args->all_cashiers->count)
        ||( above_max>0
                && currently_open<args->cashiers_handler_args->director_above_max_limit ) ){

        int index = rand_r(&seed) % args->all_cashiers->count;
        while(cashiers_map[index] == OPEN){
          index++;
          index %= args->all_cashiers->count;
        }

        cashier_t* current_cashier = (args->all_cashiers->cashiers_list)[index];

        XLOCK(current_cashier->status_mutex);
        *(current_cashier->status) = OPEN;
        XSIGNAL(current_cashier->status_closed);
        XUNLOCK(current_cashier->status_mutex);

        cashiers_map[index] = OPEN;
        currently_open++;

      }

    } else {

      //Choose a random queue to close (if there is at least one still open)
      if ( below_min>=args->cashiers_handler_args->director_below_min_limit && currently_open>1 ){

        int index = rand_r(&seed) % args->all_cashiers->count;
        while(cashiers_map[index] == CLOSE){
          index++;
          index %= args->all_cashiers->count;
        }

        XLOCK((args->all_cashiers->cashiers_list)[index]->status_mutex);
        *((args->all_cashiers->cashiers_list)[index]->status) = CLOSE;
        XUNLOCK((args->all_cashiers->cashiers_list)[index]->status_mutex);
        cashiers_map[index] = CLOSE;
        currently_open--;

        queue_t* current_queue = (args->all_cashiers->cashiers_list)[index]->queue;

        //Emptying the queue
        XLOCK(current_queue->mutex);

        //No need to pass mutex as param since we
        //  already manually locked it outside
        int count = get_count_fifo(current_queue->fifo, NULL);
        for (int i = 0; i<count; i++){

          struct __customer_at_cashier* customer = pop_fifo(current_queue->fifo, NULL, NULL);

          if (customer) {

            XLOCK(customer->response_mutex);
            *(customer->response) = 0;
            XSIGNAL(customer->no_response);
            XUNLOCK(customer->response_mutex);
          
            free(customer);
          
          }

        }

        //"wake up" element in case cashier is stuck waiting
        push_fifo(current_queue->fifo, NULL, NULL, current_queue->empty);

        XUNLOCK(current_queue->mutex);

      }

    }

  }

  //Signaling cashiers that might be stuck because they are closed
  for (int i=0; i<args->all_cashiers->count; i++){

    cashier_t* current_cashier = (args->all_cashiers->cashiers_list)[i];
    
    XLOCK(current_cashier->status_mutex);
    XSIGNAL(current_cashier->status_closed);
    XUNLOCK(current_cashier->status_mutex);
 
  }

  if (DEBUG) {
    for (int i=0; i<args->all_cashiers->count; i++){
      printf("%d", cashiers_map[i]);
      if (i != args->all_cashiers->count-1) printf(" | ");
    }
    puts("");
  }

  free(cashiers_map);

  //Waking up the director in case he is waiting giving
  //  permissions to customers
  push_fifo(args->director_permissions_list->fifo, NULL,
    args->director_permissions_list->mutex, args->director_permissions_list->empty);

  return NULL;

}
