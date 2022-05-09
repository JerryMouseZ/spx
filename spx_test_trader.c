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
bool end;
int order_id = 0;

void handle_signal(int sig)
{
    while (read(exchange_fd, buffer, 128) > 0) {
        printf("recving %s\n", buffer);
        int yes;
        scanf("%d", &yes);
        if (yes) {
            scanf("%s", message);
            write(trader_fd, message, 128);
            kill(getppid(), SIGUSR1);
        }
    }
}


int main(int argc, char ** argv) {
    if (argc < 2) {
        printf("Not enough arguments\n");
        return 1;
    }

    // connect to named pipes
    int id = atoi(argv[1]);
    char name[32];
    sprintf(name, FIFO_EXCHANGE, id);
    exchange_fd = open(name, O_RDONLY);

    sprintf(name, FIFO_TRADER, id);
    trader_fd = open(name, O_WRONLY);

    // register signal handler
    sigset_t mask, oldmask;
    signal(SIGUSR1, handle_signal);
    sigemptyset (&mask);
    sigaddset (&mask, SIGUSR1);

    // event loop:
    sigprocmask (SIG_BLOCK, &mask, &oldmask);
    while (!end)
        sigsuspend (&oldmask);
    sigprocmask (SIG_UNBLOCK, &mask, NULL);
    // wait for exchange update (MARKET message)
    // send order
    // wait for exchange confirmation (ACCEPTED message)
    //

    close(exchange_fd);
    close(trader_fd);
}
