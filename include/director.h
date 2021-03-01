#ifndef DIRECTOR_H_
#define DIRECTOR_H_

#include <pthread.h>
#include <utils.h>
#include <customer.h>

typedef struct __director{
  pthread_t thread;
}director_t;

struct __director_args{
  struct __all_cashiers* all_cashiers;
  struct __customers_counter* customers_counter;
  queue_t* director_permissions_list;
  pthread_t* entrance_thread;
  struct __cashiers_handler_args* cashiers_handler_args;
  FILE* log;
};

struct __cashiers_handler_args{
  int director_too_few_customers;
  int director_too_many_customers;
  int director_below_min_limit;
  int director_above_max_limit;
  int initial_open_cashiers;
};

/*
 * \brief Dynamic initialization of a new director.
 * \returns a director_t pointer, which will consist of only
 *                the pthread_t of the director.
 * \param all_cashiers: pointer to a list containing informations
 *                about the cashiers.
 * \param director_permissions_list: unbounded fifo where the customers will
 *                queue their permissions request if they have 0 products.
 * \param customers_counter: pointer to a struct containing an int representing
 *                the number of customers inside the supermarket at a certain
                  time, the mutex to modify the counter and a condition variable
 *                to wait for when supermarket is full.
 * \param entrance_thread: pthread_t of the entrance, that will be joined
 *                inside the director thread.
 * \param log: log file where main events will be written by director thread
 *                (not actually used, but could be useful).
 * \param cashiers_handler_args: struct containing informations and variables
 *                that will be used by the cashiers_handler thread.
 */
 director_t* director_init(struct __all_cashiers* all_cashiers, queue_t* director_permissions_list,
                 struct __customers_counter* customers_counter, pthread_t* entrance_thread,
                 FILE* log, struct __cashiers_handler_args* cashiers_handler_args);

/*
 * \brief Joins the thread inside the director passed as param.
 * \param director : pointer to director to join.
 */
void director_join(director_t* director);

/*
 * \brief main director function. Will listen to cashiers message and
                  give permission to exit to customers with 0 products
 * \param args_pointer : args initialized by init_director
 */
void* director(void* args_pointer);


/*
 * \brief Will decide to open/close cashiers, based off parameters
 *                contained in the config file
 * \param args_pointer : args initialized by the main.
 */
void* cashiers_handler(void* args_pointer);

#endif
