/* codex_pic.c */

#include "codex_pic.h"
#include "codex_core.h"

#ifdef _WIN32
#include <WinHvPlatform.h>
#include <WinHvPlatformDefs.h>
#endif


// Pomocná injekce do WHPX
static void inject_vector(struct CodexCore* core, uint8_t vector) {
#ifdef _WIN32
    WHV_INTERRUPT_CONTROL ic = {0};
    // Nové API: WHvRequestInterrupt plochý WHV_INTERRUPT_CONTROL
    ic.Type           = WHvX64InterruptTypeFixed;
    ic.DestinationMode= WHvX64InterruptDestinationModePhysical;
    ic.TriggerMode    = WHvX64InterruptTriggerModeEdge;
    ic.Vector         = vector;          // např. 0x08 pro IRQ0
    ic.Destination    = 1ull << 0;       // vCPU maska: cíl = VP #0
    // Třetí argument je velikost struktury (viz WinHvPlatform.h)
    (void)WHvRequestInterrupt(core->partition, &ic, sizeof(ic));
#else
    (void)core; (void)vector;
#endif
}

void codex_pic_try_inject(CodexPic* cpic, struct CodexCore* core) {
    if (!cpic || !core) return;

    // Má PIC něco nemaskovaného pending?
    if (!pic8259_has_pending_unmasked(&cpic->pic))
        return;

    // Přesun z IRR do ISR a získání vektoru
    uint8_t vec = pic8259_acknowledge(&cpic->pic);
    if (vec == 0xFF) return;

    inject_vector(core, vec);
}

void codex_pic_raise_irq(CodexPic* cpic, struct CodexCore* core, int line) {
    if (!cpic) return;
    pic8259_raise_irq(&cpic->pic, line);

    // Pokus o okamžité doručení – když je guest ready, dostane hned
    codex_pic_try_inject(cpic, core);
}

void codex_pic_pulse_irq(CodexPic* cpic, struct CodexCore* core, int line) {
    if (!cpic) return;
    pic8259_raise_irq(&cpic->pic, line);
    codex_pic_try_inject(cpic, core);
    pic8259_lower_irq(&cpic->pic, line); // uvolni hranu pro další tick
}