/* codex_pit.c */

#include "codex_pit.h"
#include "codex_core.h"
#include "codex_pic.h"

#include <string.h>
#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <time.h>
#endif

/* ====== DEBUG TOGGLES ==================================================== */
#ifndef PIT_DEBUG_CTRL
#define PIT_DEBUG_CTRL   1   /* log dekódovaný control word (port 0x43) */
#endif
#ifndef PIT_DEBUG_LATCH
#define PIT_DEBUG_LATCH  1   /* log páru LSB->MSB při latched read z 0x41 */
#endif
#ifndef PIT_DEBUG_NUDGE
#define PIT_DEBUG_NUDGE  1   /* diagnostický „nudge“ po 43h,40h (CH1 latch) */
#endif
/* ======================================================================== */

#ifndef PIT_HZ
#define PIT_HZ 1193182.0
#endif
#ifndef PIT_HZ_NUM
#define PIT_HZ_NUM 1193182ULL
#endif
#define NS_PER_S 1000000000ULL

/* --------------------------------------------------------------------------
 * optional timestamped logging (enable with -DPORT_DEBUG_2)
 * -------------------------------------------------------------------------- */
#ifdef PORT_DEBUG_2
  #include <stdarg.h>
  static inline double pit_now_ms(const CodexPit *pit) {
      if (!pit || !pit->now_ns) return 0.0;
      return (double)pit->now_ns(pit->now_ctx) / 1e6;
  }
  static void pit_log_ts(const CodexPit *pit, const char *fmt, ...) {
      fprintf(stderr, "[PIT %11.6f ms] ", pit_now_ms(pit));
      va_list ap; va_start(ap, fmt);
      vfprintf(stderr, fmt, ap);
      va_end(ap);
      fputc('\n', stderr);
  }
  #define PITLOG(pit, ...) pit_log_ts((pit), __VA_ARGS__)
#else
  #define PITLOG(pit, ...) (void)0
#endif

/* --------------------------------------------------------------------------
 * Monotonic now() in ns
 * -------------------------------------------------------------------------- */
static uint64_t pit_default_now_ns(void *ctx) {
    (void)ctx;
#ifdef _WIN32
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER t;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return (uint64_t)((__int128)t.QuadPart * NS_PER_S / (uint64_t)freq.QuadPart);
#else
    struct timespec ts;
# ifdef CLOCK_MONOTONIC
    clock_gettime(CLOCK_MONOTONIC, &ts);
# else
    timespec_get(&ts, TIME_UTC);
# endif
    return (uint64_t)ts.tv_sec * NS_PER_S + (uint64_t)ts.tv_nsec;
#endif
}

void codex_pit_set_time_source(CodexPit* pit, codex_pit_now_ns_fn now_ns, void *ctx) {
    pit->now_ns  = now_ns ? now_ns : pit_default_now_ns;
    pit->now_ctx = ctx;
}

static inline uint64_t ns_to_pit_ticks(uint64_t elapsed_ns) {
    __int128 acc = (__int128)elapsed_ns * PIT_HZ_NUM + (NS_PER_S / 2);
    return (uint64_t)(acc / NS_PER_S);
}

/* live down-counter values */
static uint16_t pit_get_ch0(CodexPit* pit) {
    if (!pit->ch0_programmed) return 0;
    uint64_t now_ns = pit->now_ns ? pit->now_ns(pit->now_ctx) : 0;
    uint64_t elapsed_ns = now_ns - pit->ch0_start_ns;
    uint64_t pit_ticks = ns_to_pit_ticks(elapsed_ns);
    uint32_t reload = (pit->reload ? pit->reload : 0x10000u);
    uint16_t down   = (uint16_t)(reload - (pit_ticks % reload));
    return down ? down : (uint16_t)reload;
}

static uint16_t pit_get_ch1(CodexPit* pit) {
    uint64_t now_ns = pit->now_ns ? pit->now_ns(pit->now_ctx) : 0;
    uint64_t elapsed_ns = now_ns - pit->ch1_start_ns;
    uint64_t pit_ticks = ns_to_pit_ticks(elapsed_ns);
    uint16_t reload = (uint16_t)(pit->ch1_reload ? pit->ch1_reload : 0x10000u);
    uint16_t down   = (uint16_t)(reload - (pit_ticks % reload));
    return down ? down : reload;
}

