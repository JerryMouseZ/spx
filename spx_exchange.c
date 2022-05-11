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
int current;
int market_num = 0;
int market_reserve = 10;

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
    // waitint for handling
    return;

    /* // look up events[n].data.fd and send */
    /* pid_t pid = info->si_pid; */
    /* current = trader_lookup_pid(pid); */
    /* assert(current != NULL); */
}

void pipe_handler(int sig)
{
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

void add_order(int trader_id, order_t order)
{
    
}

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

    char response[128];
    sprintf(response, "ACCEPTED %d;", new_order.id);
    current = trader_id;
    write(traders[trader_id].exfd, response, sizeof(response));
    kill(traders[trader_id].pid, SIGUSR1);

    char message[128];
    sprintf(message, "MARKET BUY %s %d %d;", new_order.name, new_order.qty, new_order.price);
    notify_except(trader_id, buffer);
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

    char response[128];
    sprintf(response, "ACCEPTED %d;", new_order.id);
    current = trader_id;
    write(traders[trader_id].exfd, response, sizeof(response));
    kill(traders[trader_id].pid, SIGUSR1);

    char message[128];
    sprintf(message, "MARKET SELL %s %d %d;", new_order.name, new_order.qty, new_order.price);
    notify_except(trader_id, buffer);
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
    for (int i = 0; i < traders[trader_id].order_num; ++i) {
        if (traders[trader_id].orders[i].id == order_id) {
            traders[trader_id].orders[i].price = price;
            traders[trader_id].orders[i].qty = qty;
            char response[128];
            sprintf(response, "AMEND %d;", order_id);
            current = trader_id;
            write(traders[trader_id].exfd, response, sizeof(response));
            kill(traders[trader_id].pid, SIGUSR1);
            return order_id;
        }
    }
    return -1;
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
    for (int i = 0; i < traders[trader_id].order_num; ++i) {
        if (traders[trader_id].orders[i].id == order_id) {
            traders[trader_id].orders[i].qty = 0;
            char response[128];
            sprintf(response, "CANCEL %d;", order_id);
            current = trader_id;
            write(traders[trader_id].exfd, response, sizeof(response));
            kill(traders[trader_id].pid, SIGUSR1);
            return order_id;
        }
    }
    return -1;
}

void match_orders(order_t order)
{
    if (order.qty == 0)
        return;
    // money and fees
    for (int i = 0; i < market_num; ++i) {
        if (markets[i].order.qty == 0)
            continue;
        if (strcmp(markets[i].order.name, order.name) == 0 && markets[i].order.buy != order.buy) {
            if (order.buy && order.price >= markets[i].order.price) {

            } else if(!order.buy && order.price <= markets[i].order.price) {

            }
        }
    }
}

void report()
{
    printf(LOG_PREFIX "\t--ORDERBOOK--\n");
    for (int i = 0; i < product_num; ++i) {
        printf(LOG_PREFIX "\tProduct: %s; ", products[i].name);
        int buy_levels = 0, sell_levels = 0;
        for (int j = 0; j < market_num; ++j) {
            if (markets[j].order.qty == 0)
                continue;
            if (strcmp(markets[j].order.name, products[i].name) == 0) {
                if (markets[j].order.buy)
                    buy_levels++;
                else
                    sell_levels++;
            }
        }
        printf("Buy levels: %d; Sell levels: %d\n", buy_levels, sell_levels);

        // print sort
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

    spx_log("[T%d] Parsing command: <%s>\n", id, buffer);

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
        write(traders[id].exfd, response, sizeof(response));
        kill(traders[id].pid, SIGUSR1);
        return;
    }

    // maybe remove and cannot find the order
    order_t new_order = traders[id].orders[res];
    // try to match orders
    // match order_list
    match_orders(new_order);
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
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigaction(SIGUSR1, &sa, NULL);

    sa.sa_sigaction = child_handler;
    sigaction(SIGCHLD, &sa, NULL);

    signal(SIGPIPE, pipe_handler);

    startup(argc, argv);

    main_loop();
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
    spx_log("Trading %d products: ", product_num);
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

void creating_pipe(int count)
{
    for (int i = 0; i < count; ++i) {
        // name pipes
        char name[32];
        sprintf(name, FIFO_EXCHANGE, i);
        mkfifo(name, 0666);
        spx_log("Created FIFO %s\n", name);

        sprintf(name, FIFO_TRADER, i);
        mkfifo(name, 0666);
        spx_log("Created FIFO %s\n", name); 
    }
}

void lauch_binary(int argc, char **argv)
{
    for (int i = 2; i < argc; ++i) {
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
    }
}

void connect_pipes(int count)
{
    // need pid
    for (int i = 0; i < count; ++i) {
        char name[32];
        sprintf(name, FIFO_EXCHANGE, i);
        int exchagne_fd = open(name, O_WRONLY);
        spx_log("Connected to %s\n", name);
        traders[i].exfd = exchagne_fd;

        sprintf(name, FIFO_TRADER, i);
        int trader_fd = open(name, O_RDONLY);
        spx_log("Connected to %s\n", name);
        traders[i].trfd = trader_fd;
    }
}

void init_traders(int num)
{
    trader_num = num;
    traders = calloc(trader_num, sizeof(trader_t));
    for (int i = 0; i < trader_num; ++i) {
        traders[i].orders = calloc(10, sizeof(order_t));
        traders[i].order_reserve = 10;
    }
}

void startup(int argc, char **argv)
{
    print_usage(argc, argv);
    spx_log(" Starting\n");
    building_book(argv[1]);
    init_traders(argc - 2);
    markets = calloc(market_reserve, sizeof(marketorder_t));

    creating_pipe(trader_num);
    lauch_binary(argc, argv);
    connect_pipes(trader_num);

    char message[128] = "MARKET OPEN;";
    notify_all(message);
}


void notify_all(char *message)
{
    for (int i = 0; i < trader_num; ++i) {
        current = i;
        write(traders[i].exfd, message, strlen(message));
    }

    for (int i = 0; i < trader_num; ++i) {
        kill(traders[i].pid, SIGUSR1);
    }
}

void notify_except(int id, char *message)
{
    for (int i = 0; i < trader_num; ++i) {
        if (i == id)
            continue;
        current = i;
        write(traders[i].exfd, message, strlen(message));
    }

    for (int i = 0; i < trader_num; ++i) {
        if (i == id)
            continue;
        kill(traders[i].pid, SIGUSR1);
    }
}

void clean_all()
{
    // free traders
    for (int i = 0; i < trader_num; ++i) {
        if (!traders[i].invalid) {
            kill(traders[i].pid, SIGKILL);
        }
        free(traders[i].orders);
    }
    free(traders);

    // free orders

    spx_log("Trading completed\n");
    spx_log("Exchange fees collected: $%d\n", fees);
}


void clean_pipe(int id)
{
    char name[32];
    sprintf(name, FIFO_EXCHANGE, id);
    unlink(name);
    sprintf(name, FIFO_TRADER, id);
    unlink(name);

    spx_log("Trader %d disconnected\n", id);
}
