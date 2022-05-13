#include "spx_common.h"
#include "spx_trader.h"
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>

int trader_fd;
int exchange_fd;
char buffer[128];
char message[128];
int count = 0;
bool end;
int order_id = 0;
int orders = 0;

void handle_signal(int sig)
{
    count++;
    return;
}

void connect_pipes(int id);
void event_loop();
void wait_for_open();
void wait_for_accepted();

void pipe_handler(int sig)
{
    printf("signal count : %d\n", count);
    exit(0);
}

int main(int argc, char ** argv) {
    if (argc < 2) {
        printf("Not enough arguments\n");
        return 1;
    }

    // register signal handler
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_handler = handle_signal;
    sigaction(SIGUSR1, &sa, NULL);
    signal(SIGPIPE, pipe_handler);

    // connect pipes
    connect_pipes(atoi(argv[1]));
    // wait for exchange update (MARKET message)
    wait_for_open();

    // event loop:
    event_loop();

    // send order
    // wait for exchange confirmation (ACCEPTED message)
    wait_for_accepted();

    close(exchange_fd);
    close(trader_fd);
}

void handle_message(char *buffer)
{
    if (strstr(buffer, "SELL")) {
        char *endline = strstr(buffer, ";");
        if (endline) {
            *endline = 0;
        } else {
            printf("error command : %s\n", buffer);
            return;
        }
        
        char *token = strtok(buffer, " "); // MARKET
        assert(token);
        token = strtok(NULL, " "); // SELL
        assert(token);
        token = strtok(NULL, " "); // name
        assert(token);
        char name[20];
        strcpy(name, token);
        token = strtok(NULL, " "); // qty
        assert(token);
        int qty = atoi(token);
        token = strtok(NULL, " "); // qty
        assert(token);
        int price = atoi(token);
        if (qty >= 1000) {
            end = true;
            return;
        }
        orders++;
        sprintf(message, "BUY %d %s %d %d;", order_id++, name, qty, price);
        write(trader_fd, message, strlen(message));
        kill(getppid(), SIGUSR1);
    } else if (strstr(buffer, "FILL")) {
        orders--;
    }
}

void event_loop()
{
    while (!end) {
        read_message(exchange_fd, buffer);
        count--;
        handle_message(buffer);
    }
}

void wait_for_open()
{
    while(true) {
        if(read_message(exchange_fd, buffer) <= 0) {
            printf("get error message %s\n", buffer);
            break;
        }
        count--;
        if (strstr(buffer, "OPEN"))
            break;
    }
}

void wait_for_accepted()
{
    while(orders) {
        if(read_message(exchange_fd, buffer) <= 0) {
            printf("get error message %s\n", buffer);
            break;
        }
        count--;
        if (strstr(buffer, "FILL")) {
            orders--;
        }
    }
}

void connect_pipes(int id)
{
    // connect to named pipes
    char name[32];
    sprintf(name, FIFO_EXCHANGE, id);
    exchange_fd = open(name, O_RDONLY);

    sprintf(name, FIFO_TRADER, id);
    trader_fd = open(name, O_WRONLY);
}
