CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -g

# Enable verbose I/O port logging by default so hardware interactions are
# visible without requiring extra build flags.

CFLAGS += -DPORT_DEBUG
LDFLAGS ?=
SRC := $(wildcard src/*.c)

LIBS :=
ifeq ($(OS),Windows_NT)
  LIBS += -lWinHvPlatform
endif

codex: $(SRC) include/codex_core.h
	$(CC) $(CFLAGS) $(SRC) -Iinclude -o $@ $(LDFLAGS) $(LIBS)

clean:
	rm -f codex pit_test

pit_test: src/codex_pit.c tests/pit_test.c include/codex_pit.h
	$(CC) $(CFLAGS) -Iinclude src/codex_pit.c tests/pit_test.c -o $@

test: pit_test
	./pit_test

.PHONY: clean test pit_test
