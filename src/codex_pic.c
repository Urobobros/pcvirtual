#include "codex_pic.h"
#include "codex_core.h"

#include <string.h>

void codex_pic_init(CodexPic* pic) {
    if (!pic) return;
    pic8259_reset(&pic->master);
    pic8259_reset(&pic->slave);
    pic->slave.vector_base = 0x70; // typical PC BIOS default after init
}

void codex_pic_io_write(CodexPic* pic, uint16_t port, uint8_t value) {
    if (!pic) return;
    if (port >= 0x20 && port <= 0x21) {
        pic8259_write(&pic->master, port, value);
    } else if (port >= 0xA0 && port <= 0xA1) {
        pic8259_write(&pic->slave, port, value);
    }
}

uint8_t codex_pic_io_read(CodexPic* pic, uint16_t port) {
    if (!pic) return 0xFF;
    if (port >= 0x20 && port <= 0x21) {
        return pic8259_read(&pic->master, port);
    } else if (port >= 0xA0 && port <= 0xA1) {
        return pic8259_read(&pic->slave, port);
    }
    return 0xFF;
}

void codex_pic_raise_irq(CodexPic* pic, struct CodexCore* core, uint8_t irq) {
    if (!pic || !core) return;

    if (irq < 8) {
        pic8259_raise_irq(&pic->master, irq);
        if (!pic8259_has_pending_unmasked(&pic->master))
            return;
        uint8_t vector = pic8259_acknowledge(&pic->master);
#ifdef _WIN32
        WHV_INTERRUPT_CONTROL ctrl;
        memset(&ctrl, 0, sizeof(ctrl));
        ctrl.Type = WHvX64InterruptTypeFixed;
        ctrl.Vector = vector;
        ctrl.TargetVtl = 0;
        WHvRequestInterrupt(core->partition, &ctrl, sizeof(ctrl));
#else
        (void)vector; (void)core;
#endif
    } else {
        // IRQs >=8 go to the slave. Devices using them are not yet modelled.
        pic8259_raise_irq(&pic->slave, irq - 8);
    }
}

