#include "spx_common.h"
#include "spx_trader.h"
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
int sigwait(const sigset_t *restrict set, int *restrict sig);

int trader_fd;
int exchange_fd;
char buffer[128];
char message[128];
int count = 0;
bool end;
int order_id = 0;
FILE *f;

void handle_signal(int sig)
{
    count++;
    return;
}


int main(int argc, char ** argv) {
    if (argc < 2) {
        printf("Not enough arguments\n");
        return 1;
    }
    // register signal handler

    f = fopen("input2.txt", "r");
    
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_handler = handle_signal;
    sigaction(SIGUSR1, &sa, NULL);

    // connect to named pipes
    int id = atoi(argv[1]);
    char name[32];
    sprintf(name, FIFO_EXCHANGE, id);
    exchange_fd = open(name, O_RDONLY);

    sprintf(name, FIFO_TRADER, id);
    trader_fd = open(name, O_WRONLY);

    // event loop:
    while (1) {
        while (count == 0)
            pause();
        assert(read_message(exchange_fd, buffer) > 0);
        count--;
        printf("recving %s\n", buffer);
        if (strstr(buffer, "OPEN"))
            break;
    }
    
    /* int buys = 1; */
    /* while (1) { */
    /*     while (count == 0) */
    /*         pause(); */
    /*     assert(read_message(exchange_fd, buffer) > 0); */
    /*     count--; */
    /*     printf("recving %s\n", buffer); */
    /*     if (strstr(buffer, "BUY")) { */
    /*         buys--; */
    /*         if (buys == 0) */
    /*             break; */
    /*     } */
    /* } */

    int orders = 0;
    while (1) {
        if (fgets(message, 128, f) == NULL) {
            end = true;
            break;
        }

        if (strstr(message, "quit")) {
            end = true;
            break;
        }

        orders++;
        write(trader_fd, message, strlen(message) - 1);
        kill(getppid(), SIGUSR1);

        while (count == 0)
            pause();
        read_message(exchange_fd, buffer);
        count--;
        printf("recving %s\n", buffer);
    }

    // wait for exchange update (MARKET message)
    // send order
    // wait for exchange confirmation (ACCEPTED message)

    close(exchange_fd);
    close(trader_fd);
}
