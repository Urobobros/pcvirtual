/* codex_pit.c */

#include "codex_pit.h"
#include "codex_core.h"
#include "codex_pic.h"

#include <string.h>
#include <stdint.h>

#ifdef _WIN32
  #include <windows.h>
#endif

#ifndef PIT_HZ
#define PIT_HZ 1193182.0   /* 1.193182 MHz */
#endif

/* === Pomocné výpočty aktuální hodnoty čítače (down-counter) ============= */

static uint16_t pit_get_ch0(CodexPit* pit) {
#ifdef _WIN32
    if (!pit->ch0_programmed) return 0;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);

    /* host ticks -> PIT ticky, ZAOKROUHLENÉ na nejbližší tik (floor(x+0.5)) */
    uint64_t host_ticks = (uint64_t)(now.QuadPart - pit->ch0_start.QuadPart);
    double   pit_ticks_f = (double)host_ticks * PIT_HZ / (double)pit->perf_freq.QuadPart;
    uint64_t pit_ticks   = (uint64_t)(pit_ticks_f + 0.5);

    uint32_t reload = (pit->reload ? pit->reload : 0x10000u);
    uint16_t down   = (uint16_t)(reload - (pit_ticks % reload));
    return down ? down : (uint16_t)reload;
#else
    (void)pit; return 0;
#endif
}

static uint16_t pit_get_ch1(CodexPit* pit) {
#ifdef _WIN32
    LARGE_INTEGER now; QueryPerformanceCounter(&now);

    /* host ticks -> PIT ticky, ZAOKROUHLENÉ na nejbližší tik (floor(x+0.5)) */
    uint64_t host_ticks = (uint64_t)(now.QuadPart - pit->ch1_start.QuadPart);
    double   pit_ticks_f = (double)host_ticks * PIT_HZ / (double)pit->perf_freq.QuadPart;
    uint64_t pit_ticks   = (uint64_t)(pit_ticks_f + 0.5);

    uint16_t reload = (uint16_t)(pit->ch1_reload ? pit->ch1_reload : 0x10000u);
    uint16_t down   = (uint16_t)(reload - (pit_ticks % reload));
    return down ? down : reload;
#else
    (void)pit; return 0;
#endif
}

/* === Init ================================================================ */

void codex_pit_init(CodexPit* pit) {
    if (!pit) return;
    memset(pit, 0, sizeof(*pit));
#ifdef _WIN32
    QueryPerformanceFrequency(&pit->perf_freq);

    LARGE_INTEGER now; QueryPerformanceCounter(&now);

    pit->reload          = 0x10000;      /* CH0 default */
    pit->ch0_programmed  = 0;
    pit->ch0_rw          = 3;            /* LSB+MSB */
    pit->ch1_rw          = 3;            /* LSB+MSB */
    pit->ch1_reload      = 0;

    /* používáme PIT_HZ konzistentně */
    pit->period_ticks    = (uint64_t)((double)pit->perf_freq.QuadPart * pit->reload / PIT_HZ);
    pit->next_fire.QuadPart = now.QuadPart + pit->period_ticks;

    pit->ch0_start       = now;
    pit->ch1_start       = now;

    pit->ch0_latched     = 0;
    pit->ch1_latched     = 0;
    pit->ch0_flip        = 0;
    pit->ch1_flip        = 0;
    pit->expect_msb      = 0;
    pit->ch1_expect_msb  = 0;
#endif
}

#ifdef _WIN32
static inline void pit_rearm_ch0(CodexPit* pit) {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    uint32_t reload = (pit->reload ? pit->reload : 0x10000u);

    /* přepočet periody pro IRQ0 na základě PIT_HZ */
    pit->period_ticks       = (uint64_t)((double)pit->perf_freq.QuadPart * reload / PIT_HZ);
    pit->next_fire.QuadPart = now.QuadPart + pit->period_ticks;
    pit->ch0_start          = now;
}
#endif

/* === I/O – zápisy ======================================================== */

