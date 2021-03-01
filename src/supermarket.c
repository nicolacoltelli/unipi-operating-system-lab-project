#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <supermarket.h>
#include <director.h>
#include <cashier.h>
#include <customer.h>
#include <utils.h>


volatile sig_atomic_t sighup_status = 0;
volatile sig_atomic_t sigquit_status = 0;


void handler_sighup(int sig){
  sighup_status = 1;
  SYS_CALL(write(1, "SIGHUP received\n", 16), "write");
}

void handler_sigquit(int sig){
  sigquit_status = 1;
  SYS_CALL(write(1, "SIGQUIT received\n", 17), "write");
}


int main(int argc, char** argv){

  // --- SIGNALS INITIALIZATION ---
  struct sigaction s;

  memset(&s, 0, sizeof(struct sigaction));
  s.sa_handler = handler_sighup;
  SYS_CALL(sigaction(SIGHUP, &s, NULL), "sigaction");

  memset(&s, 0, sizeof(struct sigaction));
  s.sa_handler = handler_sigquit;
  SYS_CALL(sigaction(SIGQUIT, &s, NULL), "sigaction");
  // ------------------------------

  //Creating "logs" folder if it doesn't exists yet 
  errno = 0;
  if (mkdir("logs", ALL_PERMISSIONS_MASK) == -1) {
    if (errno != EEXIST){
      perror(NULL);
      exit(EXIT_FAILURE);
    }
  }

  // argc == 1 -> config file is the default defined in supermarket.h
  // argc == 3 -> config file is passed as param after -f flag
  if (argc != 1 && argc != 3){
    USAGE("Number of arguments is incorrect");
  }

  char* config_file_path = xmalloc(sizeof(char)*BUFFER_SIZE);
  memset(config_file_path, '\0', sizeof(char)*BUFFER_SIZE);

  if (argc == 3){
    
    if (strncmp(argv[1], "-f", 3)){
      USAGE("First parameter is incorrect");
    }

    strncpy(config_file_path, argv[2], strlen(argv[2]));

  } else {
    strcpy(config_file_path, CONFIG_FILE);
  }


  // --- CONFIG INITIALIZATION ---

  /*
   * config_param is a struct that will contain all the input parameters.
   * The paramemeters will be taken from the config file, and if some
   * param will be missing defaults values are assigned.
   * Default values are not significant and their only purpose is to
   * not give params-dependent errors during execution 
   */
  struct __config config_param = CONFIG_DEFAULTS;

  FILE* config_file = fopen(config_file_path, "r");
  CHECK_PTR(config_file, "fopen", exit(EXIT_FAILURE));

  free(config_file_path);

  size_t buffer_size = BUFFER_SIZE;
  char* buffer = xmalloc(sizeof(char)*buffer_size);
  int len = 0;

  //The variable order written in the config files is not important
  while( (len = getline(&buffer, &buffer_size, config_file)) != -1 ){

    //Ignoring comments and empty lines
    if ( *buffer == '#' || *buffer == '\n' ) continue;

    char var_name = 0;
    char* value = xmalloc(sizeof(char)*BUFFER_SIZE);

    sscanf(buffer, "%c=%s\n", &var_name, value);

    switch (var_name) {

      case 'K': CHECK_GREATER_EQUAL_ONE(value, config_param.cashiers_count, var_name);
      case 'V': CHECK_GREATER_EQUAL_ONE(value, config_param.cashiers_variable_service_time, var_name);
      case 'O': CHECK_GREATER_EQUAL_ONE(value, config_param.initial_open_cashiers, var_name);
      case 'C': CHECK_GREATER_EQUAL_ONE(value, config_param.customers_limit, var_name);
      case 'E': CHECK_GREATER_EQUAL_ONE(value, config_param.customers_threshold, var_name);
      case 'T': CHECK_GREATER_EQUAL_TEN(value, config_param.max_fixed_time_to_shop, var_name);
      case 'P': CHECK_GREATER_EQUAL_ONE(value, config_param.max_fixed_products_count, var_name);
      case 'F': CHECK_GREATER_EQUAL_ONE(value, config_param.report_to_director_frequency, var_name);
      case 'W': CHECK_GREATER_EQUAL_ONE(value, config_param.director_too_few_customers, var_name);
      case 'X': CHECK_GREATER_EQUAL_ONE(value, config_param.director_too_many_customers, var_name);
      case 'Y': CHECK_GREATER_EQUAL_ONE(value, config_param.director_below_min_limit, var_name);
      case 'Z': CHECK_GREATER_EQUAL_ONE(value, config_param.director_above_max_limit, var_name);
      case 'I': GET_LOG_FILE(value, len, config_param.file_log_supermarket);
      case 'L': GET_LOG_FILE(value, len, config_param.file_log_cashiers);
      case 'M': GET_LOG_FILE(value, len, config_param.file_log_customers);
      case 'N': GET_LOG_FILE(value, len, config_param.file_log_director);
      default: printf("parameter \"%c\" from config.ini not recognized\n", var_name); break;

    }

    free(value);

  }

  if (fclose(config_file)) perror("fclose");
  free(buffer);

  //Auxiliar conifguration variables
  //These variables are not taken from config file
  int customers_count = 0;
  unsigned int supermarket_seed = time(NULL);
  // -----------------------------


  // -- XLOGS INITIALIZATION SECTION --
  struct __xlog supermarket_log;
  pthread_mutex_t supermarket_log_mutex = PTHREAD_MUTEX_INITIALIZER;
  supermarket_log.file = config_param.file_log_supermarket;
  supermarket_log.mutex = &supermarket_log_mutex;
  //The following two vars will be updated under
  //  the same lock as the supermarket log
  int served_customers_count = 0;
  int bought_products_count = 0;

  struct __xlog cashiers_log;
  pthread_mutex_t cashiers_log_mutex = PTHREAD_MUTEX_INITIALIZER;
  cashiers_log.file = config_param.file_log_cashiers;
  cashiers_log.mutex = &cashiers_log_mutex;

  struct __xlog customers_log;
  pthread_mutex_t customers_log_mutex = PTHREAD_MUTEX_INITIALIZER;
  customers_log.file = config_param.file_log_customers;
  customers_log.mutex = &customers_log_mutex;
  // ---------------------------------


  // -- CUSTOMERS COUNTER INITIALIZATION SECTION --
  struct __customers_counter customers_counter;
  pthread_mutex_t customers_counter_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t customers_counter_full = PTHREAD_COND_INITIALIZER;
  customers_counter.count = &customers_count;
  customers_counter.mutex = &customers_counter_mutex;
  customers_counter.full = &customers_counter_full;
  // ----------------------------------------------


  // --- CASHIERS INITIALIZATION ----
  struct __all_cashiers all_cashiers;
  all_cashiers.cashiers_list = xmalloc(sizeof(cashier_t*)*config_param.cashiers_count);
  all_cashiers.count = config_param.cashiers_count;

  for (int i = 0; i<config_param.cashiers_count; i++){
    all_cashiers.cashiers_list[i] = cashier_init(i, config_param.initial_open_cashiers,
                config_param.cashiers_variable_service_time, &cashiers_log,
                config_param.report_to_director_frequency, &customers_counter, &supermarket_seed,
                &supermarket_log, &served_customers_count, &bought_products_count);
    CHECK_PTR(all_cashiers.cashiers_list[i], "Received NULL pointer from cashier_init", exit(3));
  }
  // --------------------------------


  // --- DIRECTOR INITIALIZATION ----
  queue_t director_permissions_list;
  director_permissions_list.fifo = xmalloc(sizeof(fifo_unbounded_t));
  fifo_init(director_permissions_list.fifo);
  director_permissions_list.mutex = xmalloc(sizeof(pthread_mutex_t));
  CHECK_ERR(pthread_mutex_init(director_permissions_list.mutex, NULL), "mutex init");
  director_permissions_list.empty = xmalloc(sizeof(pthread_cond_t));
  CHECK_ERR(pthread_cond_init(director_permissions_list.empty, NULL), "cond init");

  pthread_t entrance_thread;

  struct __cashiers_handler_args cashiers_handler_args;
  cashiers_handler_args.director_too_few_customers = config_param.director_too_few_customers;
  cashiers_handler_args.director_too_many_customers = config_param.director_too_many_customers;
  cashiers_handler_args.director_below_min_limit = config_param.director_below_min_limit;
  cashiers_handler_args.director_above_max_limit = config_param.director_above_max_limit;
  cashiers_handler_args.initial_open_cashiers = config_param.initial_open_cashiers;
  director_t* director = director_init(&all_cashiers, &director_permissions_list, &customers_counter,
          &entrance_thread, config_param.file_log_director, &cashiers_handler_args);
  CHECK_PTR(director, "Received NULL pointer from director_init", exit(3));
  // --------------------------------


  // --- CUSTOMERS INITIALIZATION ---
  for (int i = 0; i<config_param.customers_limit; i++){
    customer_t* res = customer_init(i, &all_cashiers, &customers_counter,
              &director_permissions_list, &customers_log, config_param.max_fixed_time_to_shop,
              config_param.max_fixed_products_count, &supermarket_seed, &supermarket_log);
    CHECK_PTR(res, "Received NULL pointer from customer_init", NULL);
    free(res);
  }
  // --------------------------------


  // --- ENTRANCE INITIALIZATION ----
  struct __entrance_args entrance_args;
  entrance_args.customers_counter = &customers_counter;
  entrance_args.customers_limit = config_param.customers_limit;
  entrance_args.customers_threshold = config_param.customers_threshold;
  entrance_args.max_fixed_time_to_shop = config_param.max_fixed_time_to_shop;
  entrance_args.max_fixed_products_count = config_param.max_fixed_products_count;
  entrance_args.supermarket_seed = &supermarket_seed;
  entrance_args.all_cashiers = &all_cashiers;
  entrance_args.director_permissions_list = &director_permissions_list;
  entrance_args.log = &customers_log;
  entrance_args.supermarket_log = &supermarket_log;

  CHECK_PTHREAD_CREATE( pthread_create(&entrance_thread, NULL, entrance, &entrance_args),
              "entrance", exit(EXIT_FAILURE) );
  // --------------------------------

  // --------------------------------
  // ---- END OF INITIALIZATIONS ----
  // --------------------------------
  // --------------------------------


  //Until this point, the supermarket is operative
  //Upon closure, the director thread will be joined
  director_join(director);

  for (int i = 0; i<config_param.cashiers_count; i++){
    cashier_join(all_cashiers.cashiers_list[i]);
  }

  free(all_cashiers.cashiers_list);

  fprintf(config_param.file_log_supermarket, "Served Customers: %d\n", served_customers_count);
  fprintf(config_param.file_log_supermarket, "Bought products: %d\n", bought_products_count);


  CHECK_ERR(fclose(config_param.file_log_supermarket), "fclose");
  CHECK_ERR(fclose(config_param.file_log_cashiers), "fclose");
  CHECK_ERR(fclose(config_param.file_log_customers), "fclose");
  CHECK_ERR(fclose(config_param.file_log_director), "fclose");

  exit(EXIT_SUCCESS);

}


