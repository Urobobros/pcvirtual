/* codex_core.c */

#include "codex_core.h"
#include "codex_pit.h"
#include "codex_pic.h"
#include "codex_nmi.h"
#include "port_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <WinHvPlatform.h>
#include <WinHvPlatformDefs.h>
#ifdef _MSC_VER
#pragma comment(lib, "WinHvPlatform.lib")
#endif

/* Jednoduché shadow registry pro pár zařízení */
static uint8_t ppi_61_last = 0, port_63_last = 0;
static uint8_t cga3d8_last = 0, cga3b8_last = 0;
static uint8_t cga_status  = 0;

/* DMA 8237 (minimální stub, stačí na reset/flipflop/porty) */
static uint8_t  dma_main_cmd = 0, dma_req = 0, dma_mask = 0x0F, dma_mode = 0;
static uint8_t  dma_temp = 0, dma_clear = 0;
static uint8_t  dma_page[8] = {0};   /* 0x80–0x87 */
static uint16_t dma_addr[4] = {0}, dma_count[4] = {0};
static uint8_t  dma_flipflop = 0;    /* 0=LSB next, 1=MSB next */


static LARGE_INTEGER _last_time = {0}; // Pro sledování časových intervalů
#endif

int codex_core_run(CodexCore* core) {
    if (!core) return -1;

    /* ochrana před nekonečným pollováním neimplementovaných portů */
    uint16_t last_unknown = 0xffff;
    unsigned unknown_count = 0;

    /* malý debug BDA ticks */
#ifdef _WIN32
    LARGE_INTEGER _dbg_freq, _dbg_last;
    QueryPerformanceFrequency(&_dbg_freq);
    QueryPerformanceCounter(&_dbg_last);
    uint32_t* bda_ticks = (uint32_t*)((uint8_t*)core->memory + 0x046C);
    uint32_t last_ticks = *bda_ticks;
#endif

    while (1) {
        WHV_RUN_VP_EXIT_CONTEXT exit_ctx;
        HRESULT hr = WHvRunVirtualProcessor(core->partition, 0, &exit_ctx, sizeof(exit_ctx));
        if (FAILED(hr)) {
            fprintf(stderr, "WHvRunVirtualProcessor failed: 0x%lx\n", hr);
            return -1;
        }

        switch (exit_ctx.ExitReason) {
        case WHvRunVpExitReasonX64IoPortAccess: {
            WHV_X64_IO_PORT_ACCESS_CONTEXT* io = &exit_ctx.IoPortAccess;
            uint16_t port = io->PortNumber;
            uint32_t value = (uint32_t)io->Rax;

            /* PIT 0x40–0x43 */
            if (port >= 0x40 && port <= 0x43) {
                if (io->AccessInfo.IsWrite) {
                    codex_pit_io_write(&core->pit, port, (uint8_t)value);
                } else {
                    io->Rax = codex_pit_io_read(&core->pit, port);
                }
                port_log_io(io, io->AccessInfo.IsWrite ? "pit_write" : "pit_read");
                last_unknown = 0xffff; unknown_count = 0;

            /* nmi 0xA0 */
            } else if (port == 0xA0) {
                if (io->AccessInfo.IsWrite) {
                    codex_nmi_io_write(&core->nmi, (uint8_t)value);
                    port_log_io(io, "nmi_write");
                } else {
                    io->Rax = codex_nmi_io_read(&core->nmi);
                    port_log_io(io, "nmi_read");
                }
                last_unknown = 0xffff; unknown_count = 0;

            /* PIC master (PC/XT má jen 0x20/0x21) */
            } else if (port == 0x20 || port == 0x21) {
                if (io->AccessInfo.IsWrite) {
                    codex_pic_io_write(&core->pic, port, (uint8_t)value);
                } else {
                    io->Rax = codex_pic_io_read(&core->pic, port);
                }
                port_log_io(io, "pic");
                last_unknown = 0xffff; unknown_count = 0;

            /* PPI 0x61 */
            } else if (port == 0x61) {
                if (io->AccessInfo.IsWrite) ppi_61_last = (uint8_t)value;
                else io->Rax = ppi_61_last;
                port_log_io(io, "ppi61");
                last_unknown = 0xffff; unknown_count = 0;

            /* „latch“ 0x63 – pro XT klávesnici bývá využíván */
            } else if (port == 0x63) {
                if (io->AccessInfo.IsWrite) port_63_last = (uint8_t)value;
                else io->Rax = port_63_last;
                port_log_io(io, "ppi63");
                last_unknown = 0xffff; unknown_count = 0;

            /* CGA mode (0x3D8/0x3B8) */
            } else if (port == 0x3D8 || port == 0x3B8) {
                uint8_t *p = (port == 0x3D8) ? &cga3d8_last : &cga3b8_last;
                if (io->AccessInfo.IsWrite) *p = (uint8_t)value;
                else io->Rax = *p;
                port_log_io(io, "cga_mode");
                last_unknown = 0xffff; unknown_count = 0;

            /* CGA status 0x3DA (read-only) */ 
            } else if (port == 0x3DA) {
                if (!io->AccessInfo.IsWrite) {
                    cga_status ^= 0x08;                  /* toggle VRETRACE */
                    io->Rax = (uint8_t)(cga_status | 0x01); /* display enable */
                }
                port_log_io(io, "cga_status");
                last_unknown = 0xffff; unknown_count = 0;

            /* DMA 8237 0x00–0x0F */
            } else if (port <= 0x0F) {
                uint16_t p = port & 0x0F;
                if (io->AccessInfo.IsWrite) {
                    switch (p) {
                        case 0x08: dma_main_cmd = (uint8_t)value; break; /* Command */
                        case 0x09: dma_req = (uint8_t)value; break; /* Request */
                        case 0x0A: dma_mask = (uint8_t)value; break; /* Single Mask */
                        case 0x0B: dma_mode = (uint8_t)value; break; /* Mode */
                        case 0x0C: dma_flipflop = 0; break;              /* Clear FF */
                        case 0x0D: /* Master Clear */
                            dma_temp = (uint8_t)value;
                            dma_flipflop = 0;
                            dma_mask = 0x0F;          /* zamaskovat všechny kanály */
                            break;
                        case 0x0E: dma_clear = (uint8_t)value; break; /* Clear Mask (all) */
                        default: {
                            int ch = (p >> 1) & 3;
                            uint16_t* t = (p & 1) ? &dma_count[ch] : &dma_addr[ch];
                            if (!dma_flipflop) { *t = (*t & 0xFF00) | (uint8_t)value; dma_flipflop = 1; }
                            else               { *t = (*t & 0x00FF) | ((uint16_t)value << 8); dma_flipflop = 0; }
                        } break;
                    }
                } else {
                    uint8_t ret = 0;
                    switch (p) {
                        case 0x08: ret = dma_main_cmd; break;
                        case 0x09: ret = dma_req; break;
                        case 0x0A: ret = dma_mask; break;
                        case 0x0B: ret = dma_mode; break;
                        case 0x0C: ret = 0; dma_flipflop = 0; break; /* Read FF (a zároveň clear) */
                        case 0x0D: ret = dma_temp; break;
                        case 0x0E: ret = dma_clear; break;
                        default: {
                            int ch = (p >> 1) & 3;
                            uint16_t t = (p & 1) ? dma_count[ch] : dma_addr[ch];
                            ret = (!dma_flipflop) ? (uint8_t)(t & 0xFF) : (uint8_t)(t >> 8);
                            dma_flipflop ^= 1;
                        }
                    }
                    io->Rax = ret;
                }
                port_log_io(io, io->AccessInfo.IsWrite ? "dma_write" : "dma_read");
                last_unknown = 0xffff; unknown_count = 0;

            /* DMA page 0x80–0x8F (jen 0x80–0x87 mají význam v XT) */
            } else if (port >= 0x80 && port <= 0x8F) {
                uint8_t idx = (uint8_t)(port & 0x0F);
                if (idx < 8) {
                    if (io->AccessInfo.IsWrite) dma_page[idx] = (uint8_t)value;
                    else                        io->Rax = dma_page[idx];
                } else {
                    if (!io->AccessInfo.IsWrite) io->Rax = 0xFF; /* default */
                }
                port_log_io(io, io->AccessInfo.IsWrite ? "dma_page_write" : "dma_page_read");
                last_unknown = 0xffff; unknown_count = 0;

            /* Neznámé porty */
            } else if (io->AccessInfo.IsWrite) {
                port_log_io(io, "unhandled");
                if (port == last_unknown) {
                    if (++unknown_count >= 16) return -1;
                } else { last_unknown = port; unknown_count = 1; }

            } else {
                io->Rax = 0; /* default read */
                port_log_io(io, "unhandled");
                if (port == last_unknown) {
                    if (++unknown_count >= 16) return -1;
                } else { last_unknown = port; unknown_count = 1; }
            }

            /* Posun RIP + případně vrácení RAX po IN */
            WHV_REGISTER_NAME reg_names[2];
            WHV_REGISTER_VALUE reg_vals[2];
            reg_names[0] = WHvX64RegisterRip;
            reg_vals[0].Reg64 = exit_ctx.VpContext.Rip + exit_ctx.VpContext.InstructionLength;
            UINT32 reg_count = 1;
            if (!io->AccessInfo.IsWrite) {
                reg_names[1] = WHvX64RegisterRax;
                reg_vals[1].Reg64 = io->Rax;
                reg_count = 2;
            }
            HRESULT set_hr = WHvSetVirtualProcessorRegisters(core->partition, 0, reg_names, reg_count, reg_vals);
            if (FAILED(set_hr)) {
                fprintf(stderr, "WHvSetVirtualProcessorRegisters failed: 0x%lx\n", set_hr);
                return -1;
            }
            break;
        }

        case WHvRunVpExitReasonX64Halt:
            /* běžíme dál — PIT nás vzbudí přes IRQ0 */
            break;

        case WHvRunVpExitReasonX64InterruptWindow:
            /* CPU je připraven přijmout přerušení → pokus o injekci */
            codex_pic_try_inject(&core->pic, core);
            break;

        default:
            printf("Unhandled exit: %u\n", exit_ctx.ExitReason);
            return -1;
        }

        /* Periodické věci */
        codex_pit_update(&core->pit, core);
        codex_pic_try_inject(&core->pic, core);
    }

    return 0;
}
