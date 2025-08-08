#include "codex_pic.h"
#include "codex_core.h"

#include <string.h>

void codex_pic_init(CodexPic* pic) {
    if (!pic) return;
    memset(pic, 0, sizeof(*pic));
    pic->imr_master = 0xFF;
    pic->imr_slave  = 0xFF;
    pic->vector_master = 0x08; /* default IRQ0 vector */
    pic->vector_slave  = 0x70; /* typical PC BIOS value */
}

void codex_pic_io_write(CodexPic* pic, uint16_t port, uint8_t value) {
    if (!pic) return;
    switch (port) {
    case 0x20: /* master command */
        if (value & 0x10)
            pic->expect_icw_master = 1; /* expect ICW2 on data port */
        /* ignore OCW2/OCW3 for now */
        break;
    case 0x21: /* master data */
        if (pic->expect_icw_master) {
            pic->vector_master = value & 0xF8;
            pic->expect_icw_master = 0;
        } else {
            pic->imr_master = value;
        }
        break;
    case 0xA0: /* slave command */
        if (value & 0x10)
            pic->expect_icw_slave = 1;
        break;
    case 0xA1: /* slave data */
        if (pic->expect_icw_slave) {
            pic->vector_slave = value & 0xF8;
            pic->expect_icw_slave = 0;
        } else {
            pic->imr_slave = value;
        }
        break;
    default:
        break;
    }
}

uint8_t codex_pic_io_read(CodexPic* pic, uint16_t port) {
    if (!pic) return 0;
    switch (port) {
    case 0x20:
        return 0; /* IRR not implemented */
    case 0x21:
        return pic->imr_master;
    case 0xA0:
        return 0; /* IRR not implemented */
    case 0xA1:
        return pic->imr_slave;
    default:
        return 0;
    }
}

void codex_pic_raise_irq(CodexPic* pic, struct CodexCore* core, uint8_t irq) {
    if (!pic || !core) return;
    uint8_t vector;
    if (irq < 8) {
        if (pic->imr_master & (1u << irq)) return; /* masked */
        vector = pic->vector_master + irq;
    } else {
        uint8_t line = irq - 8;
        if (pic->imr_slave & (1u << line)) return;
        if (pic->imr_master & (1u << 2)) return; /* cascade masked */
        vector = pic->vector_slave + line;
    }
#ifdef _WIN32
    WHV_INTERRUPT_CONTROL ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.Type = WHvX64InterruptTypeFixed;
    ctrl.Vector = vector;
    ctrl.TargetVtl = 0;
    WHvRequestInterrupt(core->partition, &ctrl, sizeof(ctrl));
#else
    (void)vector;
#endif
}

