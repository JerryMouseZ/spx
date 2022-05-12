/**
 * comp2017 - assignment 3
 * <your name>
 * <your unikey>
 */
#define _XOPEN_SOURCE 600

#include "spx_exchange.h"
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <sys/wait.h>

static inline int min(int a, int b)
{
    return a > b ? b : a;
}

trader_t *traders = NULL; // arraries for traders
product_t *products = NULL;
marketorder_t *markets = NULL;
int market_max = -1;
int market_num = 0;
int market_reserve = 10;

int current = 0;

int trader_num = 0;
int product_num = 0;

int sigfifos[10]; // store signal fifo
int head;
int tail;
int fees = 0;
bool end;

// don't do things at signal handler
void signal_handler(int sig, siginfo_t *info, void *context)
{
    sigfifos[tail] = info->si_pid;
    tail = (tail + 1) % 10;
    return;
}

void pipe_handler(int sig, siginfo_t *info, void *context)
{
    if (!traders[current].invalid)
        spx_log(" Trader %d disconnected\n", current);
    traders[current].invalid = true;
    end = true;
    for (int i = 0; i < trader_num; ++i) {
        if (!traders[i].invalid) {
            end = false;
            break;
        }
    }
}

void child_handler(int sig, siginfo_t* info, void *context)
{
    for (int i = 0; i < trader_num; ++i) {
        if (traders[i].pid == info->si_pid) {
            if (!traders[i].invalid)
                spx_log(" Trader %d disconnected\n", i);
            traders[i].invalid = true;
            break;
        }
    }

    end = true;
    for (int i = 0; i < trader_num; ++i) {
        if (!traders[i].invalid) {
            end = false;
            break;
        }
    }
}

/* void add_order(int trader_id, order_t order) */
/* { */
/*     if (traders[trader_id].order_reserve == traders[trader_id].order_num) { */
/*         traders[trader_id].order_reserve *= 2; */
/*         traders[trader_id].orders = realloc(traders[trader_id].orders, traders[trader_id].order_reserve); */
/*     } */
/*     traders[trader_id].orders[traders[trader_id].order_num++] = order; */
/* } */

int order_buy(int trader_id, char *buffer)
{
    // split the message
    order_t new_order;
    char *token = strtok(buffer, " ");
    if (token == NULL)
        return -1;
    new_order.buy = true;
    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    new_order.id = atoi(token);
    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    strcpy(new_order.name, token);
    bool exist = false;
    for (int i = 0; i < product_num; ++i) {
        if (strcmp(products[i].name, new_order.name) == 0) {
            exist = true;
            break;
        }
    }
    if (!exist)
        return -1;
    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    new_order.qty = atoi(token);
    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    new_order.price = atoi(token);
    new_order.trader_id = trader_id;

    char response[128];
    sprintf(response, "ACCEPTED %d;", new_order.id);
    current = trader_id;
    write(traders[trader_id].exfd, response, strlen(response));
    kill(traders[trader_id].pid, SIGUSR1);
    
    char message[128];
    sprintf(message, "MARKET BUY %s %d %d;", new_order.name, new_order.qty, new_order.price);
    notify_except(trader_id, message);
    /* add_order(trader_id, new_order); */
    return new_order.id;
}

int order_sell(int trader_id, char *buffer)
{
    // split the message
    order_t new_order;
    char *token = strtok(buffer, " ");
    if (token == NULL)
        return -1;
    new_order.buy = false;
    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    new_order.id = atoi(token);
    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    strcpy(new_order.name, token);
    bool exist = false;
    for (int i = 0; i < product_num; ++i) {
        if (strcmp(products[i].name, new_order.name) == 0) {
            exist = true;
            break;
        }
    }
    if (!exist)
        return -1;
    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    new_order.qty = atoi(token);
    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    new_order.price = atoi(token);
    new_order.trader_id = trader_id;

    char response[128];
    sprintf(response, "ACCEPTED %d;", new_order.id);
    current = trader_id;
    write(traders[trader_id].exfd, response, strlen(response));
    kill(traders[trader_id].pid, SIGUSR1);

    char message[128];
    sprintf(message, "MARKET SELL %s %d %d;", new_order.name, new_order.qty, new_order.price);
    notify_except(trader_id, message);
    add_order(trader_id, new_order);
    return new_order.id;
}

