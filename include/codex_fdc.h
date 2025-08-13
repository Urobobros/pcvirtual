#pragma once

#include <stdint.h>
#include <stddef.h>

struct CodexCore;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    struct CodexCore* core;
    uint8_t dor;
    uint8_t msr;
    uint8_t cmd;
    uint8_t st0, st1, st2;
    uint8_t st0_irq, pcn_irq;
    int irq_pending;
    uint8_t params[9];
    int param_count;
    int param_expected;
    uint8_t result[7];
    int result_len;
    int result_pos;
    enum { FDC_STATE_COMMAND, FDC_STATE_PARAMS, FDC_STATE_RESULT } state;
    uint8_t track[4];
    /* loaded floppy image */
    uint8_t* disk;
    size_t disk_size;
    int tracks;
    int heads;
    int sectors_per_track;
    int sector_size;
} CodexFdc;

int codex_fdc_init(CodexFdc* fdc, struct CodexCore* core, const char* image_path);
void codex_fdc_destroy(CodexFdc* fdc);
uint8_t codex_fdc_io_read(CodexFdc* fdc, uint16_t port);
void codex_fdc_io_write(CodexFdc* fdc, uint16_t port, uint8_t value);

#ifdef __cplusplus
}
#endif

