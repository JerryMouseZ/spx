#ifndef SPX_EXCHANGE_H
#define SPX_EXCHANGE_H

#include "spx_common.h"
#include <sys/types.h>

#define LOG_PREFIX "[SPX] "

typedef struct trader_node{
    int id;
    pid_t pid;
    int exchagne_fd;
    int trader_fd;
    struct trader_node *next;
} trader_node_t;

typedef struct order_node {
    int order_id;
    int trader_id;
    char type[8];
    char product[20];
    int num;
    int price;
    struct order_node *next;
} order_node_t;

void add_trader_node(int id, pid_t pid);

void notify_all(char *message);
void notify_except(int id, char *message);

void startup(int argc, char **argv);

void wait_all();
void clean_all();

trader_node_t *trader_lookup_fd(int fd);
trader_node_t *trader_lookup_id(int id);

void add_order(order_node_t *node);
order_node_t *order_lookup(int id);
void order_remove(int id);
void match_orders();

#define spx_log(...) \
    do {            \
      printf(LOG_PREFIX __VA_ARGS__);              \
    } while (0)

void print_teardown(int id);

#endif
