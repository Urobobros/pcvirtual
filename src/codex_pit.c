/* codex_pit.c */

#include "codex_pit.h"
#include "codex_core.h"
#include "codex_pic.h"

#include <string.h>

#ifdef _WIN32
#include <winhvplatform.h>
#endif


static uint16_t pit_get_ch0(CodexPit* pit) {
#ifdef _WIN32
    if (!pit->ch0_programmed) return 0;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    uint64_t ticks = (uint64_t)(now.QuadPart - pit->ch0_start.QuadPart);
    uint64_t pit_ticks = (uint64_t)((double)ticks * 1193182.0 / pit->perf_freq.QuadPart);
    uint32_t reload = pit->reload ? pit->reload : 0x10000;
    uint16_t down = (uint16_t)(reload - (pit_ticks % reload));
    return down ? down : (uint16_t)reload;
#else
    (void)pit; return 0;
#endif
}

static uint16_t pit_get_ch1(CodexPit* pit) {
#ifdef _WIN32
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    uint64_t ticks = (uint64_t)(now.QuadPart - pit->ch1_start.QuadPart);
    uint64_t pit_ticks = (uint64_t)((double)ticks * 1193182.0 / pit->perf_freq.QuadPart);
    uint16_t reload = pit->ch1_reload ? pit->ch1_reload : 0x10000;
    uint16_t down = (uint16_t)(reload - (pit_ticks % reload));
    return down ? down : reload;
#else
    (void)pit; return 0;
#endif
}
void codex_pit_init(CodexPit* pit) {
    if (!pit) return;
    memset(pit, 0, sizeof(*pit));
#ifdef _WIN32
    QueryPerformanceFrequency(&pit->perf_freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    pit->reload = 0x10000; /* channel 0 default */
    pit->ch0_programmed = 0;
    pit->ch0_rw = 3;
    pit->ch1_rw = 3;
    pit->period_ticks = (uint64_t)((double)pit->perf_freq.QuadPart * pit->reload / 1193182.0);
    pit->next_fire.QuadPart = now.QuadPart + pit->period_ticks;
    pit->ch0_start = now; 
    pit->ch1_start = now;
    pit->ch1_reload = 0;
#endif
}

#ifdef _WIN32
static inline void pit_rearm_ch0(CodexPit* pit) {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    pit->period_ticks = (uint64_t)((double)pit->perf_freq.QuadPart * (pit->reload ? pit->reload : 0x10000) / 1193182.0);
    pit->next_fire.QuadPart = now.QuadPart + pit->period_ticks;
    pit->ch0_start = now;
}
#endif

void codex_pit_io_write(CodexPit* pit, uint16_t port, uint8_t value) {
    if (!pit) return;
    switch (port) {
    case 0x40: { /* channel 0 data */
        if (pit->ch0_rw == 1) {           /* LSB only */
            pit->reload = (pit->reload & 0xFF00) | value;
            if ((pit->reload & 0xFF) == 0 && (pit->reload & 0xFF00) == 0) pit->reload = 0x10000;
            pit->ch0_programmed = 1;
            #ifdef _WIN32
                pit_rearm_ch0(pit);
            #endif

        } else if (pit->ch0_rw == 2) {    /* MSB only */
            pit->reload = (pit->reload & 0x00FF) | ((uint32_t)value << 8);
            if ((pit->reload & 0xFFFF) == 0) pit->reload = 0x10000;
            pit->ch0_programmed = 1;
            #ifdef _WIN32
                pit_rearm_ch0(pit);
            #endif
        } else {                          /* LSB+MSB */
            if (!pit->expect_msb) {
                pit->latch_lsb = value; pit->expect_msb = 1;
                break;
            }
            pit->reload = (uint32_t)(pit->latch_lsb | (value << 8));
            if (pit->reload == 0) pit->reload = 0x10000;
            pit->ch0_programmed = 1;
            #ifdef _WIN32
                pit_rearm_ch0(pit);
            #endif
        #ifdef _WIN32
            pit->period_ticks = (uint64_t)((double)pit->perf_freq.QuadPart * pit->reload / 1193182.0);
            LARGE_INTEGER now; QueryPerformanceCounter(&now);
            pit->next_fire.QuadPart = now.QuadPart + pit->period_ticks;
            pit->ch0_start = now;
        #endif
            pit->expect_msb = 0;
        }
        break;
    }    
    case 0x41: { /* channel 1 data */
        if (pit->ch1_rw == 1) {           /* LSB only */
            pit->ch1_reload = (pit->ch1_reload & 0xFF00) | value;
        #ifdef _WIN32
            QueryPerformanceCounter(&pit->ch1_start);
        #endif
        } else if (pit->ch1_rw == 2) {    /* MSB only */
            pit->ch1_reload = (pit->ch1_reload & 0x00FF) | ((uint16_t)value << 8);
        #ifdef _WIN32
          QueryPerformanceCounter(&pit->ch1_start);
        #endif
        } else {                          /* LSB+MSB */
            if (!pit->ch1_expect_msb) { pit->ch1_lsb = value; pit->ch1_expect_msb = 1; break; }
            pit->ch1_reload = (uint16_t)(pit->ch1_lsb | (value << 8));
    #ifdef _WIN32
            QueryPerformanceCounter(&pit->ch1_start);
    #endif
            pit->ch1_expect_msb = 0;
        }
        break; 
    }
    case 0x43: { /* control word */
        int ch = (value >> 6) & 0x3;
        int rw = (value >> 4) & 0x3;      /* 0=latch, 1=LSB, 2=MSB, 3=LSB+MSB */
        int mode = (value >> 1) & 0x7;    /* zatím nevyužíváme */
        (void)mode; // pokud ho dočasně nepoužiješ
    
        if (rw == 0x00) { /* latch */
            if (ch == 0x00) { pit->ch0_latch = pit_get_ch0(pit); pit->ch0_latched = 1; pit->ch0_flip = 0; }
            else if (ch == 0x01) { pit->ch1_latch = pit_get_ch1(pit); pit->ch1_latched = 1; pit->ch1_flip = 0; }
        } else {
            if (ch == 0x00) { pit->ch0_rw = (uint8_t)rw; pit->expect_msb = 0; }     /* ch0 */
            else if (ch == 0x01) { pit->ch1_rw = (uint8_t)rw; pit->ch1_expect_msb = 0; } /* ch1 */
          /* (modes můžeš ignorovat pro MVP) */
        }
        break; 
    }
    default:
        break;
    }
}

uint8_t codex_pit_io_read(CodexPit* pit, uint16_t port) {
    if (!pit) return 0;
    switch (port) {
    case 0x40: { /* ch0 */
        uint16_t val = pit->ch0_latched ? pit->ch0_latch : pit_get_ch0(pit);
        uint8_t ret = pit->ch0_flip ? (uint8_t)(val >> 8) : (uint8_t)(val & 0xFF);
        pit->ch0_flip ^= 1;
        if (!pit->ch0_flip && pit->ch0_latched)
            pit->ch0_latched = 0;
        return ret;
    }
    case 0x41: { /* ch1 */
        uint16_t val = pit->ch1_latched ? pit->ch1_latch : pit_get_ch1(pit);
        uint8_t ret = pit->ch1_flip ? (uint8_t)(val >> 8) : (uint8_t)(val & 0xFF);
        pit->ch1_flip ^= 1;
        if (!pit->ch1_flip && pit->ch1_latched)
            pit->ch1_latched = 0;
        return ret;
    }
    default:
        break;
    }
    return 0;
}

void codex_pit_update(CodexPit* pit, struct CodexCore* core) {
    if (!pit || !core) return;
#ifdef _WIN32
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    while (now.QuadPart >= pit->next_fire.QuadPart) {
        codex_pic_pulse_irq(&core->pic, core, 0);  // IRQ0 edge
        pit->next_fire.QuadPart += pit->period_ticks;
        /* volitelně: log nebo počítadlo „missed ticks“ */
    }
#else
    (void)core;
#endif
}
