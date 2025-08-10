#include <assert.h>
#include <stdio.h>
#include "codex_pit.h"

// Stub to satisfy linkage; actual implementation isn't needed for these tests.
struct CodexPic;
struct CodexCore;
void codex_pic_pulse_irq(struct CodexPic* pic, struct CodexCore* core, int line) {
    (void)pic; (void)core; (void)line;
}

int main(void) {
    CodexPit pit;
    codex_pit_init(&pit);

    // Program channel 1 with reload value 0x7474 (mode 2, LSB+MSB)
    codex_pit_io_write(&pit, 0x43, 0x74);
    codex_pit_io_write(&pit, 0x41, 0x74); // LSB
    codex_pit_io_write(&pit, 0x41, 0x74); // MSB

    assert(pit.ch1_rw == 3);              // LSB+MSB mode set
    assert(pit.ch1_reload == 0x7474);     // reload correctly formed

    uint16_t expected[] = {0x7469, 0x7425, 0x73E5, 0x73A4, 0x7366};
    size_t count = sizeof(expected)/sizeof(expected[0]);

    for (size_t i = 0; i < count; ++i) {
        codex_pit_io_write(&pit, 0x43, 0x40); // latch command for ch1
        pit.ch1_latch = expected[i];          // emulate timer value after latch
        pit.ch1_latched = 1;
        pit.ch1_flip = 0;

        uint8_t lsb = codex_pit_io_read(&pit, 0x41);
        uint8_t msb = codex_pit_io_read(&pit, 0x41);

        assert(lsb == (expected[i] & 0xFF));
        assert(msb == (expected[i] >> 8));
        assert(pit.ch1_latched == 0); // latch cleared after MSB read
    }

    printf("PIT channel 1 latch/read test passed\n");
    return 0;
}
