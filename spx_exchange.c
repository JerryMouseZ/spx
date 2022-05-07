/**
 * comp2017 - assignment 3
 * <your name>
 * <your unikey>
 */

#include "spx_exchange.h"
#include <fcntl.h>
#include <signal.h>
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
order_node_t *order_head = NULL;
order_node_t *order_tail = NULL;

int trader_count = 0;
int fees = 0;

#define MAX_EVENTS 10
int epfd = -1;
struct epoll_event ev, events[MAX_EVENTS];

void signal_handler(int sig)
{
    int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
    if (nfds == -1) {
        perror("epoll_wait");
        exit(EXIT_FAILURE);
    }
    
    char buffer[128];
    char message[128];
    for (int i = 0; i < nfds; ++i) {
        // look up events[n].data.fd and send
        int fd = events[i].data.fd;
        trader_node_t *node = trader_lookup_fd(fd);
        assert(node != NULL);
        int bytes = read(fd, buffer, 128);
        if (bytes == 0) {
            print_teardown(node->id);
        }
        
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
        new_order.trader_id = node->id;

        order_node_t *order_check = order_lookup(new_order.order_id);
        if (order_check) {
            // cancelled if num and price == 0
            // amended if order_id is the same
            if (new_order.num == 0 && new_order.price == 0) {
                // delete the node
                order_remove(new_order.order_id);
                sprintf(message, "CANCELLED %d;", new_order.order_id);
            } else {
                order_check->num = new_order.num;
                order_check->price = new_order.price;
                sprintf(message, "AMENDED %d;", new_order.order_id);
            }
        } else {
            add_order(&new_order);
            sprintf(message, "ACCEPTED %d;", new_order.order_id);
        }

        write(node->exchagne_fd, message, 128);
        kill(node->pid, SIGUSR1);

        // notify_except
        sprintf(message, "MARKET %s %s %d %d;", new_order.type, new_order.product, new_order.num, new_order.price);
        notify_except(node->id, buffer);

        // match order_list
        match_orders();
    }
}

int main(int argc, char **argv) {
    epfd = epoll_create1(0);
    signal(SIGUSR1, signal_handler);
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
    char buffer[128];
    fgets(buffer, 128, products);
    trader_count = atoi(buffer);
    fclose(products);

    // name pipes
    for (int i = 0; i < trader_count; ++i)
    {
        char name[32];
        sprintf(name, FIFO_EXCHANGE, i);
        mkfifo(name, 0666);

        sprintf(name, FIFO_TRADER, i);
        mkfifo(name, 0666);
    }

    // child process
    for (int i = 2; i < argc; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            // child
            char command[12];
            sprintf(command, "%d", i - 2);
            execl(argv[i], command);
            // never reach here
            assert(0);
        }

        // father
        // add pid
        add_trader_node(i - 2, pid);
    }

    char message[128] = "MARKET OPEN;";
    notify_all(message);
}

void add_trader_node(int id, pid_t pid)
{
    trader_node_t *new_node = calloc(1, sizeof(trader_node_t));
    new_node->id = id;
    new_node->pid = pid;
    char name[32];
    sprintf(name, FIFO_EXCHANGE, id);
    new_node->exchagne_fd = open(name, O_WRONLY);

    sprintf(name, FIFO_TRADER, id);
    new_node->trader_fd = open(name, O_RDONLY);

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
        write(node->exchagne_fd, message, 128);
        kill(node->pid, SIGUSR1);
        node = node->next;
    }
}

void notify_except(int id, char *message)
{
    trader_node_t *node = head;
    while (node) {
        if (node->id == id)
            continue;
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
        free(backup);
    }
}

void add_order(order_node_t *node)
{
    order_node_t *new_node = calloc(1, sizeof(order_node_t));
    memcpy(new_node, node, sizeof(order_node_t));
    if (order_tail == NULL) {
        order_head = order_tail = new_node;
        return;
    }

    order_tail->next = new_node;
    order_tail = new_node;
}

order_node_t *order_lookup(int id)
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

void order_remove(int id)
{
    if (order_head->order_id == id) {
        order_node_t *backup = order_head;
        order_head = order_head->next;
        if (order_head == NULL) {
            order_tail = NULL;
        }
        free(backup);
        return;
    }

    order_node_t *node = order_head;
    while (node->next) {
        if (node->next->order_id == id) {
            order_node_t *backup = node->next;
            node->next = backup->next;
            if (backup == order_tail) {
                order_tail = NULL;
            }
            free(backup);
            return;
        }
        node = node->next;
    }
}

// a is the first and b is the last
bool deal_order(order_node_t *a, order_node_t *b, int money)
{
    int deal_num = min(a->num, b->num);
    a->num -= deal_num;
    b->num -= deal_num;
    
    // fee
    int total = money * deal_num;
    int fee = (total + 99) / 100;
    fees += fee;

    // fill
    char fill_message[128];
    sprintf(fill_message, "FILL %d %d;", a->trader_id, deal_num);
    trader_node_t *trader = trader_lookup_id(a->trader_id);
    write(trader->exchagne_fd, fill_message, 128);
    kill(trader->pid, SIGUSR1);

    sprintf(fill_message, "FILL %d %d;", b->trader_id, deal_num);
    trader = trader_lookup_id(b->trader_id);
    write(trader->exchagne_fd, fill_message, 128);
    kill(trader->pid, SIGUSR1);

    if (a->num == 0) {
        order_remove(a->order_id);
    }

    if (b->num == 0) {
        order_remove(b->order_id);
        return true;
    }
    
    return false;
}

void match_orders()
{
    order_node_t *node = order_head;
    while (node != order_tail) {
        if (strcmp(node->type, order_tail->type)) {
            if (strstr(node->type, "BUY")) {
                if (node->price >= order_tail->price) {
                    if (deal_order(node, order_tail, node->price)) {
                        break;
                    }
                }
            } else {
                if (node->price <= order_tail->price) {
                    if (deal_order(node, order_tail, order_tail->price)) {
                        break;
                    }
                }
            }
        }
        node = node->next;
    }
}

void print_teardown(int id)
{
    spx_log("Trader %d disconnected\n", id);
}
