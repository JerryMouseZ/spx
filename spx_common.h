#ifndef SPX_COMMON_H
#define SPX_COMMON_H

#define _POSIX_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define FIFO_EXCHANGE "/tmp/spx_exchange_%d"
#define FIFO_TRADER "/tmp/spx_trader_%d"
#define FEE_PERCENTAGE 1

static inline int read_message(int fd, char *buffer)
{
    int i = 0;
    int nbytes = 0;
    while((nbytes = read(fd, buffer + i, 1)) > 0) {
        if (buffer[i] == -1)
            return -1;
        if (buffer[i] == ';') {
            buffer[i + 1] = 0;
            return i + 1;
        }
        i++;
    }
    return -1;
}

#endif
