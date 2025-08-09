/* codex_pic.h */

#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <WinHvPlatform.h>
#endif

#include "pic8259.h"

#ifdef __cplusplus
extern "C" {
#endif

struct CodexCore; // fwd

typedef struct CodexPic {
    PIC8259 pic;  // vlastní 8259 stav
} CodexPic;

// init/reset
static inline void codex_pic_init(CodexPic* cpic) {
    if (!cpic) return;
    pic8259_reset(&cpic->pic);
}

// I/O z port interceptu
static inline uint8_t codex_pic_io_read(CodexPic* cpic, uint16_t port) {
    return pic8259_read(&cpic->pic, port);
}
static inline void codex_pic_io_write(CodexPic* cpic, uint16_t port, uint8_t val) {
    pic8259_write(&cpic->pic, port, val);
}

// Zařízení zvedá linku (edge)
void codex_pic_raise_irq(CodexPic* cpic, struct CodexCore* core, int line);

// Volá hlavní smyčka kdykoli (po exitu/v kole) – doručí IRQ pokud je pending
void codex_pic_try_inject(CodexPic* cpic, struct CodexCore* core);

// Krátký puls na IRQ lince (edge: raise → inject → lower)
void codex_pic_pulse_irq(CodexPic* cpic, struct CodexCore* core, int line);

#ifdef __cplusplus
}
#endif
