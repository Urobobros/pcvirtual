#pragma once

#include <stdint.h>

#define CODEX_NMI_PORT 0xA0

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t mask;
} CodexNmi;

void codex_nmi_init(CodexNmi* nmi);
void codex_nmi_io_write(CodexNmi* nmi, uint8_t value);
uint8_t codex_nmi_io_read(const CodexNmi* nmi);

#ifdef __cplusplus
}
#endif

