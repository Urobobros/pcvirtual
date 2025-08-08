CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra

# Enable verbose I/O port logging by default so hardware interactions are
# visible without requiring extra build flags.
CFLAGS += -DPORT_DEBUG
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