int order_update(int trader_id, char *buffer)
{
    char *token = strtok(buffer, " ");
    if (token == NULL)
        return -1;
    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    int order_id = atoi(token);
    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    int qty = atoi(token);
    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    int price = atoi(token);
    order_t order;
    int res = -1;
    for (int i = 0; i < traders[trader_id].order_num; ++i) {
        if (traders[trader_id].orders[i].id == order_id) {
            // remove on market
            for (int j = 0; j < market_num; ++j) {
                if (markets[j].trader_id == trader_id && markets[j].order.id == order_id) {
                    markets[j].order.qty = 0;
                    update_market(j);
                    break;
                }
            }
            order = traders[trader_id].orders[i];
            traders[trader_id].orders[i].price = price;
            traders[trader_id].orders[i].qty = qty;
            add_market_order(trader_id, traders[trader_id].orders[i]);
            char response[128];
            sprintf(response, "AMEND %d;", order_id);
            current = trader_id;
            write(traders[trader_id].exfd, response, strlen(response));
            kill(traders[trader_id].pid, SIGUSR1);
            res = order_id;
            break;
        }
    }

    char message[128];
    sprintf(message, "MARKET AMEND %s %d %d;", order.name, order.qty, order.price);
    notify_except(trader_id, message);
    return res;
}

int order_remove(int trader_id, char *buffer)
{
    char *token = strtok(buffer, " ");
    if (token == NULL)
        return -1;
    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    int order_id = atoi(token);
    int res = -1;
    order_t order;
    for (int i = 0; i < traders[trader_id].order_num; ++i) {
        if (traders[trader_id].orders[i].id == order_id) {
            order = traders[trader_id].orders[i];
            traders[trader_id].orders[i].qty = 0;
            char response[128];
            sprintf(response, "CANCEL %d;", order_id);
            current = trader_id;
            write(traders[trader_id].exfd, response, strlen(response));
            kill(traders[trader_id].pid, SIGUSR1);
            res = order_id;
            break;
        }
    }
    char message[128];
    sprintf(message, "MARKET CANCEL %s 0 0;", order.name);
    notify_except(trader_id, message);

    return res;
}

void update_market(int index)
{
    if (markets[index].order.qty == 0) {
        int prev = markets[index].prev;
        int next = markets[index].next;
        if (prev >= 0) {
            markets[prev].next = next;
        } else {
            // prev = -1
            market_max = next;
        }

        if (next >= 0) {
            markets[next].prev = prev;
        }
        markets[index].next = -1;
        markets[index].prev = -1;
    }
}

int get_product_index(char *name)
{
    for (int i = 0; i < product_num; ++i) {
        if (strcmp(name, products[i].name) == 0)
            return i;
    }
    return -1;
}

void add_market_order(int trader_id, order_t order)
{
    // add the remain order to book
    if (market_num == market_reserve) {
        market_reserve *= 2;
        markets = realloc(markets, market_reserve);
    }

    markets[market_num].order = order;
    markets[market_num].trader_id = trader_id;
    if (market_max == -1) {
        market_max = market_num;
        markets[market_num].next = -1;
        markets[market_num].prev = -1;
        market_num++;
        return;
    }

    int index = market_max;
    int rev = -1;
    while (index != -1) {
        int next = markets[index].next;
        if (markets[index].order.qty == 0) {
            index = next;
            continue;
        }
        int prev = markets[index].prev;
        if (markets[index].order.price < order.price) {
            markets[market_num].next = index;
            markets[market_num].prev = prev;
            markets[index].prev = market_num;
            if (prev != -1) {
                markets[prev].next = market_num;
            }
            break;
        } else {
            rev = index;
        }
        index = next;
    }
    if (index == -1) {
        // smallest
        markets[market_num].prev = rev;
        markets[market_num].next = -1;
    }

    if (markets[market_num].prev == -1)
        market_max = market_num;

    market_num++;
}

