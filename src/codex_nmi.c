#include "codex_nmi.h"

int codex_nmi_init(CodexNmi* nmi) {
    if (!nmi) return -1;
    nmi->mask = 0;
    return 0;
}

void codex_nmi_io_write(CodexNmi* nmi, uint8_t value) {
    if (!nmi) return;
    nmi->mask = value & 0x80u;
}

uint8_t codex_nmi_io_read(const CodexNmi* nmi) {
    return nmi ? nmi->mask : 0;
}

