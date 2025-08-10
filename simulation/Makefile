# Makefile
CC      ?= gcc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra
LIBS    := -lm            # na MinGW je OK, když by zlobilo, klidně smaž

TARGET  := pit_sim
SRC     := pit_sim.c

# .exe na Windows
ifeq ($(OS),Windows_NT)
  EXE := .exe
endif

BIN := $(TARGET)$(EXE)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LIBS)

.PHONY: clean
clean:
	rm -f $(TARGET) $(TARGET).exe
