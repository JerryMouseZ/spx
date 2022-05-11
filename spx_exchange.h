#ifndef SPX_EXCHANGE_H
#define SPX_EXCHANGE_H

#include "spx_common.h"
#include <sys/types.h>
#include <stdbool.h>

#define LOG_PREFIX "[SPX]"

#define spx_log(...) \
    do {            \
      printf(LOG_PREFIX  __VA_ARGS__);              \
    } while (0)

typedef struct Order {
    int id;
    bool buy;
    char name[16];
    int qty;
    int price;
} order_t;

typedef struct Trader {
    int pid;
    int exfd; // exchange fd
    int trfd; // trader pipe fd
    bool invalid; // if true the trader has exit
    int order_num;
    int order_reserve;
    order_t *orders;
} trader_t;

typedef struct MarketOrder {
    int trader_id;
    order_t order;
} marketorder_t;

typedef struct Product {
    char name[16];
} product_t;

void clean_pipe(int id);
void clean_all();
void notify_all(char *message);
void notify_except(int id, char *message);
void startup(int argc, char **argv);
void clean_pipe(int id);

#endif
