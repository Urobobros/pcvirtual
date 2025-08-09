/* codex_pit.h */

#pragma once

#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct CodexCore; /* forward declaration */

typedef struct {
    /* Channel 0 reload/value latching */
    uint32_t reload;
    uint8_t  latch_lsb;
    int      expect_msb;   /* používá se jen pro RW=LSB+MSB u ch0 */
    uint8_t  ch0_rw;       /* 1=LSB, 2=MSB, 3=LSB+MSB */

    /* Channel 0 runtime state (pro IN 0x40 a latch) */
    uint16_t ch0_latch;    /* latched current count */
    int      ch0_latched;  /* 1 pokud je hodnota latched */
    int      ch0_flip;     /* 0=LSB next, 1=MSB next */
    int      ch0_programmed; // 0 po resetu, 1 po prvním zápisu do 0x40
#ifdef _WIN32
    LARGE_INTEGER ch0_start; /* timestamp začátku aktuální periody ch0 */
#endif

    /* Channel 1 state for BIOS memory refresh polling */
    uint16_t ch1_reload;
    uint8_t  ch1_lsb;
    int      ch1_expect_msb; /* používá se jen pro RW=LSB+MSB u ch1 */
    uint8_t  ch1_rw;         /* 1=LSB, 2=MSB, 3=LSB+MSB */
    uint16_t ch1_latch;
    int      ch1_latched;
    int      ch1_flip;
#ifdef _WIN32
    LARGE_INTEGER ch1_start;
#endif
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
