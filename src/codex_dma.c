/* codex_dma.c */

#include "codex_dma.h"
#include <string.h>

void dma_init(CodexDma* d) {
    memset(d, 0, sizeof(*d));
    for (int i=0;i<4;i++) d->count[i] = 0xFFFF;
}

static inline int chan_from_port(uint16_t p) {
    // 0x00/0x01 addr ch0, 0x02/0x03 count ch0, … po dvou portech
    if (p <= 0x07) return (p/2) & 3; else return -1;
}

uint8_t dma_io_read(CodexDma* d, uint16_t port) {
    if (port <= 0x07) {
        int ch = chan_from_port(port);
        if (ch < 0) return 0xFF;
        uint16_t *arr = (port & 1) ? &d->count[ch] : &d->addr[ch];
        uint8_t ret = d->flipflop ? (*arr >> 8) & 0xFF : (*arr & 0xFF);
        d->flipflop ^= 1;
        return ret;
    } else if (port == 0x08) {
        // DMA status – pro MVP klidně 0
        return 0x00;
    } else if (port == 0x0D) {
        // Master reset – vymaž flipflop
        d->flipflop = 0;
        return 0xFF;
    } else if (port >= 0x80 && port <= 0x8F) {
        return d->page[port - 0x80];
    }
    return 0xFF;
}

void dma_io_write(CodexDma* d, uint16_t port, uint8_t val) {
    if (port <= 0x07) {
        int ch = chan_from_port(port);
        if (ch >= 0) {
            uint16_t *arr = (port & 1) ? &d->count[ch] : &d->addr[ch];
            if (!d->flipflop) *arr = (*arr & 0xFF00) | val;
            else              *arr = (*arr & 0x00FF) | ((uint16_t)val << 8);
            d->flipflop ^= 1;
        }
    } else if (port == 0x08) {
        /* write status – ignoruj */
    } else if (port == 0x09) {
        /* request – ignoruj */
    } else if (port == 0x0A) {
        d->mask = val; /* mask register */
    } else if (port == 0x0B) {
        d->mode = val; /* mode register */
    } else if (port == 0x0C) {
        d->flipflop = 0; /* clear byte pointer (flip-flop) */
    } else if (port == 0x0D) {
        /* master clear – reset všeho jednoduše */
        dma_init(d);
    } else if (port >= 0x80 && port <= 0x8F) {
        d->page[port - 0x80] = val;
    }
}