void match_orders(int trader_id, order_t order)
{
    char message[128];
    if (order.qty == 0)
        return;
    int pro = get_product_index(order.name);
    assert(pro >= 0);
    // money and fees

    for (int i = market_max; i != -1; i = markets[i].next) {
        if (markets[i].order.qty == 0)
            continue;
        if (strcmp(markets[i].order.name, order.name) == 0 && markets[i].order.buy != order.buy) {
            if (order.buy && order.price >= markets[i].order.price) {
                int num = min(order.qty, markets[i].order.qty);
                order.qty -= num;
                int value = order.price * num;
                traders[order.trader_id].prices[pro] -= order.price * num;
                traders[order.trader_id].qtys[pro] += num;

                markets[i].order.qty -= num;
                traders[markets[i].order.trader_id].prices[pro] += order.price * num;
                traders[markets[i].order.trader_id].qtys[pro] -= num;
                int fee = (order.price * num + 50) / 100;
                fees += fee;
                traders[order.trader_id].prices[pro] -= fee;

                update_market(i);
                spx_log(" Match: Order %d [T%d], New Order %d [T%d], value: $%d, fee: $%d.\n", 
                        markets[i].order.id, markets[i].trader_id, order.id, trader_id, value, fee);
                sprintf(message, "FILL %d %d;", markets[i].order.id, num);
                write(traders[markets[i].trader_id].exfd, message, strlen(message));
                if (order.qty == 0)
                    return;
            } else if(!order.buy && order.price <= markets[i].order.price) {
                int num = min(order.qty, markets[i].order.qty);
                int value = markets[i].order.price * num;
                order.qty -= num;
                traders[order.trader_id].prices[pro] += value;
                traders[order.trader_id].qtys[pro] -= num;

                markets[i].order.qty -= num;
                traders[markets[i].order.trader_id].prices[pro] -= value;
                traders[markets[i].order.trader_id].qtys[pro] += num;
                int fee = (value + 50) / 100;
                fees += fee;
                traders[order.trader_id].prices[pro] -= fee;

                update_market(i);
                spx_log(" Match: Order %d [T%d], New Order %d [T%d], value: $%d, fee: $%d.\n", 
                        markets[i].order.id, markets[i].trader_id, order.id, trader_id, value, fee);
                sprintf(message, "FILL %d %d;", markets[i].order.id, num);
                write(traders[markets[i].trader_id].exfd, message, strlen(message));
                if (order.qty == 0)
                    return;
            }
        }
    }

    add_market_order(trader_id, order);
}

void report()
{
    printf(LOG_PREFIX "\t--ORDERBOOK--\n");
    for (int i = 0; i < product_num; ++i) {
        printf(LOG_PREFIX "\tProduct: %s; ", products[i].name);
        int buy_levels = 0, sell_levels = 0;
        int index = market_max;
        while (index != -1) {
            int next = markets[index].next;
            if (markets[index].order.qty != 0) {
                if (strcmp(markets[index].order.name, products[i].name) == 0) {
                    if (next >= 0 && markets[next].order.qty > 0) {
                        if ((strcmp(markets[next].order.name, products[i].name) == 0)
                            && (markets[index].order.buy == markets[next].order.buy)
                            && (markets[index].order.price == markets[next].order.price)) {
                            index = next;
                            continue;
                        }
                    }
                    if (markets[index].order.buy)
                        buy_levels++;
                    else
                        sell_levels++;
                }
            }
            index = next;
        }

        printf("Buy levels: %d; Sell levels: %d\n", buy_levels, sell_levels);

        // print order book
        index = market_max;
        int orders = 0;
        int qtys = 0;

        while (index != -1) {
            int next = markets[index].next;
            if (markets[index].order.qty != 0) {
                if (strcmp(markets[index].order.name, products[i].name) == 0) {
                    orders++;
                    qtys += markets[index].order.qty;
                    if (next >= 0 && markets[next].order.qty > 0) {
                        if ((strcmp(markets[next].order.name, products[i].name) == 0)
                            && (markets[index].order.buy == markets[next].order.buy)
                            && (markets[index].order.price == markets[next].order.price)) {
                            index = next;
                            continue;
                        }
                    }
                    if (markets[index].order.buy) {
                        if (qtys > 1)
                            spx_log("\t\tBUY %d @ $%d (%d orders)\n", qtys, markets[index].order.price, orders);
                        else
                            spx_log("\t\tBUY %d @ $%d (%d order)\n", qtys, markets[index].order.price, orders);
                    }
                    else {
                        if (qtys > 1)
                            spx_log("\t\tBUY %d @ $%d (%d orders)\n", qtys, markets[index].order.price, orders);
                        else
                            spx_log("\t\tSell %d @ $%d (%d order)\n", qtys, markets[index].order.price, orders);
                    }
                    orders = 0;
                    qtys = 0;
                }
            }
            index = next;
        }
    }

    // for positions
    spx_log("\t--POSITIONS--\n");
    for (int i = 0; i < trader_num; ++i) {
        spx_log("\tTrader %d: ", i);
        for (int j = 0; j < product_num; ++j) {
            if (j != product_num - 1)
                printf("%s %d ($%d), ", products[j].name, traders[i].qtys[j], traders[i].prices[j]);
            else
                printf("%s %d ($%d)\n", products[j].name, traders[i].qtys[j], traders[i].prices[j]);
        }
    }
}

