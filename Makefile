CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra
SRC := $(wildcard src/*.c)

LIBS :=
ifeq ($(OS),Windows_NT)
LIBS += -lWinHvPlatform
endif

codex: $(SRC) include/codex_core.h
	$(CC) $(CFLAGS) $(SRC) -Iinclude -o $@ $(LIBS)

clean:
	rm -f codex

.PHONY: clean
