/* codex_fdc.c - minimal NEC 765 FDC + floppy image */

#include "codex_fdc.h"
#include "codex_core.h"
#include "codex_pic.h"
#include "codex_dma.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t* dma_buffer(CodexFdc* fdc, size_t len) {
    CodexDma* dma = &fdc->core->dma;
    uint32_t addr = dma->addr[2];
    uint8_t page = dma->page[1];
    uint32_t phys = addr | ((uint32_t)page << 16);
    if (phys + len > fdc->core->memory_size) return NULL;
    return fdc->core->memory + phys;
}

static void raise_irq(CodexFdc* fdc) {
    fdc->irq_pending = 1;
    codex_pic_raise_irq(&fdc->core->pic, fdc->core, 6);
}

static void set_result(CodexFdc* fdc, const uint8_t* buf, int len) {
    memcpy(fdc->result, buf, len);
    fdc->result_len = len;
    fdc->result_pos = 0;
    fdc->state = FDC_STATE_RESULT;
    /* RQM|DIO|BUSY while results remain */
    fdc->msr = 0xD0;
}

static void finish_command(CodexFdc* fdc) {
    fdc->state = FDC_STATE_COMMAND;
    fdc->msr = 0x80; /* RQM */
    fdc->param_count = 0;
    fdc->param_expected = 0;
}

static void exec_command(CodexFdc* fdc) {
    switch (fdc->cmd & 0x1F) {
    case 0x03: /* SPECIFY */
        finish_command(fdc);
        break;
    case 0x07: { /* RECALIBRATE */
        int drive = fdc->params[0] & 3;
        fdc->track[drive] = 0;
        fdc->st0_irq = 0x20 | drive; /* seek end */
        fdc->pcn_irq = 0;
        raise_irq(fdc);
        finish_command(fdc);
        break; }
    case 0x04: { /* SENSE DRIVE STATUS */
        int drive = fdc->params[0] & 3;
        uint8_t st3 = 0x20 | drive; /* drive ready */
        if (fdc->track[drive] == 0) st3 |= 0x10;
        uint8_t res[1] = { st3 };
        set_result(fdc, res, 1);
        break; }
    case 0x0F: { /* SEEK */
        int drive = fdc->params[0] & 3;
        uint8_t cyl = fdc->params[1];
        fdc->track[drive] = cyl;
        fdc->st0_irq = 0x20 | drive;
        fdc->pcn_irq = cyl;
        raise_irq(fdc);
        finish_command(fdc);
        break; }
    case 0x08: { /* SENSE INTERRUPT STATUS */
        uint8_t res[2] = { fdc->st0_irq, fdc->pcn_irq };
        set_result(fdc, res, 2);
        fdc->irq_pending = 0;
        break; }
    case 0x06: { /* READ DATA */
        int drive = fdc->params[0] & 3;
        int head = (fdc->params[2] & 1);
        uint8_t track = fdc->params[1];
        uint8_t sector = fdc->params[3];
        uint8_t size_code = fdc->params[4];
        int sz = 128 << size_code;
        int spt = fdc->sectors_per_track;
        size_t offset = ((track * fdc->heads + head) * spt + (sector - 1)) * fdc->sector_size;
        uint8_t* dest = dma_buffer(fdc, sz);
        if (dest && offset + sz <= fdc->disk_size) {
            memcpy(dest, fdc->disk + offset, sz);
            fdc->st0 = drive;
            fdc->st1 = 0;
            fdc->st2 = 0;
        } else {
            fdc->st0 = drive | 0x40;
            fdc->st1 = 0x20; /* ND */
            fdc->st2 = 0x00;
        }
        uint8_t res[7] = { fdc->st0, fdc->st1, fdc->st2, track, (uint8_t)head, sector, size_code };
        set_result(fdc, res, 7);
        raise_irq(fdc);
        break; }
    default:
        finish_command(fdc);
        break;
    }
}

