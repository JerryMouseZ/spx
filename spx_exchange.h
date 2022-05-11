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
    int trader_id;
    int order_id;
    bool buy;
    char name[16];
    int qty;
    int price;
    struct Order *next;
} order_t;


typedef struct Product {
    char name[16];
    int price;
} product_t;


typedef struct Trader {
    int pid;
    int exfd; // exchange fd
    int trfd; // trader pipe fd
    bool invalid; // if true the trader has exit
    int *prices;
    int *qtys;
} trader_t;


void add_market_order(int trader_id, order_t order);
void clean_pipe(int id);
void clean_all();
void notify_all(char *message);
void notify_except(int id, char *message);
void startup(int argc, char **argv);
void clean_pipe(int id);
void update_market(int index);
void wait_all();
void match_orders(int trader_id, order_t order, bool add);

#endif
