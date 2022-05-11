CC=gcc
CFLAGS=-Wall -Werror -Wvla -O0 -std=c11 -g -fsanitize=address,leak $(DEFINE)
LDFLAGS=-lm
BINARIES=spx_exchange spx_trader spx_test_trader

all: $(BINARIES)

.PHONY: clean zip reset
clean:
	rm -f $(BINARIES) /tmp/spx*

zip:
	zip submission.zip spx_common.h spx_exchange.c spx_exchange.h spx_trader.c spx_trader.h tests/*

reset:
	rm -f /tmp/spx*
