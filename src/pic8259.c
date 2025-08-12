/* pic8259.c */

#include "pic8259.h"

#include <string.h>

// ---- helpers ----
static int highest_priority_line(uint8_t bits, uint8_t prio_add) {
    if (!bits) return -1;
    for (int i = 0; i < 8; ++i) {
        int line = (i + prio_add) & 7;  // rotated priority
        if (bits & (1u << line)) return line;
    }
    return -1;
}

static int highest_inservice_line(const PIC8259* p) {
    return highest_priority_line(p->isr, p->priority_add);
}

// ---- public API ----
void pic8259_reset(PIC8259* p) {
    memset(p, 0, sizeof(*p));
    p->vector_base = 0x08;      // XT default after init (ICW2 will override)
    p->read_isr    = 0;         // read IRR by default
    p->priority_add = 0;        // IRQ0 highest
}

void pic8259_raise_irq(PIC8259* p, int line) {
    uint8_t m = 1u << (line & 7);
    if (!(p->last_level & m)) {
        p->irr |= m;
        p->last_level |= m;
    }
}

void pic8259_lower_irq(PIC8259* p, int line) {
    p->last_level &= (uint8_t)~(1u << (line & 7));
}

bool pic8259_has_pending_unmasked(const PIC8259* p) {
    return (p->irr & (uint8_t)~p->imr) != 0;
}

int pic8259_get_pending_line(const PIC8259* p) {
    return highest_priority_line((uint8_t)(p->irr & (uint8_t)~p->imr), p->priority_add);
}

uint8_t pic8259_acknowledge(PIC8259* p) {
    int line = pic8259_get_pending_line(p);
    if (line < 0) return 0xFF; // no vector

    uint8_t m = (uint8_t)(1u << line);
    p->irr &= (uint8_t)~m;

    if (!p->auto_eoi) {
        p->isr |= m;           // in-service until EOI
    }
    return (uint8_t)(p->vector_base + line);
}

void pic8259_eoi(PIC8259* p, int line) {
    int l = line;
    if (l < 0) l = highest_inservice_line(p);
    if (l < 0) return;
    p->isr &= (uint8_t)~(1u << l);
}

uint8_t pic8259_read(PIC8259* p, uint16_t port) {
    if ((port & 1) == 0) {
        return p->read_isr ? p->isr : p->irr;
    } else {
        return p->imr;
    }
}

void pic8259_write(PIC8259* p, uint16_t port, uint8_t val) {
    if ((port & 1) == 0) { // command
        if (val & PIC8259_ICW1_INIT) {
            p->icw_step    = 2;
            p->expect_icw4 = (val & PIC8259_ICW1_ICW4) ? 1 : 0;
            p->imr = p->irr = p->isr = p->last_level = 0;
            p->priority_add = 0;
            p->read_isr = 0;
            return;
        }
        if ((val & 0x18) == 0x00) {
            if (val & PIC8259_OCW3_SET_RIS)
                p->read_isr = (uint8_t)(val & PIC8259_OCW3_RIS);
        } else {
            uint8_t cmd = (val & 0xE0);
            uint8_t lvl = (uint8_t)(val & 0x07);
            switch (cmd) {
                case PIC8259_OCW2_NON_SPEC_EOI:
                    pic8259_eoi(p, -1);
                    break;
                case PIC8259_OCW2_SPEC_EOI:
                    pic8259_eoi(p, lvl);
                    break;
                case PIC8259_OCW2_ROT_NON_SPEC:
                    pic8259_eoi(p, -1);
                    p->priority_add = (uint8_t)((p->priority_add + 1) & 7);
                    break;
                case PIC8259_OCW2_ROT_SPEC:
                    pic8259_eoi(p, lvl);
                    p->priority_add = (uint8_t)((lvl + 1) & 7);
                    break;
                default:
                    break;
            }
        }
    } else { // data
        if (p->icw_step == 2) {
            p->vector_base = (uint8_t)(val & 0xF8);
            p->icw_step = p->expect_icw4 ? 4 : 0;
        } else if (p->icw_step == 3) {
            p->icw_step = p->expect_icw4 ? 4 : 0;
        } else if (p->icw_step == 4) {
            p->auto_eoi = (uint8_t)((val & PIC8259_ICW4_AUTO_EOI) ? 1 : 0);
            p->icw_step = 0;
        } else {
            p->imr = val;
        }
    }
}

