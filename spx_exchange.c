/**
 * comp2017 - assignment 3
 * <your name>
 * <your unikey>
 */

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
#include <sys/epoll.h>

static inline int min(int a, int b)
{
    return a > b ? b : a;
}

trader_node_t *head = NULL;
trader_node_t *tail = NULL;
order_node_t *buy_head = NULL;
order_node_t *buy_tail = NULL;
order_node_t *sell_head = NULL;
order_node_t *sell_tail = NULL;
order_book_t *book = NULL;
order_book_t *book_tail = NULL;

int trader_count = 0;
int fees = 0;

trader_node_t *current;

#define MAX_EVENTS 10
int epfd = -1;
struct epoll_event ev, events[MAX_EVENTS];

void signal_handler(int sig)
{
    int nfds = 0;
    do {
        nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }
        
        if (nfds == 0)
            break;
        char buffer[128];
        char message[128];
        for (int i = 0; i < nfds; ++i) {
            // look up events[n].data.fd and send
            int fd = events[i].data.fd;
            current = trader_lookup_fd(fd);
            assert(current != NULL);
            int bytes = read(fd, buffer, 128);
            if (bytes == 0) {
                // pipe error
                print_teardown(current->id);
                epoll_ctl(epfd, EPOLL_CTL_DEL, current->trader_fd, &ev);
                epoll_ctl(epfd, EPOLL_CTL_DEL, current->exchagne_fd, &ev);
                close(current->exchagne_fd);
                close(current->trader_fd);
                current->valid = 0;
                continue;
            }
            
            char *endline = strstr(buffer, ";");
            if (endline) {
                *endline = 0;
            }

            spx_log("[T%d] Parsing command: <%s>\n", current->id, buffer);
            // split the message
            order_node_t new_order;
            char *token = strtok(buffer, " ");
            strcpy(new_order.type, token);
            token = strtok(NULL, " ");
            new_order.order_id = atoi(token);
            token = strtok(NULL, " ");
            strcpy(new_order.product, token);
            token = strtok(NULL, " ");
            new_order.num = atoi(token);
            token = strtok(NULL, " ");
            new_order.price = atoi(token);

            // process
            new_order.trader_id = current->id;

            order_node_t *order_check = NULL;
            if (strstr(new_order.type, "BUY"))
                order_check = order_lookup(buy_head, new_order.order_id);
            else
                order_check = order_lookup(sell_head, new_order.order_id);
            if (order_check) {
                // cancelled if num and price == 0
                // amended if order_id is the same
                if (new_order.num == 0 && new_order.price == 0) {
                    // delete the node
                    if (strstr(new_order.type, "BUY"))
                        order_remove(&buy_head, &buy_tail, new_order.order_id);
                    else
                        order_remove(&sell_head, &sell_tail, new_order.order_id);
                    sprintf(message, "CANCELLED %d;", new_order.order_id);
                } else {
                    order_check->num = new_order.num;
                    order_check->price = new_order.price;
                    sprintf(message, "AMENDED %d;", new_order.order_id);
                }
            } else {
                add_order(&new_order);
                memset(message, 0, 128);
                sprintf(message, "ACCEPTED %d;", new_order.order_id);
            }

            write(current->exchagne_fd, message, 128);
            kill(current->pid, SIGUSR1);

            // notify_except
            sprintf(message, "MARKET %s %s %d %d;", new_order.type, new_order.product, new_order.num, new_order.price);
            notify_except(current->id, buffer);

            // match order_list
            match_orders();
            report();
        }
    } while (nfds != 0);
}

void pipe_handler(int sig)
{
    print_teardown(current->id);
    
    assert (epoll_ctl(epfd, EPOLL_CTL_DEL, current->trader_fd, &ev) != -1);
    assert (epoll_ctl(epfd, EPOLL_CTL_DEL, current->exchagne_fd, &ev) != -1);
    close(current->exchagne_fd);
    close(current->trader_fd);
    current->valid = 0;
}

int main(int argc, char **argv) {
    epfd = epoll_create1(0);
    signal(SIGUSR1, signal_handler);
    signal(SIGPIPE, pipe_handler);

    startup(argc, argv);
    wait_all();
    clean_all();

    return 0;
}

