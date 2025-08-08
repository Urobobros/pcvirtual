#pragma once
// 8259 Programmable Interrupt Controller (master/slave)
// Minimal XT-compatible implementation for WHPX-based emulator

#include <stdint.h>
#include <stdbool.h>

// OCW2 command encodings (upper 3 bits)
#define PIC8259_OCW2_NON_SPEC_EOI   0x20  // 001xxxxx
#define PIC8259_OCW2_SPEC_EOI       0x60  // 011xxxxx
#define PIC8259_OCW2_ROT_NON_SPEC   0xA0  // 101xxxxx (rotate on EOI)
#define PIC8259_OCW2_ROT_SPEC       0xE0  // 111xxxxx (rotate on specific EOI)

// OCW3 bits (we implement RIS: 1=ISR,0=IRR select for reads)
#define PIC8259_OCW3_RIS            0x01  // read ISR/IRR select
#define PIC8259_OCW3_SET_RIS        0x40  // set RIS according to bit0

// ICW1 bits
#define PIC8259_ICW1_INIT           0x10  // start initialization
#define PIC8259_ICW1_SINGLE         0x02  // 1=single, 0=cascade
#define PIC8259_ICW1_ICW4           0x01  // 1=ICW4 will follow

// ICW4 bits
#define PIC8259_ICW4_8086           0x01
#define PIC8259_ICW4_AUTO_EOI       0x02

typedef struct PIC8259 {
    uint8_t imr;        // Interrupt Mask Register
    uint8_t irr;        // Interrupt Request Register
    uint8_t isr;        // In-Service Register

    uint8_t icw_step;       // 0=idle, 2=expect ICW2, 3=ICW3, 4=ICW4
    uint8_t expect_icw4;    // from ICW1
    uint8_t vector_base;    // from ICW2 (usually 0x08 on XT)
    uint8_t single_mode;    // from ICW1 (XT: 1)
    uint8_t auto_eoi;       // from ICW4 bit1

    uint8_t read_isr;       // OCW3 read select: 0=IRR, 1=ISR
    uint8_t priority_add;   // rotating priority base (0..7)
    uint8_t last_level;     // edge-detection helper for raise/lower
} PIC8259;

void pic8259_reset(PIC8259* p);
uint8_t pic8259_read(PIC8259* p, uint16_t port);
void    pic8259_write(PIC8259* p, uint16_t port, uint8_t val);

void pic8259_raise_irq(PIC8259* p, int line);
void pic8259_lower_irq(PIC8259* p, int line);
bool pic8259_has_pending_unmasked(const PIC8259* p);
int  pic8259_get_pending_line(const PIC8259* p);
uint8_t pic8259_acknowledge(PIC8259* p);
void pic8259_eoi(PIC8259* p, int line /* -1 = nonspecific */);