int codex_fdc_init(CodexFdc* fdc, CodexCore* core, const char* image_path) {
    if (!fdc || !core) return -1;
    memset(fdc, 0, sizeof(*fdc));
    fdc->core = core;
    fdc->msr = 0x80;
    fdc->sector_size = 512;
    /* default geometry 1.44MB */
    fdc->heads = 2;
    fdc->sectors_per_track = 18;
    fdc->tracks = 80;
    if (image_path) {
        FILE* f = fopen(image_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            fdc->disk = (uint8_t*)malloc(sz);
            if (fdc->disk) {
                fread(fdc->disk, 1, sz, f);
                fdc->disk_size = sz;
                /* guess geometry based on common floppy sizes */
                switch (fdc->disk_size) {
                case 184320: /* 180K 5.25" SS */
                    fdc->heads = 1;
                    fdc->sectors_per_track = 9;
                    fdc->tracks = 40;
                    break;
                case 368640: /* 360K 5.25" DS */
                    fdc->heads = 2;
                    fdc->sectors_per_track = 9;
                    fdc->tracks = 40;
                    break;
                case 737280: /* 720K 3.5" DS */
                    fdc->heads = 2;
                    fdc->sectors_per_track = 9;
                    fdc->tracks = 80;
                    break;
                case 1228800: /* 1.2M 5.25" */
                    fdc->heads = 2;
                    fdc->sectors_per_track = 15;
                    fdc->tracks = 80;
                    break;
                case 1474560: /* 1.44M 3.5" */
                    fdc->heads = 2;
                    fdc->sectors_per_track = 18;
                    fdc->tracks = 80;
                    break;
                default:
                    break; /* keep defaults */
                }
            }
            fclose(f);
        }
    }
    return 0;
}

void codex_fdc_destroy(CodexFdc* fdc) {
    if (!fdc) return;
    free(fdc->disk);
    fdc->disk = NULL;
}

uint8_t codex_fdc_io_read(CodexFdc* fdc, uint16_t port) {
    switch (port) {
    case 0x3F4:
        return fdc->msr;
    case 0x3F5:
        if (fdc->state == FDC_STATE_RESULT && fdc->result_pos < fdc->result_len) {
            uint8_t v = fdc->result[fdc->result_pos++];
            if (fdc->result_pos >= fdc->result_len)
                finish_command(fdc);
            return v;
        }
        return 0;
    case 0x3F7:
        return 0; /* disk change not emulated */
    default:
        return 0xFF;
    }
}

void codex_fdc_io_write(CodexFdc* fdc, uint16_t port, uint8_t value) {
    switch (port) {
    case 0x3F2:
        fdc->dor = value;
        if (!(value & 0x04)) { /* reset */
            fdc->st0_irq = 0xC0; /* invalid */
            fdc->pcn_irq = 0;
            fdc->irq_pending = 0;
            finish_command(fdc);
        }
        break;
    case 0x3F5:
        if (fdc->state == FDC_STATE_COMMAND) {
            fdc->cmd = value;
            fdc->param_count = 0;
            switch (fdc->cmd & 0x1F) {
            case 0x03: fdc->param_expected = 2; fdc->state = FDC_STATE_PARAMS; fdc->msr = 0x90; break;
            case 0x04: fdc->param_expected = 1; fdc->state = FDC_STATE_PARAMS; fdc->msr = 0x90; break;
            case 0x07: fdc->param_expected = 1; fdc->state = FDC_STATE_PARAMS; fdc->msr = 0x90; break;
            case 0x0F: fdc->param_expected = 2; fdc->state = FDC_STATE_PARAMS; fdc->msr = 0x90; break;
            case 0x06: fdc->param_expected = 8; fdc->state = FDC_STATE_PARAMS; fdc->msr = 0x90; break;
            case 0x08: fdc->param_expected = 0; exec_command(fdc); break;
            default: fdc->param_expected = 0; finish_command(fdc); break;
            }
        } else if (fdc->state == FDC_STATE_PARAMS) {
            if (fdc->param_count < (int)sizeof(fdc->params))
                fdc->params[fdc->param_count++] = value;
            if (fdc->param_count >= fdc->param_expected)
                exec_command(fdc);
        }
        break;
    case 0x3F7:
        /* ignore */
        break;
    }
}