void handle_event(int id)
{
    char buffer[128];
    int bytes = read_message(traders[id].trfd, buffer);
    assert(bytes > 0);

    char *endline = strstr(buffer, ";");
    if (endline) {
        *endline = 0;
    } else {
        printf("error command : %s\n", buffer);
        return;
    }

    spx_log(" [T%d] Parsing command: <%s>\n", id, buffer);

    int res = 0;
    if (strstr(buffer, "BUY")) {
        res = order_buy(id, buffer);
    } else if (strstr(buffer, "SELL")) {
        res = order_sell(id, buffer);
    } else if (strstr(buffer, "AMEND")) {
        res = order_update(id, buffer);
    } else if (strstr(buffer, "CANCEL")) {
        res = order_remove(id, buffer);
    } else {
        // invalid
        res = -1;
    }

    if (res == -1) {
        char response[128];
        sprintf(response, "INVALID;");
        current = id;
        write(traders[id].exfd, response, strlen(response));
        kill(traders[id].pid, SIGUSR1);
        return;
    }

    // maybe remove and cannot find the order
    order_t new_order = traders[id].orders[res];
    // try to match orders
    // match order_list
    match_orders(id, new_order);
    report();
}

void main_loop()
{
    while (true) {
        // wait for signal
        while (head == tail && !end)
            pause(); 
        if (end) {
            break;
        }

        pid_t pid = sigfifos[head];
        head = (head + 1) % 10;
        for (int i = 0; i < trader_num; ++i) {
            if (traders[i].pid == pid) {
                handle_event(i);
                break;
            }
        }
    }
}

int main(int argc, char **argv) {
    // install signal handler
    struct sigaction sa, sb, sc;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigaction(SIGUSR1, &sa, NULL);

    sb.sa_flags = SA_SIGINFO;
    sb.sa_sigaction = child_handler;
    sigaction(SIGCHLD, &sb, NULL);

    sc.sa_flags = SA_SIGINFO;
    sc.sa_sigaction = pipe_handler;
    sigaction(SIGPIPE, &sc, NULL);

    startup(argc, argv);

    main_loop();

    wait_all();
    clean_all();

    return 0;
}

void print_usage(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage:\n"
                "./spx_exchange [product file] [trader 0] [trader 1] ... [trader n]\n");
        exit(1);
    }
}

