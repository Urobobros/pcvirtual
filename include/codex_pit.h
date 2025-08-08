#pragma once

#include <stdint.h>
#ifdef _WIN32
#include <Windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct CodexCore; /* forward declaration */

typedef struct {
    uint32_t reload;
    uint8_t  latch_lsb;
    int      expect_msb;
    /* Dummy counter for channel 1 so BIOS polling sees changing values */
    uint16_t ch1_dummy;
    int      ch1_flip;
#ifdef _WIN32
    LARGE_INTEGER perf_freq;
    LARGE_INTEGER next_fire;
    uint64_t period_ticks;
#endif
} CodexPit;

void codex_pit_init(CodexPit* pit);
void codex_pit_io_write(CodexPit* pit, uint16_t port, uint8_t value);
uint8_t codex_pit_io_read(CodexPit* pit, uint16_t port);
void codex_pit_update(CodexPit* pit, struct CodexCore* core);

#ifdef __cplusplus
}
#endif
