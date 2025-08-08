CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra
SRC := $(wildcard src/*.c)

codex: $(SRC) include/codex_core.h
	$(CC) $(CFLAGS) $(SRC) -Iinclude -o $@

clean:
	rm -f codex

.PHONY: clean
