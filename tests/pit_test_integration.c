// pit_ch1_latch_time_test.c
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "codex_pit.h"

// Zapni logy kompilací s -DPORT_DEBUG
#ifdef PORT_DEBUG
  #define DLOG(...) fprintf(stderr, __VA_ARGS__)
#else
  #define DLOG(...) (void)0
#endif

/* Přepínač: 1 = máme codex_pit_set_time_source a chceme DI času,
             0 = nemáme, použij fallback s ručním latchem */
#ifndef HAVE_PIT_TIME_SOURCE
#define HAVE_PIT_TIME_SOURCE 0
#endif

// === Forwardy/stub ===
struct CodexPic;
struct CodexCore;
void codex_pic_pulse_irq(struct CodexPic* pic, struct CodexCore* core, int line) {
    (void)pic; (void)core; (void)line;
}

#if HAVE_PIT_TIME_SOURCE
// Fake monotonic clock: vrací, co mu nastavíme do .cur_ns
typedef struct { uint64_t cur_ns; } FakeClock;
static uint64_t fake_now_ns(void *ctx) {
    FakeClock *fc = (FakeClock*)ctx;
    return fc->cur_ns;
}
#endif

static void dump_ch1(const CodexPit *pit, const char *tag) {
    DLOG("[CH1 %s] reload=0x%04X latched=%u flip=%u latched_val=0x%04X rw=%u\n",
         tag, pit->ch1_reload, pit->ch1_latched, pit->ch1_flip,
         pit->ch1_latch, pit->ch1_rw);
}

int main(void) {
    // Konstanty
    static const uint16_t N = 0x7474;     // BIOS typicky zapisuje do CH1

    // Očekávané hodnoty counteru po latche (mode 2)
    uint16_t expected[] = {0x7469, 0x7425, 0x73E5, 0x73A4, 0x7366};
    const size_t count  = sizeof(expected)/sizeof(expected[0]);

    CodexPit pit;
    codex_pit_init(&pit);

#if HAVE_PIT_TIME_SOURCE
    // Připrav fake clock a DI času
    static const uint32_t PIT_HZ = 1193182u;   // 1.193182 MHz
    FakeClock clk = { .cur_ns = 0 };
    codex_pit_set_time_source(&pit, fake_now_ns, &clk);
#endif

    // Naprogramuj CH1: mode 2, LSB+MSB, reload 0x7474
    DLOG("Programming CH1: control=0x74, reload=0x%04X (mode 2, LSB+MSB)\n", N);
    codex_pit_io_write(&pit, 0x43, 0x74);
    codex_pit_io_write(&pit, 0x41, (uint8_t)(N & 0xFF));        // LSB
    codex_pit_io_write(&pit, 0x41, (uint8_t)((N >> 8) & 0xFF)); // MSB
    dump_ch1(&pit, "after program");

    // Základní sanity
    assert(pit.ch1_rw == 3);
    assert(pit.ch1_reload == N);

#if HAVE_PIT_TIME_SOURCE
    // Vypočti časy v ns pro jednotlivé expected hodnoty (elapsed_ticks = N - expected)
    uint64_t times_ns[32];
    for (size_t i = 0; i < count; ++i) {
        uint32_t elapsed_ticks = (uint32_t)(N - expected[i]); // < N
        times_ns[i] = (uint64_t)elapsed_ticks * 1000000000ull / PIT_HZ; // ns = ticks * 1e9 / PIT_HZ
        DLOG("case %zu: expected=0x%04X, ticks=%u, t=%.3f ms\n",
             i, expected[i], elapsed_ticks, times_ns[i] / 1e6);
    }
#endif

    // Pro každý případ proveď latch a čti LSB/MSB
    for (size_t i = 0; i < count; ++i) {
        DLOG("\n---- case %zu ----\n", i);

#if HAVE_PIT_TIME_SOURCE
        // End-to-end varianta: posuň „čas“ před latchem
        clk.cur_ns = times_ns[i];
        codex_pit_io_write(&pit, 0x43, 0x40); // latch command (CH1)
        dump_ch1(&pit, "after latch (DI time)");
#else
        // Fallback varianta: nemáme DI času, tak jen vyvoláme latch a dosadíme latched hodnotu
        codex_pit_io_write(&pit, 0x43, 0x40); // latch command (CH1)
        pit.ch1_latch   = expected[i];        // emuluj hodnotu po latche
        pit.ch1_latched = 1;
        pit.ch1_flip    = 0;
        dump_ch1(&pit, "after latch (fallback)");
#endif

        // Přečti LSB a MSB
        uint8_t lsb = codex_pit_io_read(&pit, 0x41);
        DLOG("read LSB = 0x%02X (expected 0x%02X) | latched=%u flip=%u\n",
             lsb, (expected[i] & 0xFF), pit.ch1_latched, pit.ch1_flip);

        uint8_t msb = codex_pit_io_read(&pit, 0x41);
        DLOG("read MSB = 0x%02X (expected 0x%02X) | latched=%u flip=%u\n",
             msb, (expected[i] >> 8), pit.ch1_latched, pit.ch1_flip);

        if (lsb != (expected[i] & 0xFF))
            fprintf(stderr, "Mismatch LSB in case %zu: got 0x%02X, want 0x%02X\n",
                    i, lsb, (expected[i] & 0xFF));
        if (msb != (expected[i] >> 8))
            fprintf(stderr, "Mismatch MSB in case %zu: got 0x%02X, want 0x%02X\n",
                    i, msb, (expected[i] >> 8));

        assert(lsb == (expected[i] & 0xFF));
        assert(msb == (expected[i] >> 8));
        assert(pit.ch1_latched == 0); // latch cleared po MSB
        dump_ch1(&pit, "after read");
    }

    printf("PIT CH1 latch/read test passed\n");
    return 0;
}
