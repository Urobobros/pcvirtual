/* codex_pit.h */
#ifndef CODEX_PIT_H
#define CODEX_PIT_H

#include <stdint.h>

#ifdef _WIN32
#  include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct CodexCore;

typedef uint64_t (*codex_pit_now_ns_fn)(void *ctx);

typedef struct CodexPit {
    /* time source */
    codex_pit_now_ns_fn now_ns;
    void*               now_ctx;

#ifdef _WIN32
    LARGE_INTEGER perf_freq;
    LARGE_INTEGER ch0_start;
    LARGE_INTEGER ch1_start;
    LARGE_INTEGER next_fire;
    uint64_t      period_ticks;
#endif

    /* CH0 (system timer / IRQ0) */
    uint64_t ch0_start_ns;
    uint32_t reload;          /* CH0 reload (16-bit; 0 == 65536) */
    uint8_t  ch0_programmed;  /* running flag so first IN 0x40 isn’t 0 */
    uint8_t  ch0_rw;          /* 1=LSB, 2=MSB, 3=LSB+MSB */
    uint8_t  expect_msb;      /* pair-write helper */
    uint8_t  latch_lsb;       /* pair-write temp (LSB) */

    uint8_t  ch0_latched;
    uint16_t ch0_latch;
    uint8_t  ch0_flip;        /* 0=LSB next, 1=MSB next when reading */

    /* CH1 (RAM refresh on PC/XT; BIOS uses for calibration) */
    uint64_t ch1_start_ns;
    uint16_t ch1_reload;
    uint8_t  ch1_rw;          /* 1=LSB, 2=MSB, 3=LSB+MSB */
    uint8_t  ch1_expect_msb;
    uint8_t  ch1_lsb;

    uint8_t  ch1_latched;
    uint16_t ch1_latch;
    uint8_t  ch1_flip;

    /* CH2 (speaker/timer) — needed for POST calibration via port 0x61 */
    uint64_t ch2_start_ns;
    uint16_t ch2_reload;
    uint8_t  ch2_rw;          /* 1=LSB, 2=MSB, 3=LSB+MSB */
    uint8_t  ch2_mode;        /* we implement 2 (rate) and 3 (square) */
    uint8_t  ch2_programmed;
    uint8_t  ch2_expect_msb;
    uint8_t  ch2_lsb;
    uint8_t  ch2_gate;        /* bit 0 of port 0x61 */

} CodexPit;

/* API */
void    codex_pit_init(CodexPit* pit);
void    codex_pit_update(CodexPit* pit, struct CodexCore* core);

void    codex_pit_set_time_source(CodexPit* pit, codex_pit_now_ns_fn now_ns, void *ctx);

void    codex_pit_io_write(CodexPit* pit, uint16_t port, uint8_t value);
uint8_t codex_pit_io_read (CodexPit* pit, uint16_t port);

/* CH2 helpers for port 0x61 glue */
void    codex_pit_set_gate2(CodexPit* pit, int gate_on, int rising_edge);
uint8_t codex_pit_out2    (CodexPit* pit);   /* returns 0 or 1 -> bit5 on port 0x61 */

#ifdef __cplusplus
}
#endif

#endif /* CODEX_PIT_H */