/* CH2 output (bit 5 on 0x61) */
uint8_t codex_pit_out2(CodexPit* pit) {
    if (!pit->ch2_programmed || !pit->ch2_gate) return 1; /* OUT2 idle=1 when disabled */
    uint64_t now_ns = pit->now_ns ? pit->now_ns(pit->now_ctx) : 0;
    uint64_t ticks  = ns_to_pit_ticks(now_ns - pit->ch2_start_ns);
    uint32_t reload = pit->ch2_reload ? pit->ch2_reload : 0x10000u;
    if (!reload) reload = 0x10000u;
    uint32_t phase  = (uint32_t)(ticks % reload);

    if (pit->ch2_mode == 3) {
        /* square wave */
        return (phase < (reload >> 1)) ? 1u : 0u;
    } else {
        /* rate generator (short low pulse each cycle end) */
        return (phase == reload - 1) ? 0u : 1u;
    }
}

void codex_pit_set_gate2(CodexPit* pit, int gate_on, int rising_edge) {
    pit->ch2_gate = gate_on ? 1 : 0;
    if (pit->ch2_gate && rising_edge) {
        pit->ch2_start_ns = pit->now_ns ? pit->now_ns(pit->now_ctx) : pit->ch2_start_ns;
    }
}

/* --------------------------------------------------------------------------
 * Init
 * -------------------------------------------------------------------------- */
void codex_pit_init(CodexPit* pit) {
    if (!pit) return;
    memset(pit, 0, sizeof(*pit));

    codex_pit_set_time_source(pit, NULL, NULL);

#ifdef _WIN32
    QueryPerformanceFrequency(&pit->perf_freq);
    LARGE_INTEGER now_qpc; QueryPerformanceCounter(&now_qpc);
    pit->ch0_start = now_qpc;
    pit->ch1_start = now_qpc;
#endif

    uint64_t now_ns = pit->now_ns(pit->now_ctx);
    pit->ch0_start_ns = now_ns;
    pit->ch1_start_ns = now_ns;

    /* CH0 running immediately so first IN 0x40 isn’t 0 */
    pit->reload          = 0x10000;
    pit->ch0_programmed  = 1;
    pit->ch0_rw          = 3;
    pit->ch1_rw          = 3;
    pit->ch1_reload      = 0;

#ifdef _WIN32
    pit->period_ticks    = (uint64_t)((double)pit->perf_freq.QuadPart * pit->reload / PIT_HZ);
    pit->next_fire.QuadPart = pit->ch0_start.QuadPart + pit->period_ticks;
#endif

    pit->ch0_latched     = 0;
    pit->ch1_latched     = 0;
    pit->ch0_flip        = 0;
    pit->ch1_flip        = 0;
    pit->expect_msb      = 0;
    pit->ch1_expect_msb  = 0;

    /* CH2 defaults */
    pit->ch2_reload      = 0;
    pit->ch2_rw          = 3;
    pit->ch2_mode        = 3;    /* square wave */
    pit->ch2_programmed  = 0;
    pit->ch2_expect_msb  = 0;
    pit->ch2_lsb         = 0;
    pit->ch2_gate        = 0;
    pit->ch2_start_ns    = now_ns;

    PITLOG(pit, "init: CH0 reload=%u (prog=%d) CH1 reload=%u CH2 mode=%u",
           pit->reload, pit->ch0_programmed, pit->ch1_reload, pit->ch2_mode);
}

#ifdef _WIN32
static inline void pit_rearm_ch0(CodexPit* pit) {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    uint32_t reload = (pit->reload ? pit->reload : 0x10000u);

    pit->period_ticks       = (uint64_t)((double)pit->perf_freq.QuadPart * reload / PIT_HZ);
    pit->next_fire.QuadPart = now.QuadPart + pit->period_ticks;
    pit->ch0_start          = now;
    pit->ch0_start_ns       = pit->now_ns ? pit->now_ns(pit->now_ctx) : pit->ch0_start_ns;
}
#endif

/* --------------------------------------------------------------------------
 * helpers for debug
 * -------------------------------------------------------------------------- */
