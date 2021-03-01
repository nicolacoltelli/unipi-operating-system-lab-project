#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <supermarket.h>
#include <cashier.h>
#include <customer.h>
#include <fifo_unbounded.h>
#include <utils.h>


cashier_t* cashier_init(int id, int initial_open_cashiers, int variable_service_time, struct __xlog* log,
  int report_to_director_frequency, struct __customers_counter* customers_counter, unsigned int* supermarket_seed,
  struct __xlog* supermarket_log, int* served_customers_count, int* bought_products_count){

  //This queue is the one used by customers
  queue_t* queue = xmalloc(sizeof(queue_t));
  queue->fifo = xmalloc(sizeof(fifo_unbounded_t));
  fifo_init(queue->fifo);
  queue->mutex = xmalloc(sizeof(pthread_mutex_t));
  CHECK_ERR(pthread_mutex_init(queue->mutex, NULL), "mutex init");
  queue->empty = xmalloc(sizeof(pthread_cond_t));
  CHECK_ERR(pthread_cond_init(queue->empty, NULL), "cond init");

  //Status must be protected by mutex because
  //  it can be modified by the cashiers handler. 
  int* status = xmalloc(sizeof(int));
  if (id<initial_open_cashiers){
    *(status) = OPEN;
  } else {
    *(status) = CLOSE;
  }
  pthread_mutex_t* status_mutex = xmalloc(sizeof(pthread_mutex_t));
  CHECK_ERR(pthread_mutex_init(status_mutex, NULL), "mutex init");
  pthread_cond_t* status_closed = xmalloc(sizeof(pthread_cond_t));
  CHECK_ERR(pthread_cond_init(status_closed, NULL), "cond init");

  //This struct is shared between the cashiers
  //  handler and the report to director thread.
  struct __cashier_director_comm* queue_customers_count = xmalloc(sizeof(struct __cashier_director_comm));
  queue_customers_count->count = -1;
  queue_customers_count->mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
  queue_customers_count->old_value = (pthread_cond_t)PTHREAD_COND_INITIALIZER;


  struct __cashier_args* args = xmalloc(sizeof(struct __cashier_args));
  args->id = id;
  args->fixed_service_time = MIN_FIXED_SERVICE_TIME + ( rand_r(supermarket_seed)
                          % (MAX_FIXED_SERVICE_TIME - MIN_FIXED_SERVICE_TIME) );
  args->variable_service_time = variable_service_time;
  args->queue = queue;
  args->status = status;
  args->status_mutex = status_mutex;
  args->status_closed = status_closed;
  args->queue_customers_count = queue_customers_count;
  args->report_to_director_frequency = report_to_director_frequency;
  args->customers_counter = customers_counter;
  args->log = log;
  args->supermarket_log = supermarket_log;
  args->served_customers_count = served_customers_count;
  args->bought_products_count = bought_products_count;

  cashier_t* res = xmalloc(sizeof(struct __cashier));
  res->id = id;
  res->thread = 0;
  res->queue = queue;
  res->status = status;
  res->status_mutex = status_mutex;
  res->status_closed = status_closed;
  res->queue_customers_count = queue_customers_count;


  CHECK_PTHREAD_CREATE( pthread_create(&(res->thread), NULL, cashier, args),
              "cashier", free(res) );

  return res;

}


void cashier_join(cashier_t* cashier){

  push_fifo(cashier->queue->fifo, NULL, cashier->queue->mutex, cashier->queue->empty);
  CHECK_PTHREAD_JOIN(pthread_join(cashier->thread, NULL),
              "cashier", exit(EXIT_FAILURE));

  free_fifo(cashier->queue->fifo);
  free(cashier->queue->fifo);
  free(cashier->queue->mutex);
  free(cashier->queue->empty);
  free(cashier->queue);

  free(cashier->status);
  free(cashier->status_mutex);
  free(cashier->status_closed);

  free(cashier->queue_customers_count);

  free(cashier);

}


void cashier_cleanup(void* args_pointer){

  struct __cashier_cleanup_args* args = args_pointer;

  XLOCK(args->cashier_args->log->mutex);
  fprintf(args->cashier_args->log->file, "Cashier thread %d closing... (TID: %ld)\n",
              args->cashier_args->id, pthread_self());
  XUNLOCK(args->cashier_args->log->mutex);

  CHECK_PTHREAD_JOIN(pthread_join(*(args->report_to_director_thread), NULL),
              "report to director", exit(EXIT_FAILURE));

  free(args->report_to_director_thread);
  free(args->cashier_args);
  free(args);

}


