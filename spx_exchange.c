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
order_t *orders = NULL;

int current = 0; // current trader index

int trader_num = 0;
int product_num = 0;

// store signal fifo
int sigfifos[10]; 
int head;
int tail;

int fees = 0;
// exit flag
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

order_t *order_find(int trader_id, int order_id)
{
    order_t *node = orders;
    while (node) {
        if (node->trader_id == trader_id && node->order_id == order_id) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

void add_order(order_t order)
{
    order_t *new_order = calloc(1, sizeof(order_t));
    memcpy(new_order, &order, sizeof(order_t));
    new_order->next = NULL;

    if (orders == NULL) {
        orders = new_order;
        return;
    }
    
    if (new_order->price > orders->price) {
        new_order->next = orders;
        orders = new_order;
        return;
    }

    order_t *node = orders;
    while (node->next) {
        if (new_order->price > node->next->price) {
            order_t *next = node->next;
            node->next = new_order;
            new_order->next = next;
            break;
        }
        node = node->next;
    }
    if (node->next == NULL) {
        node->next = new_order;
    }
}

void remove_order(int trader_id, int order_id)
{
    if (orders->trader_id == trader_id && orders->order_id == order_id) {
        order_t *backup = orders;
        orders = orders->next;
        free(backup);
        return;
    }

    order_t *node = orders;
    while (node->next) {
        if (node->next->trader_id == trader_id && node->next->order_id == order_id) {
            order_t *next = node->next;
            node->next = next->next;
            free(next);
            break;
        }
        node = node->next;
    }
}

int command_buy(int trader_id, char *buffer)
{
    // split the message
    order_t new_order;
    new_order.trader_id = trader_id;
    char *token = strtok(buffer, " ");
    if (token == NULL)
        return -1;
    new_order.buy = true;
    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    new_order.order_id = atoi(token);
    if (new_order.order_id != traders[trader_id].next_id) {
        return -1;
    }
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
    if (new_order.qty <= 0 || new_order.qty >= 1000000)
        return -1;
    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    new_order.price = atoi(token);
    if (new_order.price <= 0 || new_order.price >= 1000000)
        return -1;
    
    traders[trader_id].next_id++;
    char response[128];
    sprintf(response, "ACCEPTED %d;", new_order.order_id);
    current = trader_id;
    write(traders[trader_id].exfd, response, strlen(response));
    kill(traders[trader_id].pid, SIGUSR1);
    
    char message[128];
    sprintf(message, "MARKET BUY %s %d %d;", new_order.name, new_order.qty, new_order.price);
    notify_except(trader_id, message);
    match_orders(trader_id, new_order, true);
    return new_order.order_id;
}

int command_sell(int trader_id, char *buffer)
{
    // split the message
    order_t new_order;
    new_order.trader_id = trader_id;
    char *token = strtok(buffer, " ");
    if (token == NULL)
        return -1;
    new_order.buy = false;
    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    new_order.order_id = atoi(token);
    if (new_order.order_id != traders[trader_id].next_id) {
        return -1;
    }
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
    if (new_order.qty <= 0 || new_order.qty >= 1000000)
        return -1;

    token = strtok(NULL, " ");
    if (token == NULL)
        return -1;
    new_order.price = atoi(token);
    if (new_order.price <= 0 || new_order.price >= 1000000)
        return -1;


    traders[trader_id].next_id++;
    char response[128];
    sprintf(response, "ACCEPTED %d;", new_order.order_id);
    current = trader_id;
    write(traders[trader_id].exfd, response, strlen(response));
    kill(traders[trader_id].pid, SIGUSR1);

    char message[128];
    sprintf(message, "MARKET SELL %s %d %d;", new_order.name, new_order.qty, new_order.price);
    notify_except(trader_id, message);
    match_orders(trader_id, new_order, true);
    return new_order.order_id;
}

int command_amended(int trader_id, char *buffer)
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
    
    order_t *oldorder = order_find(trader_id, order_id);
    if (oldorder == NULL)
        return -1;
    oldorder->qty = qty;
    oldorder->price = price;
    char message[128];
    sprintf(message, "MARKET AMEND %s %d %d;", order.name, order.qty, order.price);
    notify_except(trader_id, message);
    match_orders(trader_id, *oldorder, false);
    return res;
}

int command_cancelled(int trader_id, char *buffer)
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
    
    order_t *oldorder = order_find(trader_id, order_id);
    if (oldorder == NULL)
        return -1;
    remove_order(trader_id, order_id);
    char message[128];
    sprintf(message, "MARKET CANCEL %s 0 0;", order.name);
    notify_except(trader_id, message);
    return res;
}

int get_product_index(char *name)
{
    for (int i = 0; i < product_num; ++i) {
        if (strcmp(name, products[i].name) == 0)
            return i;
    }
    return -1;
}


