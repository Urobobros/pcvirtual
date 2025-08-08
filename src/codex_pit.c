#include "codex_pit.h"
#include "codex_core.h"

#include <string.h>

#ifdef _WIN32
#include <winhvplatform.h>
#endif

void codex_pit_init(CodexPit* pit) {
    if (!pit) return;
    memset(pit, 0, sizeof(*pit));
#ifdef _WIN32
    QueryPerformanceFrequency(&pit->perf_freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    pit->reload = 0x10000; /* default 65536 */
    pit->period_ticks = (uint64_t)((double)pit->perf_freq.QuadPart * pit->reload / 1193182.0);
    pit->next_fire.QuadPart = now.QuadPart + pit->period_ticks;
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
    case 0x43: /* control word */
        pit->expect_msb = 0; /* reset flipflop */
        break;
    default:
        break;
    }
}

uint8_t codex_pit_io_read(CodexPit* pit, uint16_t port) {
    (void)port;
    if (!pit) return 0;
    return 0; /* minimal implementation */
}

void codex_pit_update(CodexPit* pit, struct CodexCore* core) {
    if (!pit || !core) return;
#ifdef _WIN32
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (now.QuadPart >= pit->next_fire.QuadPart) {
        WHV_INTERRUPT_CONTROL ctrl;
        memset(&ctrl, 0, sizeof(ctrl));
        ctrl.Type = WHvX64InterruptTypeFixed;
        ctrl.Vector = 0x08; /* IRQ0 -> INT 08h */
        ctrl.TargetVtl = 0;
        WHvRequestInterrupt(core->partition, &ctrl, sizeof(ctrl));
        pit->next_fire.QuadPart += pit->period_ticks;
    }
#else
    (void)core;
#endif
}