void startup(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage:\n"
                "./spx_exchange [product file] [trader 0] [trader 1] ... [trader n]\n");
        exit(1);
    }

    FILE* products = fopen(argv[1], "r");
    if (products == NULL) {
        fprintf(stderr, "file %s cannot be open\n", argv[1]);
        exit(2);
    }
    spx_log("Starting\n");

    char buffer[128];
    fgets(buffer, 128, products);
    trader_count = atoi(buffer);
    spx_log("Trading %d products: ", trader_count);
    for (int i = 0; i < trader_count; ++i) {
        fgets(buffer, 128, products);
        buffer[strlen(buffer) - 1] = 0; // remove endl
        add_products(buffer);
        if (i != trader_count - 1)
            printf("%s ", buffer);
        else
            printf("%s\n", buffer);
    }
    fclose(products);

    for (int i = 2; i < argc; ++i) {
        // name pipes
        char name[32];
        sprintf(name, FIFO_EXCHANGE, i - 2);
        mkfifo(name, 0666);
        spx_log("Created FIFO %s\n", name);

        sprintf(name, FIFO_TRADER, i - 2);
        mkfifo(name, 0666);
        spx_log("Created FIFO %s\n", name);

        spx_log("Starting trader %d (%s)\n", i - 2, argv[i]);
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
        // parent
        // add pid
        add_trader_node(i - 2, pid);
    }

    char message[128] = "MARKET OPEN;";
    notify_all(message);
}

void add_products(char *name)
{
    order_book_t *new_product = calloc(1, sizeof(order_book_t));
    strcpy(new_product->name, name);

    if (book == NULL) {
        book_tail = book = new_product;
    } else {
        book_tail->next = new_product;
        book_tail = new_product;
    }
}

void clean_book(order_book_t *head)
{
    order_book_t *node = head;
    while (node) {
        order_book_t *backup = node;
        node = node->next;
        free(backup);
    }
}

order_book_t* copy_book()
{
    order_book_t *old = book, *new = calloc(1, sizeof(order_book_t));
    order_book_t *tail = new;
    while (old) {
        memcpy(tail, old, sizeof(order_book_t));
        old = old->next;
        if (old) {
            tail->next = calloc(1, sizeof(order_book_t));
            tail = tail->next;
        }
    }
    return new;
}

void add_trader_node(int id, pid_t pid)
{
    trader_node_t *new_node = calloc(1, sizeof(trader_node_t));
    new_node->id = id;
    new_node->pid = pid;
    new_node->valid = 1;
    new_node->book = copy_book();

    char name[32];
    sprintf(name, FIFO_EXCHANGE, id);
    new_node->exchagne_fd = open(name, O_WRONLY);
    spx_log("Connected to %s\n", name);

    sprintf(name, FIFO_TRADER, id);
    new_node->trader_fd = open(name, O_RDONLY);
    spx_log("Connected to %s\n", name);

    // add to signal handler
    struct epoll_event ev;
    ev.data.fd = new_node->trader_fd;
    ev.events = EPOLLIN;
    assert (epoll_ctl(epfd, EPOLL_CTL_ADD, new_node->trader_fd, &ev) != -1);

    // add to list
    if (tail == NULL) {
        head = tail = new_node;
        return;
    }
    tail->next = new_node;
    tail = new_node;
}