#if PIT_DEBUG_CTRL
static const char* rwstr[4] = {"latch","LSB","MSB","LSB+MSB"};
static int normmode(int m){ return (m==6)?2: (m==7)?3: m; }
#endif

/* --------------------------------------------------------------------------
 * I/O – write
 * -------------------------------------------------------------------------- */
void codex_pit_io_write(CodexPit* pit, uint16_t port, uint8_t value) {
    if (!pit) return;

    switch (port) {
    case 0x40: { /* CH0 data */
        if (pit->ch0_rw == 1) {
            pit->reload = (pit->reload & 0xFF00u) | value;
            if ((pit->reload & 0xFFFFu) == 0) pit->reload = 0x10000u;
            pit->ch0_programmed = 1;
#ifdef _WIN32
            pit_rearm_ch0(pit);
#else
            pit->ch0_start_ns = pit->now_ns ? pit->now_ns(pit->now_ctx) : pit->ch0_start_ns;
#endif
        } else if (pit->ch0_rw == 2) {
            pit->reload = (pit->reload & 0x00FFu) | ((uint32_t)value << 8);
            if ((pit->reload & 0xFFFFu) == 0) pit->reload = 0x10000u;
            pit->ch0_programmed = 1;
#ifdef _WIN32
            pit_rearm_ch0(pit);
#else
            pit->ch0_start_ns = pit->now_ns ? pit->now_ns(pit->now_ctx) : pit->ch0_start_ns;
#endif
        } else { /* LSB+MSB */
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
#else
            pit->ch0_start_ns = pit->now_ns ? pit->now_ns(pit->now_ctx) : pit->ch0_start_ns;
#endif
            pit->expect_msb = 0;
        }
        break;
    }

    case 0x41: { /* CH1 data */
        if (pit->ch1_rw == 1) {
            pit->ch1_reload = (pit->ch1_reload & 0xFF00u) | value;
        } else if (pit->ch1_rw == 2) {
            pit->ch1_reload = (pit->ch1_reload & 0x00FFu) | ((uint16_t)value << 8);
            pit->ch1_start_ns = pit->now_ns ? pit->now_ns(pit->now_ctx) : pit->ch1_start_ns;
#ifdef _WIN32
            QueryPerformanceCounter(&pit->ch1_start);
#endif
        } else {
            if (!pit->ch1_expect_msb) {
                pit->ch1_lsb = value;
                pit->ch1_expect_msb = 1;
                break;
            }
            pit->ch1_reload = (uint16_t)(pit->ch1_lsb | (value << 8));
            pit->ch1_start_ns = pit->now_ns ? pit->now_ns(pit->now_ctx) : pit->ch1_start_ns;
#ifdef _WIN32
            QueryPerformanceCounter(&pit->ch1_start);
#endif
            pit->ch1_expect_msb = 0;
            pit->ch1_flip = 0;
        }
        break;
    }

    case 0x42: { /* CH2 data */
        if (pit->ch2_rw == 1) {                  /* LSB only */
            pit->ch2_reload = (pit->ch2_reload & 0xFF00u) | value;
            if ((pit->ch2_reload & 0xFFFFu) == 0) pit->ch2_reload = 0;      // 0 = 65536
            pit->ch2_programmed = 1;
            pit->ch2_start_ns = pit->now_ns ? pit->now_ns(pit->now_ctx) : pit->ch2_start_ns;
        } else if (pit->ch2_rw == 2) {           /* MSB only */
            pit->ch2_reload = (pit->ch2_reload & 0x00FFu) | ((uint16_t)value << 8);
            if ((pit->ch2_reload & 0xFFFFu) == 0) pit->ch2_reload = 0;      // 0 = 65536
            pit->ch2_programmed = 1;
            pit->ch2_start_ns = pit->now_ns ? pit->now_ns(pit->now_ctx) : pit->ch2_start_ns;
        } else {                                 /* LSB+MSB */
            if (!pit->ch2_expect_msb) {
                pit->ch2_lsb = value;
                pit->ch2_expect_msb = 1;
                break;
            }
            pit->ch2_reload = (uint16_t)(pit->ch2_lsb | (value << 8));
            if (pit->ch2_reload == 0) pit->ch2_reload = 0;                  // 0 = 65536
            pit->ch2_programmed = 1;
            pit->ch2_start_ns = pit->now_ns ? pit->now_ns(pit->now_ctx) : pit->ch2_start_ns;
            pit->ch2_expect_msb = 0;
        }
        break;
    }

    case 0x43: { /* control word */
        int ch   = (value >> 6) & 0x3;
        int rw   = (value >> 4) & 0x3;
        int mode = (value >> 1) & 0x7;
        int m    = (mode == 6 ? 2 : (mode == 7 ? 3 : mode));

#if PIT_DEBUG_CTRL
        // fprintf(stderr, "PIT CTRL: ch=%d rw=%s mode=%d bcd=%d\n",
                // ch, rwstr[rw], (mode==6||mode==7)? ((mode==6)?2:3) : mode, value & 1);
#endif

#if PIT_DEBUG_NUDGE
        /* diagnosticky: po CH1 latch (43h,40h) posuň start, aby delta nebyla 0 */
        if (ch == 1 && rw == 0) {
            pit->ch1_start_ns -= (uint64_t)((256.0 / PIT_HZ) * 1e9); /* ~256 PIT ticků */
        }
#endif

        if (rw == 0x00) { /* latch current count */
            if (ch == 0x00) {
                pit->ch0_latch   = pit_get_ch0(pit);
                pit->ch0_latched = 1;
                pit->ch0_flip    = 0;   /* LSB first after latch */
            } else if (ch == 0x01) {
                pit->ch1_latch   = pit_get_ch1(pit);
                pit->ch1_latched = 1;
                pit->ch1_flip    = 0;   /* LSB first after latch */
            }
            /* CH2 latch ignorujeme */
        } else {
            if (ch == 0x00) {
                pit->ch0_rw = (uint8_t)rw; pit->expect_msb = 0;
            } else if (ch == 0x01) {
                pit->ch1_rw = (uint8_t)rw; pit->ch1_expect_msb = 0;
            } else if (ch == 0x02) {
                pit->ch2_rw   = (uint8_t)rw;
                pit->ch2_mode = (uint8_t)m;
                pit->ch2_expect_msb = 0;
            }
        }
        break;
    }

    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * I/O – read
 * -------------------------------------------------------------------------- */
uint8_t codex_pit_io_read(CodexPit* pit, uint16_t port) {
    if (!pit) return 0;

    switch (port) {
    case 0x40: { /* CH0 */
        uint16_t val = pit->ch0_latched ? pit->ch0_latch : pit_get_ch0(pit);
        uint8_t  ret = pit->ch0_flip ? (uint8_t)(val >> 8) : (uint8_t)(val & 0xFF);
        pit->ch0_flip ^= 1;
        if (!pit->ch0_flip && pit->ch0_latched) pit->ch0_latched = 0;
        return ret;
    }
    case 0x41: { /* CH1 */
        if (pit->ch1_latched) {
            uint8_t ret = pit->ch1_flip ? (uint8_t)(pit->ch1_latch >> 8)
                                        : (uint8_t)(pit->ch1_latch & 0xFF);
#if PIT_DEBUG_LATCH
            static int have_pair = 0; static uint8_t pair_lsb = 0;
            if (!pit->ch1_flip) { pair_lsb = ret; have_pair = 1; }
            else if (have_pair) {
                // fprintf(stderr, "[PIT1-LATCH] %02X %02X\n", pair_lsb, ret);
                have_pair = 0;
            }
#endif
            pit->ch1_flip ^= 1;
            if (!pit->ch1_flip) pit->ch1_latched = 0;
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

/* --------------------------------------------------------------------------
 * Periodic IRQ0 (CH0)
 * -------------------------------------------------------------------------- */
void codex_pit_update(CodexPit* pit, struct CodexCore* core) {
    if (!pit || !core) return;
#ifdef _WIN32
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    while (now.QuadPart >= pit->next_fire.QuadPart) {
        codex_pic_pulse_irq(&core->pic, core, 0);  /* edge IRQ0 */
        pit->next_fire.QuadPart += pit->period_ticks;
    }
#else
    (void)core;
#endif
}