void* entrance(void* args_pointer){

  struct __entrance_args* args = (struct __entrance_args*)args_pointer;

  //At this point we just created "args->customers_limit"
  //  customers from the main function. So the next customer's
  //  ID will be "args->customer_limit".
  int progressive_id = args->customers_limit;
  int min_customers = args->customers_limit - args->customers_threshold;

  while( !sighup_status && !sigquit_status ){

    //Resources-critical section where we wait
    //  (passively) until the number of customers
    //   goes below the threshold.
    //The condition variable will be signaled by
    //  each customer when he leaves the supermarket. 
    XLOCK(args->customers_counter->mutex);

    while( *(args->customers_counter->count) > min_customers && !sighup_status && !sigquit_status){
      XWAIT(args->customers_counter->full, args->customers_counter->mutex);
    }

    int new_customers_count = args->customers_limit - *(args->customers_counter->count);

    XUNLOCK(args->customers_counter->mutex);

    if (sighup_status || sigquit_status) break;

    //During the reinsertion of new customers it
    //  may happen that the overall number of
    //  customers goes slightly below the threshold.
    //This is permissible as specific.
    for (int i = 0; i<new_customers_count; i++){

      customer_t* res = customer_init(progressive_id, args->all_cashiers, args->customers_counter,
              args->director_permissions_list, args->log, args->max_fixed_time_to_shop,
              args->max_fixed_products_count, args->supermarket_seed, args->supermarket_log);
      CHECK_PTR(res, "Received NULL pointer from customer_init", NULL);
      free(res);
      progressive_id++;

    }

  }

  return NULL;

}