trader_node_t *trader_lookup_fd(int fd)
{
    trader_node_t *node = head;
    while (node) {
        if (node->trader_fd == fd) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

trader_node_t *trader_lookup_id(int id)
{
    trader_node_t *node = head;
    while (node) {
        if (node->id == id) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

void notify_all(char *message)
{
    trader_node_t *node = head;
    while (node) {
        if (node->valid) {
            write(node->exchagne_fd, message, 128);
            kill(node->pid, SIGUSR1);
        }
        node = node->next;
    }
}

void notify_except(int id, char *message)
{
    trader_node_t *node = head;
    while (node) {
        if (node->id == id || !node->valid) {
            node = node->next;
            continue;
        }
        write(node->exchagne_fd, message, 128);
        kill(node->pid, SIGUSR1);
        node = node->next;
    }
}

void wait_all()
{
    trader_node_t *node = head;
    int status;
    while (node) {
        waitpid(node->pid, &status, 0);
        node = node->next;
    }
}

void clean_all()
{
    trader_node_t *node = head;
    while (node) {
        trader_node_t *backup = node;
        node = node->next;
        if (backup->valid) {
            close(backup->exchagne_fd);
            close(backup->trader_fd);
            print_teardown(backup->id);
        }
        clean_book(backup->book);
        free(backup);
    }
    clean_book(book);
    clean_orders(sell_head);
    clean_orders(buy_head);

    spx_log("Trading completed\n");
    spx_log("Exchange fees collected: $%d\n", fees);
}


void add_order_node(order_node_t **head, order_node_t **tail, order_node_t *new_node)
{
    if (*tail == NULL) {
        *head = *tail = new_node;
        return;
    }

    (*tail)->next = new_node;
    new_node->prev = *tail;
    *tail = new_node;
}

void add_order(order_node_t *node)
{
    order_node_t *new_node = calloc(1, sizeof(order_node_t));
    memcpy(new_node, node, sizeof(order_node_t));
    if (strstr(new_node->type, "BUY")) {
        add_order_node(&buy_head, &buy_tail, new_node);
    } else {
        add_order_node(&sell_head, &sell_tail, new_node);
    }
}

order_node_t *order_lookup(order_node_t *order_head, int id)
{
    order_node_t *node = order_head;
    while (node) {
        if (node->order_id == id) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

void order_remove(order_node_t **order_head, order_node_t **order_tail, int id)
{
    if ((*order_head)->order_id == id) {
        order_node_t *backup = *order_head;
        *order_head = (*order_head)->next;
        if (*order_head == NULL) {
            *order_tail = NULL;
        }
        free(backup);
        return;
    }

    order_node_t *node = *order_head;
    while (node->next) {
        if (node->next->order_id == id) {
            order_node_t *backup = node->next;
            node->next = backup->next;
            if (backup == *order_tail) {
                *order_tail = NULL;
            }
            free(backup);
            return;
        }
        node = node->next;
    }
}

// return true if b is empty
bool deal_order(order_node_t *a, order_node_t *b, int money)
{
    int deal_num = min(a->num, b->num);
    a->num -= deal_num;
    b->num -= deal_num;

    // fee
    int total = money * deal_num;
    int fee = (total + 50) / 100;
    fees += fee;

    // fill
    char fill_message[128];
    sprintf(fill_message, "FILL %d %d;", a->trader_id, deal_num);
    trader_node_t *trader = trader_lookup_id(a->trader_id);
    // fees
    order_book_t *trader_book = trader->book;
    while (trader_book) {
        if (strcmp(trader_book->name, a->product) == 0) {
            if (strstr(a->type, "BUY")) {
                trader_book->num += deal_num;
                trader_book->total_price -= total;
            }
            else {
                trader_book->num -= deal_num;
                trader_book->total_price += total;
            }
            break;
        }
        trader_book = trader_book->next;
    }

    write(trader->exchagne_fd, fill_message, 128);
    kill(trader->pid, SIGUSR1);

    sprintf(fill_message, "FILL %d %d;", b->trader_id, deal_num);
    trader = trader_lookup_id(b->trader_id);
    // fees
    trader_book = trader->book;
    while (trader_book) {
        if (strcmp(trader_book->name, b->product) == 0) {
            if (strstr(b->type, "BUY")) {
                trader_book->num += deal_num;
                trader_book->total_price -= total + fee;
            }
            else {
                trader_book->num -= deal_num;
                trader_book->total_price += total - fee;
            }
            break;
        }
        trader_book = trader_book->next;
    }
    write(trader->exchagne_fd, fill_message, 128);
    kill(trader->pid, SIGUSR1);

    spx_log("Match: Order %d [T%d], New Order %d [T%d], value: $%d, fee: $%d.\n", 
            a->order_id, a->trader_id, b->order_id, b->trader_id, total, fee);

    if (a->num == 0) {
        if (strstr(a->type, "BUY"))
            order_remove(&buy_head, &buy_head, a->order_id);
        else
            order_remove(&sell_head, &sell_head, a->order_id);
    }

    if (b->num == 0) {
        if (strstr(a->type, "BUY"))
            order_remove(&buy_head, &buy_head, b->order_id);
        else
            order_remove(&sell_head, &sell_head, b->order_id);
        return true;
    }

    return false;
}

order_node_t* copy_and_sort(order_node_t *head)
{
    order_node_t *old = head, *new = calloc(1, sizeof(order_node_t));
    order_node_t *tail = new;

    while (old) {
        memcpy(tail, old, sizeof(order_node_t));
        old = old->next;
        if (old) {
            tail->next = calloc(1, sizeof(order_node_t));
            tail = tail->next;
        }
    }

    // sort new head
    order_node_t* tempval;
    order_node_t *node = new->next, *key = NULL;
    // 插入排序
    while (node) {
        key = node;
        while (key != NULL && key->prev != NULL && key->price > key->prev->price) {
            // swap key and key->prev
            tempval = key->prev;
            key->prev = tempval->prev;
            tempval->next = key->next;

            if (tempval->prev)
                tempval->prev->next = key;
            tempval->prev = key;

            if (key->next)
                key->next->prev = tempval;
            key->next = tempval;
            key = key->prev;
        }
        node = node->next;
    }

    return new;
}

void clean_orders(order_node_t *head)
{
    order_node_t *node = head;
    while (node) {
        order_node_t *backup = node;
        node = node->next;
        free(backup);
    }
}

void report()
{
    printf(LOG_PREFIX "\t--ORDERBOOK--\n");
    // traverse orderbook
    order_book_t *node = book;

    // sort
    if (buy_tail)
        buy_tail->next = sell_head;
    if (sell_head)
        sell_head->prev = buy_tail;

    order_node_t *buy_and_sells = copy_and_sort(buy_head);
    while (node) {
        printf(LOG_PREFIX "\tProduct: %s; ", node->name);
        int buy_levels = 0, sell_levels = 0;

        // get levels
        order_node_t *temp = buy_and_sells;
        while (temp) {
            if (strcmp(temp->product, node->name))
            {
                temp = temp->next;
                continue;
            }
            if (temp->next && temp->next->price == temp->price && strcmp(temp->product, temp->next->product) == 0) {
                temp = temp->next;
                continue;
            }

            if (strstr(temp->type, "BUY"))
                buy_levels++;
            else
                sell_levels++;
            temp = temp->next;
        }
        printf("Buy levels: %d; Sell levels: %d\n", buy_levels, sell_levels);

        temp = buy_and_sells;
        int orders = 0;
        int nums = 0;
        while (temp) {
            if (strcmp(temp->product, node->name))
            {
                temp = temp->next;
                continue;
            }

            if (temp->next && temp->next->price == temp->price && strcmp(temp->product, temp->next->product) == 0) {
                temp = temp->next;
                orders++;
                nums += temp->num;
                continue;
            }

            orders++;
            nums += temp->num;
            if (orders > 1)
                printf(LOG_PREFIX "\t\t%s %d @ $%d (%d orders)\n", temp->type, nums, temp->price, orders);
            else
                printf(LOG_PREFIX "\t\t%s %d @ $%d (%d order)\n", temp->type, nums, temp->price, orders);

            temp = temp->next;
        }
        node = node->next;
    }
    clean_orders(buy_and_sells);
    if (buy_tail)
        buy_tail->next = NULL;
    if (sell_head)
        sell_head->prev = NULL;

    printf(LOG_PREFIX "\t--POSITIONS--\n");
    trader_node_t *trader = head;
    while (trader) {
        printf(LOG_PREFIX "\tTrader %d: ", trader->id);
        order_book_t *trader_book = trader->book;
        while (trader_book) {
            if (trader_book->next)
                printf("%s %d ($%d), ", trader_book->name, trader_book->num, trader_book->total_price);
            else
                printf("%s %d ($%d)\n", trader_book->name, trader_book->num, trader_book->total_price);
            trader_book = trader_book->next;
        }
        trader = trader->next;
    }
}

void match_orders()
{
    if (buy_tail == NULL || sell_tail == NULL)
        return;
    order_node_t *node = buy_head;
    while (node) {
        if (node->price >= sell_tail->price) {
            if (deal_order(node, sell_tail, sell_tail->price)) {
                break;
            }
        }
        node = node->next;
    }
    node = sell_head;
    while (node) {
        if (node->price <= buy_tail->price) {
            if (deal_order(node, sell_tail, sell_tail->price)) {
                break;
            }
        }
        node = node->next;
    }
}

void print_teardown(int id)
{
    char name[32];
    sprintf(name, FIFO_EXCHANGE, id);
    unlink(name);
    sprintf(name, FIFO_TRADER, id);
    unlink(name);

    spx_log("Trader %d disconnected\n", id);
}
