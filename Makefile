CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -g

# Verbózní logy I/O portů
CFLAGS += -DPORT_DEBUG

LDFLAGS ?=
SRC := $(wildcard src/*.c)

LIBS :=
ifeq ($(OS),Windows_NT)
  LIBS += -lWinHvPlatform
endif

# Přepínač pro end-to-end test s DI času (0=fallback, 1=DI)
PIT_TIME_SOURCE ?= 1

all: codex

codex: $(SRC) include/codex_core.h
	$(CC) $(CFLAGS) $(SRC) -Iinclude -o $@ $(LDFLAGS) $(LIBS)

clean:
	rm -f codex pit_test pit_test_integration pit_test_unit

# Integrační test – propasujeme HAVE_PIT_TIME_SOURCE z proměnné
pit_test_integration: src/codex_pit.c tests/pit_test_integration.c include/codex_pit.h
	$(CC) $(CFLAGS) -Iinclude -DHAVE_PIT_TIME_SOURCE=$(PIT_TIME_SOURCE) \
		src/codex_pit.c tests/pit_test_integration.c -o $@

pit_test_unit: src/codex_pit.c tests/pit_test_unit.c include/codex_pit.h
	$(CC) $(CFLAGS) -Iinclude src/codex_pit.c tests/pit_test_unit.c -o $@

test: pit_test_integration
	./pit_test_integration

# End-to-end běh testu s DI času (po implementaci setteru)
test-e2e: PIT_TIME_SOURCE=1
test-e2e: pit_test_integration
	./pit_test_integration

.PHONY: all clean test test-e2e pit_test_integration pit_test_unit
