/* codex_dma.h */

#pragma once
#include <stdint.h>

typedef struct {
    uint8_t flipflop;         // 0=LSB next, 1=MSB next (reset přes 0x0C)
    uint16_t addr[4];         // kanály 0..3
    uint16_t count[4];
    uint8_t page[16];         // 0x80..0x8F
    uint8_t mode;             // poslední zápis do 0x0B
    uint8_t mask;             // 0x0A (bitové maskování kanálů)
} CodexDma;

void dma_init(CodexDma* d);
uint8_t dma_io_read(CodexDma* d, uint16_t port);
void    dma_io_write(CodexDma* d, uint16_t port, uint8_t val);
