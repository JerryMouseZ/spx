CC=gcc
CFLAGS=-Wall -Werror -Wvla -O0 -std=c11 -g -fsanitize=address,leak $(DEFINE)
LDFLAGS=-lm
BINARIES=spx_exchange spx_trader spx_test_trader

all: $(BINARIES)

.PHONY: clean zip reset
clean:
	rm -f $(BINARIES) /tmp/spx*

reset:
	rm -f /tmp/spx*