void* cashier(void* args_pointer){

  struct __cashier_args* args = (struct __cashier_args*)args_pointer;

  XLOCK(args->log->mutex);
  fprintf(args->log->file, "Cashier thread %d started (TID: %ld)\n",
              args->id, pthread_self());
  XUNLOCK(args->log->mutex);

  // -- SETTING UP REPORT TO DIRECTOR THREAD --
  struct __report_to_director_args* report_to_director_args = xmalloc(sizeof(struct __report_to_director_args));
  report_to_director_args->queue_customers_count = args->queue_customers_count;
  report_to_director_args->frequency = args->report_to_director_frequency;
  report_to_director_args->queue = args->queue;

  pthread_t* report_to_director_thread = xmalloc(sizeof(pthread_t));

  CHECK_PTHREAD_CREATE( pthread_create(report_to_director_thread, NULL, report_to_director, report_to_director_args),
              "entrance", exit(EXIT_FAILURE) );
  // -------------------

  // -- SETTING UP CLEANUP FUNCTION --
  struct __cashier_cleanup_args* cleanup_args = xmalloc(sizeof(struct __cashier_cleanup_args));
  cleanup_args->cashier_args = args_pointer;
  cleanup_args->report_to_director_thread = report_to_director_thread;
  pthread_cleanup_push(cashier_cleanup, cleanup_args);
  // -------------------

  // -- LOG VARIABLES --
  int cashier_served_customers = 0;
  int cashier_elaborated_products = 0;
  int cashier_closures_count;
  struct timespec time_cashier_opened;
  struct timespec time_cashier_closed;
  SYS_CALL(clock_gettime(CLOCK_REALTIME, &time_cashier_opened), "clock_gettime");
  // -------------------

  XLOCK(args->status_mutex)
  if ( *(args->status) == OPEN ){
    cashier_closures_count = 0;
  } else {
    cashier_closures_count = -1;
  }
  XUNLOCK(args->status_mutex);

  //The cashiers will keep being open even after a signal has been
  //  to serve any remaining customer. 
  XLOCK(args->customers_counter->mutex);
  while((!sighup_status && !sigquit_status) || *(args->customers_counter->count)>0 ){
    XUNLOCK(args->customers_counter->mutex);

    XLOCK(args->status_mutex)
    while ( *(args->status) == OPEN ){
      XUNLOCK(args->status_mutex);

      struct __customer_at_cashier* customer = pop_fifo(args->queue->fifo,
                  args->queue->mutex, args->queue->empty);

      //If sigquit status has been received, we don't "serve" him
      //  and we just respond that a sigquit has been received.
      if (sigquit_status && customer){

        XLOCK(customer->response_mutex);
        *(customer->response) = -2;
        XSIGNAL(customer->no_response);
        XUNLOCK(customer->response_mutex);

        free(customer);

      } else if (customer){

        //Register the time the customer is popped from the queue
        //  as requested by specific. 
        SYS_CALL(clock_gettime(CLOCK_REALTIME, customer->time_queue_out), "clock_gettime");
        struct timespec time_customer_served_start;
        memcpy(&time_customer_served_start, customer->time_queue_out, sizeof(struct timespec));
        int customer_id = customer->id;
        int customer_products_count = customer->products_count;

        XLOCK(args->log->mutex);
        fprintf(args->log->file, "Cashier %d is serving customer %d (TID: %ld)\n",
                    args->id, customer->id, pthread_self());
        XUNLOCK(args->log->mutex);

        nanotimer(args->fixed_service_time +
                  args->variable_service_time * customer->products_count);

        //Write response to customer and signal him.
        XLOCK(customer->response_mutex);
        *(customer->response) = 1;
        XSIGNAL(customer->no_response);
        XUNLOCK(customer->response_mutex);

        XLOCK(args->supermarket_log->mutex);
        //+=1 beacuse compiler would warn with ++
        //  ("value computed is not used [-Wunused-value]")
        *(args->served_customers_count)+=1;
        *(args->bought_products_count)+=customer->products_count;
        XUNLOCK(args->supermarket_log->mutex);

        free(customer);

        //Compute time to serve customer for log file.
        struct timespec time_customer_served_end;
        SYS_CALL(clock_gettime(CLOCK_REALTIME, &time_customer_served_end), "clock_gettime");
        struct timespec time_to_serve;
        timespec_diff(&time_customer_served_start, &time_customer_served_end, &time_to_serve);

        XLOCK(args->supermarket_log->mutex);
        fprintf(args->supermarket_log->file,  "KC\t%d\t%d\t%ld.%03lu\n",
                  args->id, customer_id, time_to_serve.tv_sec, time_to_serve.tv_nsec/MILLION);
        XUNLOCK(args->supermarket_log->mutex);

        cashier_served_customers++;
        cashier_elaborated_products+=customer_products_count;

      }

      //This code will take care of the remaining clients
      //  once the supermarket entrance is closed
      XLOCK(args->customers_counter->mutex);
      if ( (sighup_status || sigquit_status) && (*args->customers_counter->count) == 0 ){
        XUNLOCK(args->customers_counter->mutex);
        //The following is a dummy lock used just to
        //  match the one in the loop condition
        XLOCK(args->status_mutex);
        break;
      }
      XUNLOCK(args->customers_counter->mutex);

      XLOCK(args->status_mutex)
    }
    XUNLOCK(args->status_mutex);

    if (sighup_status || sigquit_status){
      XLOCK(args->customers_counter->mutex);
      break;
    }

    //cashier_closures_count == -1 means that when the supermarket started
    //  the cashier was initialized closed, so we don't print the first
    //  workshift since the cashier hasn't really been opened yet.
    if (cashier_closures_count != -1){

      SYS_CALL(clock_gettime(CLOCK_REALTIME, &time_cashier_closed), "clock_gettime");
      struct timespec time_workshift;
      timespec_diff(&time_cashier_opened, &time_cashier_closed, &time_workshift);

      XLOCK(args->supermarket_log->mutex);
      fprintf(args->supermarket_log->file, "KS\t%d\t%ld.%03lu\n",
                args->id, time_workshift.tv_sec, time_workshift.tv_nsec/MILLION);
      XUNLOCK(args->supermarket_log->mutex);

      XLOCK(args->log->mutex);
      fprintf(args->log->file, "Cashier %d closed by director (TID: %ld)\n",
                args->id, pthread_self());
      XUNLOCK(args->log->mutex);
    }

    cashier_closures_count++;

    //wait for director to signal to reopen
    XLOCK(args->status_mutex)
    while ( *(args->status) == CLOSE && !sighup_status && !sigquit_status){
      XWAIT(args->status_closed, args->status_mutex);
    }
    XUNLOCK(args->status_mutex);

    if (cashier_closures_count != 0 && !sighup_status && !sigquit_status){
      XLOCK(args->log->mutex);
      fprintf(args->log->file, "Cashier %d reopened by director (TID: %ld)\n",
                args->id, pthread_self());
      XUNLOCK(args->log->mutex);
    }

    //Register the time the cashier reopened to compute the workshift time
    SYS_CALL(clock_gettime(CLOCK_REALTIME, &time_cashier_opened), "clock_gettime");

    XLOCK(args->customers_counter->mutex);
  }
  XUNLOCK(args->customers_counter->mutex);

  if (*(args->status) == OPEN){

    SYS_CALL(clock_gettime(CLOCK_REALTIME, &time_cashier_closed), "clock_gettime");
    struct timespec time_workshift;
    timespec_diff(&time_cashier_opened, &time_cashier_closed, &time_workshift);

    XLOCK(args->supermarket_log->mutex);
    fprintf(args->supermarket_log->file, "KS\t%d\t%ld.%03lu\n",
              args->id, time_workshift.tv_sec, time_workshift.tv_nsec/MILLION);
    XUNLOCK(args->supermarket_log->mutex);

  }

  //Log general informations about the cashier as requested by specific.
  XLOCK(args->supermarket_log->mutex);
  fprintf(args->supermarket_log->file, "K\t%d\t%d\t%d\t%d\n",
            args->id, cashier_served_customers, cashier_elaborated_products, cashier_closures_count);
  XUNLOCK(args->supermarket_log->mutex);

  pthread_cleanup_pop(1);

  return NULL;

}


void* report_to_director(void* args_pointer){

  struct __report_to_director_args* args = (struct __report_to_director_args*)args_pointer;

  while(!sighup_status && !sigquit_status){

    int count = get_count_fifo(args->queue->fifo, args->queue->mutex);

    //The following memory is shared with the cashiers handler thread.
    //The old value is overwritten. The update frequency is taken from
    //  the config file.
    XLOCK(&args->queue_customers_count->mutex);
    args->queue_customers_count->count = count;
    XSIGNAL(&args->queue_customers_count->old_value);
    XUNLOCK(&args->queue_customers_count->mutex);

    nanotimer(args->frequency);

  }

  XLOCK(&args->queue_customers_count->mutex);
  XSIGNAL(&args->queue_customers_count->old_value);
  XUNLOCK(&args->queue_customers_count->mutex);

  free(args);

  return NULL;

}
