#include "codex_pit.h"
#include "codex_core.h"
#include "codex_pic.h"

#include <string.h>

#ifdef _WIN32
#include <winhvplatform.h>
#endif

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
    pit->period_ticks = (uint64_t)((double)pit->perf_freq.QuadPart * pit->reload / 1193182.0);
    pit->next_fire.QuadPart = now.QuadPart + pit->period_ticks;
    pit->ch1_start = now;
    pit->ch1_reload = 0;
#endif
}

void codex_pit_io_write(CodexPit* pit, uint16_t port, uint8_t value) {
    if (!pit) return;
    switch (port) {
    case 0x40: /* channel 0 data */
        if (!pit->expect_msb) {
            pit->latch_lsb = value;
            pit->expect_msb = 1;
        } else {
            pit->reload = (uint32_t)(pit->latch_lsb | (value << 8));
            if (pit->reload == 0) pit->reload = 0x10000;
#ifdef _WIN32
            pit->period_ticks = (uint64_t)((double)pit->perf_freq.QuadPart * pit->reload / 1193182.0);
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            pit->next_fire.QuadPart = now.QuadPart + pit->period_ticks;
#endif
            pit->expect_msb = 0;
        }
        break;
    case 0x41: /* channel 1 data */
        if (!pit->ch1_expect_msb) {
            pit->ch1_lsb = value;
            pit->ch1_expect_msb = 1;
        } else {
            pit->ch1_reload = (uint16_t)(pit->ch1_lsb | (value << 8));
#ifdef _WIN32
            QueryPerformanceCounter(&pit->ch1_start);
#endif
            pit->ch1_expect_msb = 0;
        }
        break;
    case 0x43: /* control word */
        pit->expect_msb = 0; /* reset flipflop for channel 0 */
        if ((value & 0xC0) == 0x40) {
            if ((value & 0x30) == 0x00) {
                /* Latch channel 1 */
                pit->ch1_latch = pit_get_ch1(pit);
                pit->ch1_latched = 1;
                pit->ch1_flip = 0;
            } else {
                /* Prepare for new reload value */
                pit->ch1_expect_msb = 0;
            }
        }
        break;
    default:
        break;
    }
}

uint8_t codex_pit_io_read(CodexPit* pit, uint16_t port) {
    if (!pit) return 0;
    switch (port) {
    case 0x41: {
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
    if (now.QuadPart >= pit->next_fire.QuadPart) {
        codex_pic_raise_irq(&core->pic, core, 0);
        pit->next_fire.QuadPart += pit->period_ticks;
    }
#else
    (void)core;
#endif
}
