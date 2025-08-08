#pragma once

#include <stdint.h>

#include "pic8259.h"

struct CodexCore; /* forward declaration */

typedef struct {
    PIC8259 master;
    PIC8259 slave;
} CodexPic;

void codex_pic_init(CodexPic* pic);
void codex_pic_io_write(CodexPic* pic, uint16_t port, uint8_t value);
uint8_t codex_pic_io_read(CodexPic* pic, uint16_t port);
void codex_pic_raise_irq(CodexPic* pic, struct CodexCore* core, uint8_t irq);

