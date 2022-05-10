#ifndef SPX_EXCHANGE_H
#define SPX_EXCHANGE_H

#include "spx_common.h"
#include <sys/types.h>

#define LOG_PREFIX "[SPX]"

typedef struct order_book {
    char name[20];
    int num;
    int total_price;
    struct order_book *next;
} order_book_t;

typedef struct trader_node{
    int id;
    pid_t pid;
    int exchagne_fd;
    int trader_fd;
    struct trader_node *next;
    int valid;
    order_book_t *book;
} trader_node_t;

typedef struct order_node {
    int order_id;
    int trader_id;
    char type[8];
    char product[20];
    int num;
    int price;
    struct order_node *next;
    struct order_node *prev;
} order_node_t;

void add_products(char *name);

void add_trader_node(int id, pid_t pid);

void notify_all(char *message);
void notify_except(int id, char *message);

void startup(int argc, char **argv);

void wait_all();
void clean_all();

void clean_orders(order_node_t *head);

trader_node_t *trader_lookup_pid(int fd);
trader_node_t *trader_lookup_id(int id);

void clean_book(order_book_t *head);
void add_order(order_node_t *node);
order_node_t *order_lookup(order_node_t *order_head, int id);
void order_remove(order_node_t **order_head, order_node_t **order_tail, int id);
void match_orders();
void report();

#define spx_log(...) \
    do {            \
      printf(LOG_PREFIX " " __VA_ARGS__);              \
    } while (0)

void print_teardown(int id);

#endif
