#ifndef SUPERMARKET_H_
#define SUPERMARKET_H_

#include <customer.h>
#include <utils.h>

#define CONFIG_FILE "./config/config.ini"
#define BUFFER_SIZE 1024
#define ALL_PERMISSIONS_MASK 0777

#define USAGE(string){ \
            if (string) puts(#string); \
            printf("Use: %s [-f configfile]\n", argv[0]); \
            exit(1); \
          }

#define CHECK_GREATER_EQUAL_ONE(original, param, char){       \
              int converted = my_strtoi(original);            \
              if (converted<1){                               \
                printf("parameter \"%c\" must be an integer " \
                        "greater or equal than one\n", char); \
                break;                                        \
              }                                               \
              param = converted;                              \
              break;                                          \
            }

#define CHECK_GREATER_EQUAL_TEN(original, param, char){       \
              int converted = my_strtoi(original);            \
              if (converted<10){                              \
                printf("parameter \"%c\" must be an integer " \
                        "greater or equal than one\n", char); \
                break;                                        \
              }                                               \
              param = converted;                              \
              break;                                          \
            }

#define GET_LOG_FILE(original, len, file){                    \
            char* param = xmalloc(sizeof(char)*len);          \
            memset(param, '\0', sizeof(char)*len);            \
            strncpy(param, original, len-1);                  \
            file = fopen(param, "a");                         \
            CHECK_PTR(file, "fopen", exit(EXIT_FAILURE));     \
            free(param);                                      \
            break;                                            \
}

#define CONFIG_DEFAULTS {1,1,1,1,1,1,1,1,1,1,1,1,NULL,NULL,NULL, NULL}

struct __config{
  int cashiers_count;
  int cashiers_variable_service_time;
  int initial_open_cashiers;
  int customers_limit;
  int customers_threshold;
  int max_fixed_time_to_shop;
  int max_fixed_products_count;
  int report_to_director_frequency;
  int director_too_few_customers;
  int director_too_many_customers;
  int director_below_min_limit;
  int director_above_max_limit;
  FILE* file_log_supermarket;
  FILE* file_log_cashiers;
  FILE* file_log_customers;
  FILE* file_log_director;
};

struct __entrance_args{
  struct __customers_counter* customers_counter;
  int customers_limit;
  int customers_threshold;
  int max_fixed_time_to_shop;
  int max_fixed_products_count;
  unsigned int* supermarket_seed;
  struct __all_cashiers* all_cashiers;
  queue_t* director_permissions_list;
  struct __xlog* log;
  struct __xlog* supermarket_log;
};

extern volatile sig_atomic_t sighup_status;
extern volatile sig_atomic_t sigquit_status;

/*
 * \brief This function will handle the number of customers inside the
 *                supermarket, using the config variables C and E.
 * \param args_pointer : args initialized by the main.
 */
void* entrance(void* args_pointer);

#endif
