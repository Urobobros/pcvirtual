#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "codex_pit.h"

// Zapni logy kompilací s -DDEBUG
#ifdef PORT_DEBUG
  #define DLOG(...) fprintf(stderr, __VA_ARGS__)
#else
  #define DLOG(...) (void)0
#endif

// Stub to satisfy linkage; actual implementation isn't needed for these tests.
struct CodexPic;
struct CodexCore;
void codex_pic_pulse_irq(struct CodexPic* pic, struct CodexCore* core, int line) {
    (void)pic; (void)core; (void)line;
}

static void dump_ch1(const CodexPit *pit, const char *tag) {
    DLOG("[CH1 %s] reload=0x%04X latched=%u flip=%u latched_val=0x%04X rw=%u\n",
         tag, pit->ch1_reload, pit->ch1_latched, pit->ch1_flip,
         pit->ch1_latch, pit->ch1_rw);
}

int main(void) {
    CodexPit pit;
    codex_pit_init(&pit);

    // Program channel 1 with reload value 0x7474 (mode 2, LSB+MSB)
    DLOG("Programming CH1: control=0x74, reload=0x7474 (mode 2, LSB+MSB)\n");
    codex_pit_io_write(&pit, 0x43, 0x74);
    codex_pit_io_write(&pit, 0x41, 0x74); // LSB
    codex_pit_io_write(&pit, 0x41, 0x74); // MSB
    dump_ch1(&pit, "after program");

    assert(pit.ch1_rw == 3);              // LSB+MSB mode set
    assert(pit.ch1_reload == 0x7474);     // reload correctly formed

    uint16_t expected[] = {0x7469, 0x7425, 0x73E5, 0x73A4, 0x7366};
    size_t count = sizeof(expected)/sizeof(expected[0]);

    for (size_t i = 0; i < count; ++i) {
        // Latch command for CH1
        codex_pit_io_write(&pit, 0x43, 0x40);
        pit.ch1_latch   = expected[i]; // emulate timer value after latch
        pit.ch1_latched = 1;
        pit.ch1_flip    = 0;

        DLOG("\n---- case %zu ----\n", i);
        dump_ch1(&pit, "before read");

        uint8_t lsb = codex_pit_io_read(&pit, 0x41);
        DLOG("read LSB = 0x%02X (expected 0x%02X) | latched=%u flip=%u\n",
             lsb, (expected[i] & 0xFF), pit.ch1_latched, pit.ch1_flip);

        uint8_t msb = codex_pit_io_read(&pit, 0x41);
        DLOG("read MSB = 0x%02X (expected 0x%02X) | latched=%u flip=%u\n",
             msb, (expected[i] >> 8), pit.ch1_latched, pit.ch1_flip);

        // Případná detailní chybová hláška před assertem:
        if (lsb != (expected[i] & 0xFF))
            fprintf(stderr, "Mismatch LSB in case %zu: got 0x%02X, want 0x%02X\n",
                    i, lsb, (expected[i] & 0xFF));
        if (msb != (expected[i] >> 8))
            fprintf(stderr, "Mismatch MSB in case %zu: got 0x%02X, want 0x%02X\n",
                    i, msb, (expected[i] >> 8));

        assert(lsb == (expected[i] & 0xFF));
        assert(msb == (expected[i] >> 8));
        assert(pit.ch1_latched == 0); // latch cleared after MSB read

        dump_ch1(&pit, "after read");
    }

    printf("PIT channel 1 latch/read test passed\n");
    return 0;
}