void match_orders(int trader_id, order_t order, bool add)
{
    char message[128];
    if (order.qty == 0)
        return;
    int pro = get_product_index(order.name);
    assert(pro >= 0);

    order_t *node = orders;
    while (node) {
        order_t *next = node->next;
        if ((strcmp(node->name, order.name) == 0) && node->buy != order.buy) {
            if (order.buy && order.price >= node->price) {
                int num = min(order.qty, node->qty);
                order.qty -= num;
                int value = order.price * num;
                traders[order.trader_id].prices[pro] -= order.price * num;
                traders[order.trader_id].qtys[pro] += num;

                node->qty -= num;

                traders[node->trader_id].prices[pro] += order.price * num;
                traders[node->trader_id].qtys[pro] -= num;
                int fee = (order.price * num + 50) / 100;
                fees += fee;
                traders[order.trader_id].prices[pro] -= fee;

                spx_log(" Match: Order %d [T%d], New Order %d [T%d], value: $%d, fee: $%d.\n", 
                        node->order_id, node->trader_id, order.order_id, trader_id, value, fee);
                sprintf(message, "FILL %d %d;", node->order_id, num);
                write(traders[node->trader_id].exfd, message, strlen(message));
                sprintf(message, "FILL %d %d;", order.order_id, num);
                write(traders[order.trader_id].exfd, message, strlen(message));

                if (node->qty == 0) {
                    remove_order(node->trader_id, node->order_id);
                }
                if (order.qty == 0)
                    return;
            } else if (!order.buy && order.price <= node->price) {
                int num = min(order.qty, node->qty);
                int value = node->price * num;
                order.qty -= num;
                traders[order.trader_id].prices[pro] += value;
                traders[order.trader_id].qtys[pro] -= num;

                node->qty -= num;
                traders[node->trader_id].prices[pro] -= value;
                traders[node->trader_id].qtys[pro] += num;
                int fee = (value + 50) / 100;
                fees += fee;
                traders[order.trader_id].prices[pro] -= fee;

                spx_log(" Match: Order %d [T%d], New Order %d [T%d], value: $%d, fee: $%d.\n", 
                        node->order_id, node->trader_id, order.order_id, trader_id, value, fee);
                sprintf(message, "FILL %d %d;", node->order_id, num);
                write(traders[node->trader_id].exfd, message, strlen(message));

                sprintf(message, "FILL %d %d;", order.order_id, num);
                write(traders[order.trader_id].exfd, message, strlen(message));

                if (node->qty == 0) {
                    remove_order(node->trader_id, node->order_id);
                }

                if (order.qty == 0)
                    return;
            }
        }
        node = next;
    }

    if (order.qty > 0 && add) {
        add_order(order);
    }
}

void report()
{
    printf(LOG_PREFIX "\t--ORDERBOOK--\n");
    for (int i = 0; i < product_num; ++i) {
        printf(LOG_PREFIX "\tProduct: %s; ", products[i].name);
        int buy_levels = 0, sell_levels = 0;
        order_t *node = orders;
        while (node) {
            order_t *next = node->next;
            if (strcmp(node->name, products[i].name) == 0) {
                if (next) {
                    if ((strcmp(next->name, products[i].name) == 0)
                        && (node->buy == next->buy)
                        && (node->price == next->price)) {
                        node = node->next;
                        continue;
                    }
                }
                if (node->buy)
                    buy_levels++;
                else
                    sell_levels++;
            }
            node = node->next;
        }

        printf("Buy levels: %d; Sell levels: %d\n", buy_levels, sell_levels);

        // print order book
        int qtys = 0;
        node = orders;
        int counts = 0;

        while (node) {
            order_t *next = node->next;
            if (strcmp(node->name, products[i].name) == 0) {
                counts++;
                qtys += node->qty;
                if (next) {
                    if ((strcmp(next->name, products[i].name) == 0)
                        && (node->buy == next->buy)
                        && (node->price == next->price)) {
                        node = node->next;
                        continue;
                    }
                }

                if (node->buy) {
                    if (counts > 1)
                        spx_log("\t\tBUY %d @ $%d (%d orders)\n", qtys, node->price, counts);
                    else
                        spx_log("\t\tBUY %d @ $%d (%d order)\n", qtys, node->price, counts);
                }
                else {
                    if (counts > 1)
                        spx_log("\t\tSELL %d @ $%d (%d orders)\n", qtys, node->price, counts);
                    else
                        spx_log("\t\tSELL %d @ $%d (%d order)\n", qtys, node->price, counts);
                }
                qtys = 0;
                counts = 0;
            }
            node = node->next;
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
        res = command_buy(id, buffer);
    } else if (strstr(buffer, "SELL")) {
        res = command_sell(id, buffer);
    } else if (strstr(buffer, "AMEND")) {
        res = command_amended(id, buffer);
    } else if (strstr(buffer, "CANCEL")) {
        res = command_cancelled(id, buffer);
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
    // try to match orders
    // match order_list
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
        waitpid(traders[i].pid, NULL, 0);
        clean_pipe(i);
    }
}

void clean_all()
{
    // free traders
    for (int i = 0; i < trader_num; ++i) {
        // free orders
        free(traders[i].prices);
        free(traders[i].qtys);
    }
    free(traders);

    order_t *node = orders;
    while (node) {
        order_t *next = node->next;
        free(node);
        node = next;
    }

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