void building_book(const char *filename)
{
    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        fprintf(stderr, "file %s cannot be open\n", filename);
        exit(2);
    }

    char buffer[128];
    fgets(buffer, 128, f);
    product_num = atoi(buffer);
    products = calloc(product_num, sizeof(product_t));
    spx_log(" Trading %d products: ", product_num);
    for (int i = 0; i < product_num; ++i) {
        fgets(buffer, 128, f);
        buffer[strlen(buffer) - 1] = 0; // remove endl
        strcpy(products[i].name, buffer);
        if (i != product_num - 1)
            printf("%s ", buffer);
        else
            printf("%s\n", buffer);
    }
    fclose(f);
}

void creating_pipe(int i)
{
    // name pipes
    char name[32];
    sprintf(name, FIFO_EXCHANGE, i);
    mkfifo(name, 0666);
    spx_log(" Created FIFO %s\n", name);

    sprintf(name, FIFO_TRADER, i);
    mkfifo(name, 0666);
    spx_log(" Created FIFO %s\n", name); 
}

void connect_pipes(int i)
{
    // need pid
    char name[32];
    sprintf(name, FIFO_EXCHANGE, i);
    int exchagne_fd = open(name, O_WRONLY);
    spx_log(" Connected to %s\n", name);
    traders[i].exfd = exchagne_fd;

    sprintf(name, FIFO_TRADER, i);
    int trader_fd = open(name, O_RDONLY);
    spx_log(" Connected to %s\n", name);
    traders[i].trfd = trader_fd;
}

void lauch_binary(int argc, char **argv)
{
    for (int i = 2; i < argc; ++i) {
        creating_pipe(i - 2);
        spx_log(" Starting trader %d (%s)\n", i - 2, argv[i]);
        pid_t pid = fork();
        assert (pid >= 0);
        if (pid == 0) {
            // child
            char command[12];
            sprintf(command, "%d", i - 2);
            execl(argv[i], command);
            // never reach here
            assert(0);
        }
        traders[i - 2].pid = pid;
        connect_pipes(i - 2);
    }
}


void init_traders(int num)
{
    trader_num = num;
    traders = calloc(trader_num, sizeof(trader_t));
    for (int i = 0; i < trader_num; ++i) {
        traders[i].orders = calloc(10, sizeof(order_t));
        traders[i].order_reserve = 10;
        traders[i].prices = calloc(product_num, sizeof(int));
        traders[i].qtys = calloc(product_num, sizeof(int));
    }
}

void startup(int argc, char **argv)
{
    print_usage(argc, argv);
    spx_log(" Starting\n");
    building_book(argv[1]);
    init_traders(argc - 2);
    markets = calloc(market_reserve, sizeof(marketorder_t));

    /* creating_pipe(trader_num); */
    lauch_binary(argc, argv);

    char message[128] = "MARKET OPEN;";
    notify_all(message);
}


void notify_all(char *message)
{
    for (int i = 0; i < trader_num; ++i) {
        if (traders[i].invalid)
            continue;
        current = i;
        write(traders[i].exfd, message, strlen(message));
    }

    for (int i = 0; i < trader_num; ++i) {
        if (traders[i].invalid)
            continue;
        kill(traders[i].pid, SIGUSR1);
    }
}

void notify_except(int id, char *message)
{
    for (int i = 0; i < trader_num; ++i) {
        if (i == id || traders[i].invalid)
            continue;
        current = i;
        write(traders[i].exfd, message, strlen(message));
    }

    for (int i = 0; i < trader_num; ++i) {
        if (i == id || traders[i].invalid)
            continue;
        kill(traders[i].pid, SIGUSR1);
    }
}

void wait_all()
{
    for (int i = 0; i < trader_num; ++i) {
        kill(traders[i].pid, SIGKILL);
        clean_pipe(i);
    }
}

void clean_all()
{
    // free traders
    for (int i = 0; i < trader_num; ++i) {
        // free orders
        free(traders[i].orders);
        free(traders[i].prices);
        free(traders[i].qtys);
    }
    free(traders);

    // free markets
    spx_log(" Trading completed\n");
    spx_log(" Exchange fees collected: $%d\n", fees);
}


void clean_pipe(int id)
{
    char name[32];
    sprintf(name, FIFO_EXCHANGE, id);
    unlink(name);
    sprintf(name, FIFO_TRADER, id);
    unlink(name);
}