void codex_pit_io_write(CodexPit* pit, uint16_t port, uint8_t value) {
    if (!pit) return;

    switch (port) {
    case 0x40: { /* CH0 data */
        if (pit->ch0_rw == 1) {                /* LSB only */
            pit->reload = (pit->reload & 0xFF00u) | value;
            if ((pit->reload & 0xFFFFu) == 0) pit->reload = 0x10000u;
            pit->ch0_programmed = 1;
#ifdef _WIN32
            pit_rearm_ch0(pit);
#endif
        } else if (pit->ch0_rw == 2) {         /* MSB only */
            pit->reload = (pit->reload & 0x00FFu) | ((uint32_t)value << 8);
            if ((pit->reload & 0xFFFFu) == 0) pit->reload = 0x10000u;
            pit->ch0_programmed = 1;
#ifdef _WIN32
            pit_rearm_ch0(pit);
#endif
        } else {                               /* LSB+MSB */
            if (!pit->expect_msb) {
                pit->latch_lsb = value;
                pit->expect_msb = 1;
                break;
            }
            pit->reload = (uint32_t)(pit->latch_lsb | (value << 8));
            if (pit->reload == 0) pit->reload = 0x10000u;
            pit->ch0_programmed = 1;
#ifdef _WIN32
            pit_rearm_ch0(pit);
#endif
            pit->expect_msb = 0;
        }
        break;
    }

    case 0x41: { /* CH1 data */
        if (pit->ch1_rw == 1) {                /* LSB only */
            pit->ch1_reload = (pit->ch1_reload & 0xFF00u) | value;
            /* u LSB-only se start hned nepřenastavuje */
        } else if (pit->ch1_rw == 2) {         /* MSB only */
            pit->ch1_reload = (pit->ch1_reload & 0x00FFu) | ((uint16_t)value << 8);
#ifdef _WIN32
            QueryPerformanceCounter(&pit->ch1_start);  /* start po MSB u MSB-only */
#endif
        } else {                               /* LSB+MSB */
            if (!pit->ch1_expect_msb) {
                pit->ch1_lsb = value;
                pit->ch1_expect_msb = 1;
                break;
            }
            pit->ch1_reload = (uint16_t)(pit->ch1_lsb | (value << 8));
#ifdef _WIN32
            QueryPerformanceCounter(&pit->ch1_start);  /* *** start PO MSB zápisu *** */
#endif
            pit->ch1_expect_msb = 0;
            pit->ch1_flip = 0;                 /* nové programování → začínáme od LSB */
        }
        break;
    }

    case 0x43: { /* control word */
        int ch   = (value >> 6) & 0x3;
        int rw   = (value >> 4) & 0x3;   /* 0=latch, 1=LSB, 2=MSB, 3=LSB+MSB */
        int mode = (value >> 1) & 0x7;   /* režimy zatím detailně neřešíme */
        (void)mode;

        if (rw == 0x00) { /* latch current count */
            if (ch == 0x00) { /* CH0 latch */
                pit->ch0_latch   = pit_get_ch0(pit);
                pit->ch0_latched = 1;
                pit->ch0_flip    = 0;          /* LSB první */
            } else if (ch == 0x01) { /* CH1 latch (0x40) */
                pit->ch1_latch   = pit_get_ch1(pit);
                pit->ch1_latched = 1;
                pit->ch1_flip    = 0;          /* LSB první */
            }
            /* CH2 ignorujeme v tomto mini emu */
        } else {
            if (ch == 0x00) { pit->ch0_rw = (uint8_t)rw; pit->expect_msb = 0; }
            else if (ch == 0x01) { pit->ch1_rw = (uint8_t)rw; pit->ch1_expect_msb = 0; }
        }
        break;
    }

    default:
        /* CH2/ostatní – ignor */
        break;
    }
}

/* === I/O – čtení ========================================================= */

uint8_t codex_pit_io_read(CodexPit* pit, uint16_t port) {
    if (!pit) return 0;

    switch (port) {
    case 0x40: { /* CH0 */
        uint16_t val = pit->ch0_latched ? pit->ch0_latch : pit_get_ch0(pit);
        uint8_t  ret = pit->ch0_flip ? (uint8_t)(val >> 8) : (uint8_t)(val & 0xFF);
        pit->ch0_flip ^= 1;
        if (!pit->ch0_flip && pit->ch0_latched) pit->ch0_latched = 0; /* po MSB latch mizí */
        return ret;
    }

    case 0x41: { /* CH1 */
        if (pit->ch1_latched) {
            uint8_t ret = pit->ch1_flip ? (uint8_t)(pit->ch1_latch >> 8)
                                        : (uint8_t)(pit->ch1_latch & 0xFF);
            pit->ch1_flip ^= 1;
            if (!pit->ch1_flip) pit->ch1_latched = 0;  /* po LSB+MSB latch pryč */
            return ret;
        } else {
            uint16_t live = pit_get_ch1(pit);
            uint8_t  ret  = pit->ch1_flip ? (uint8_t)(live >> 8)
                                          : (uint8_t)(live & 0xFF);
            pit->ch1_flip ^= 1;
            return ret;
        }
    }

    default:
        return 0;
    }
}

/* === Periodické tiky (IRQ0) ============================================= */

void codex_pit_update(CodexPit* pit, struct CodexCore* core) {
    if (!pit || !core) return;
#ifdef _WIN32
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    while (now.QuadPart >= pit->next_fire.QuadPart) {
        codex_pic_pulse_irq(&core->pic, core, 0);  /* hrana IRQ0 */
        pit->next_fire.QuadPart += pit->period_ticks;
    }
#else
    (void)core;
#endif
}
