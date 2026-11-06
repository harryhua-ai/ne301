CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11

ws_fifo_harness: ws_fifo_harness.c
	$(CC) $(CFLAGS) -o $@ $< -lpthread

run: ws_fifo_harness
	./ws_fifo_harness

clean:
	rm -f ws_fifo_harness

.PHONY: run clean
