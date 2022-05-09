CC=gcc
CFLAGS=-Wall -Werror -Wvla -O0 -std=c11 -g -fsanitize=address,leak $(DEFINE)
LDFLAGS=-lm
BINARIES=spx_exchange spx_trader

all: $(BINARIES)

.PHONY: clean zip
clean:
	rm -f $(BINARIES)

zip:
	zip submission.zip spx_common.h spx_exchange.c spx_exchange.h spx_trader.c spx_trader.h tests/*
