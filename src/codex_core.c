/* codex_core.c */

#include "codex_core.h"
#include "codex_cga.h"
#include "port_log.h"
#include "codex_fdc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <windows.h>

#ifdef _WIN32
#include <WinHvPlatform.h>
#include <WinHvPlatformDefs.h>
#ifdef _MSC_VER
#pragma comment(lib, "WinHvPlatform.lib")
#endif
#endif


#ifndef IO_DEBUG_RAX
#define IO_DEBUG_RAX 0   /* 1 = zapnout debug RAX merge logy, 0 = vypnout */
#endif

static inline unsigned io_bytes_from_access_size(UINT8 accessSize)
{
    return (accessSize == 1 || accessSize == 2 || accessSize == 4) ? accessSize : 1;
}

/* ========================================================================== */
/* Režim testovacího skriptování hodnot z PCem logu                           */
/* ========================================================================== */
#ifndef FORCE_IO_SCRIPT
#define FORCE_IO_SCRIPT 0   /* 1 = ruční vracení; 0 = původní emulace */
#endif

/* Jediný sdílený čítač I/O (POZOR: nikde jinde ho nedefinovat!) */
static uint32_t g_io_index = 0;

#if FORCE_IO_SCRIPT
typedef struct { uint32_t index; uint16_t port; uint8_t value; } IoForcedRead;

/* --- Hodnoty z PCem logu --- */
static const IoForcedRead g_forced_reads[] = {
    {  5, 0x0040, 0x00 },
    { 13, 0x0041, 0x69 }, { 14, 0x0041, 0x74 },
    { 16, 0x0041, 0x4E }, { 17, 0x0041, 0x74 },
    { 19, 0x0041, 0x33 }, { 20, 0x0041, 0x74 },
    { 22, 0x0041, 0x18 }, { 23, 0x0041, 0x74 },
    { 25, 0x0041, 0xFD }, { 26, 0x0041, 0x73 },
    { 45, 0x0000, 0x01 }, { 46, 0x0000, 0x01 },
    { 47, 0x0001, 0x02 }, { 48, 0x0001, 0x02 },
    { 49, 0x0002, 0x04 }, { 50, 0x0002, 0x04 },
    { 51, 0x0003, 0x08 }, { 52, 0x0003, 0x08 },
    { 53, 0x0004, 0x10 }, { 54, 0x0004, 0x10 },
    { 55, 0x0005, 0x20 }, { 56, 0x0005, 0x20 },
    { 57, 0x0006, 0x40 }, { 58, 0x0006, 0x40 },
    { 59, 0x0007, 0x80 }, { 60, 0x0007, 0x80 },
};
static const size_t g_forced_reads_len =
    sizeof(g_forced_reads) / sizeof(g_forced_reads[0]);

static bool try_forced_read(uint32_t idx, uint16_t port, uint8_t* out_val) {
    for (size_t i = 0; i < g_forced_reads_len; ++i) {
        if (g_forced_reads[i].index == idx && g_forced_reads[i].port == port) {
            *out_val = g_forced_reads[i].value;
            return true;
        }
    }
    return false;
}
#endif /* FORCE_IO_SCRIPT */


/* ========================================================================== */
/* Shadow registry a jednoduché periferie (pouze v původním režimu)           */
/* ========================================================================== */

#ifdef _WIN32
#if !FORCE_IO_SCRIPT
#define GUEST_RAM_KB 640
#endif /* !FORCE_IO_SCRIPT */

/* Stav pro omezené logování IF/shadow */
typedef struct {
    uint8_t if_prev;
    uint8_t shadow_prev;
    uint8_t valid;
} IfShadowDebug;

static void dbg_log_if_shadow_once(CodexCore *core, IfShadowDebug *st)
{
    WHV_REGISTER_NAME names[2] = { WHvX64RegisterRflags, WHvRegisterInterruptState };
    WHV_REGISTER_VALUE vals[2];
    if (FAILED(WHvGetVirtualProcessorRegisters(core->partition, 0, names, 2, vals))) return;

    uint64_t rflags = vals[0].Reg64;
    uint8_t iflag = (uint8_t)((rflags >> 9) & 1);

    /* Správný typ: WHV_X64_INTERRUPT_STATE_REGISTER */
    WHV_X64_INTERRUPT_STATE_REGISTER isr = vals[1].InterruptState;
    uint8_t shadow = isr.InterruptShadow;

    if (!st->valid || st->if_prev != iflag || st->shadow_prev != shadow) {
        printf("IF=%u Shadow=%u\n", (unsigned)iflag, (unsigned)shadow);
        st->if_prev = iflag;
        st->shadow_prev = shadow;
        st->valid = 1;
    }
}
#endif /* _WIN32 */


/* ========================================================================== */
/* Hlavní běh jádra                                                           */
/* ========================================================================== */

int codex_core_run(CodexCore* core)
{
    if (!core) return -1;

#ifdef _WIN32
    IfShadowDebug ifdbg = {0};

    while (1) {
        WHV_RUN_VP_EXIT_CONTEXT exit_ctx;
        HRESULT hr = WHvRunVirtualProcessor(core->partition, 0, &exit_ctx, sizeof(exit_ctx));
        if (FAILED(hr)) {
            fprintf(stderr, "WHvRunVirtualProcessor failed: 0x%lx\n", hr);
            return -1;
        }

        /* Lehké sledování IF/shadow (log jen při změně) */
        dbg_log_if_shadow_once(core, &ifdbg);

        switch (exit_ctx.ExitReason) {

        case WHvRunVpExitReasonX64IoPortAccess: {
            WHV_X64_IO_PORT_ACCESS_CONTEXT* io = &exit_ctx.IoPortAccess;
            const uint16_t port = io->PortNumber;
            const bool isWrite = io->AccessInfo.IsWrite ? true : false;

#if !FORCE_IO_SCRIPT
            const uint32_t value = (uint32_t)io->Rax;  /* hodnota pro OUT (AL/AX/EAX) */
#endif

#if FORCE_IO_SCRIPT
            /* ---------------------- TEST MODE ---------------------- */
            if (isWrite) {
                port_log_io(io, "forced_out");
            } else {
                uint8_t ret = 0x00;
                if (try_forced_read(g_io_index, port, &ret)) {
                    /* do io->Rax dej hodnotu pro příslušnou šířku (0=1B,1=2B,2=4B) */
                    switch (io->AccessInfo.AccessSize) {
                        case 0: io->Rax = (uint64_t)ret; break; /* AL */
                        case 1: io->Rax = (uint64_t)ret; break; /* AX (low 16b) */
                        case 2: io->Rax = (uint64_t)ret; break; /* EAX (low 32b) */
                        default: io->Rax = (uint64_t)ret; break;
                    }
                    port_log_io(io, "forced_in");
                } else {
                    io->Rax = 0; /* default */
                    port_log_io(io, "forced_in(default)");
                }
            }

#else
            /* ---------------------- PŮVODNÍ EMULACE ---------------------- */

            /* --- PIT 0x40–0x43 ------------------------------------------------ */
            if (port >= 0x40 && port <= 0x43) {
                if (isWrite) {
                    codex_pit_io_write(&core->pit, port, (uint8_t)value);
                } else {
                    io->Rax = codex_pit_io_read(&core->pit, port);
                }
                port_log_io(io, isWrite ? "pit_write" : "pit_read");

            /* --- NMI mask 0xA0 ------------------------------------------------ */
            } else if (port == 0xA0) {
                if (isWrite) {
                    codex_nmi_io_write(&core->nmi, (uint8_t)value);
                } else {
                    io->Rax = codex_nmi_io_read(&core->nmi);
                }
                port_log_io(io, isWrite ? "nmi_write" : "nmi_read");

            /* --- PIC 8259A 0x20/0x21 ----------------------------------------- */
            } else if (port == 0x20 || port == 0x21) {
                if (isWrite) {
                    codex_pic_io_write(&core->pic, port, (uint8_t)value);
                } else {
                    
                    io->Rax = codex_pic_io_read(&core->pic, port);
                }
                port_log_io(io, isWrite ? "pic_write" : "pic_read");

                /* Po konfiguraci/EOI/IMR změnách hned zkusit injekci */
                codex_pic_try_inject(&core->pic, core);

            /* --- PPI 0x61 ----------------------------------------------------- */
            } else if (port == 0x61) {
                if (isWrite) {
                    uint8_t prev = core->ppi_61_last;
                    core->ppi_61_last = (uint8_t)value;

                    /* bit0 = GATE2. Na náběžné hraně restart CH2 fáze */
                    codex_pit_set_gate2(&core->pit,
                        (core->ppi_61_last & 0x01) != 0,
                        ((prev & 0x01) == 0) && ((core->ppi_61_last & 0x01) != 0));

                } else {
                    /* bit5 = OUT2 z PIT CH2 */
                    uint8_t out2 = codex_pit_out2(&core->pit) ? 0x20 : 0x00;
                    io->Rax = (core->ppi_61_last & (uint8_t)~0x20) | out2;
                }
                port_log_io(io, isWrite ? "ppi61_write" : "ppi61_read");

            /* --- SYS_PORTC 0x62 ---------------------------------------------- */
            } else if (port == 0x62) {
                /* DIP switches read via PPI port C */
                const uint8_t port62_mem_nibble =
                    (uint8_t)((GUEST_RAM_KB - 64) / 32);
                uint8_t valr;
                if (core->ppi_61_last & 0x08) {
                    /* bit3=1 selects disk/video DIP group. 1 drive, color 80x25 */
                    valr = 0x04;
                } else {
                    /* bit3=0 selects memory-size nibble */
                    valr = (core->ppi_61_last & 0x04)
                               ? (port62_mem_nibble & 0x0F)          /* low */
                               : ((port62_mem_nibble >> 4) & 0x0F);  /* high */
                }
                if (core->ppi_61_last & 0x02) valr |= 0x20; /* speaker */
                io->Rax = valr;
                port_log_io(io, "sys_portc");

            /* --- PPI 0x63 ----------------------------------------------------- */
            } else if (port == 0x63) {
                static uint8_t port_63_last_local = 0;
                if (isWrite) port_63_last_local = (uint8_t)value;
                else io->Rax = port_63_last_local;
                port_log_io(io, "ppi63");

            /* --- CGA mode/status --------------------------------------------- */
            } else if (port == 0x3D8 || port == 0x3B8) {
                static uint8_t cga3d8_last_local = 0, cga3b8_last_local = 0;
                uint8_t *p = (port == 0x3D8) ? &cga3d8_last_local : &cga3b8_last_local;
                if (isWrite) *p = (uint8_t)value;
                else io->Rax = *p;
                port_log_io(io, "cga_mode");

            } else if (port == 0x3DA) {
                static uint8_t cga_status_local = 0;
                if (!isWrite) {
                    cga_status_local ^= 0x08; /* střídáme vert retrace bit */
                    io->Rax = (uint8_t)(cga_status_local | 0x01);
                }
                port_log_io(io, "cga_status");

            /* --- FDC 0x3F0–0x3F7 -------------------------------------------- */
            } else if (port >= 0x3F0 && port <= 0x3F7) {
                if (isWrite) {
                    codex_fdc_io_write(&core->fdc, port, (uint8_t)value);
                } else {
                    io->Rax = codex_fdc_io_read(&core->fdc, port);
                }
                port_log_io(io, isWrite ? "fdc_write" : "fdc_read");

            /* --- DMA kontroler (0x00–0x0F) ----------------------------------- */
            } else if (port <= 0x0F) {
                if (isWrite) {
                    dma_io_write(&core->dma, port, (uint8_t)value);
                } else {
                    io->Rax = dma_io_read(&core->dma, port);
                }
                port_log_io(io,isWrite ? "dma_write" : "dma_read");

            } else if (port == 0x80) {                   // POST port
                if (isWrite) {
                    uint8_t code = (uint8_t)value;
                    printf("POST: %02X\n", code);
                } else {
                    io->Rax = 0x00;
                }
                port_log_io(io, "post");

            /* --- DMA stránkové registry 0x80–0x8F ----------------------------- */
            } else if (port >= 0x80 && port <= 0x8F) {
                if (isWrite) {
                    dma_io_write(&core->dma, port, (uint8_t)value);
                } else {
                    io->Rax = dma_io_read(&core->dma, port);
                }
                port_log_io(io, isWrite ? "dma_page_write" : "dma_page_read");

            /* --- Ostatní porty: default -------------------------------------- */
            } else {
                if (isWrite) {
                } else {
                    io->Rax = 0;

                }
                port_log_io(io, isWrite ? "unhandled_write" : "unhandled_read");
            }

#endif /* FORCE_IO_SCRIPT */

            /* ---------------- Posun RIP a vrácení RAX (pro IN) ---------------- */
            WHV_REGISTER_NAME reg_names[2];
            WHV_REGISTER_VALUE reg_vals[2];
            reg_names[0] = WHvX64RegisterRip;
            reg_vals[0].Reg64 = exit_ctx.VpContext.Rip + exit_ctx.VpContext.InstructionLength;
            UINT32 reg_count = 1;

            if (!isWrite) {
                /* Nejprve načti aktuální RAX (kvůli zachování vyšších bitů) */
                WHV_REGISTER_NAME get_name = WHvX64RegisterRax;
                WHV_REGISTER_VALUE get_val;
                HRESULT get_hr = WHvGetVirtualProcessorRegisters(core->partition, 0, &get_name, 1, &get_val);
                if (FAILED(get_hr)) {
                    fprintf(stderr, "WHvGetVirtualProcessorRegisters(RAX) failed: 0x%lx\n", get_hr);
                    return -1;
                }
                uint64_t rax_prev = get_val.Reg64;
                uint64_t dev_val  = io->Rax;  /* co nám „vrátilo zařízení“ (AL/AX/EAX dle AccessSize) */
                uint64_t rax_new;

                switch (io->AccessInfo.AccessSize) {
                    case 1: /* AL */
                        rax_new = (rax_prev & ~0xFFULL)        | (dev_val & 0xFFULL);
                        break;
                    case 2: /* AX */
                        rax_new = (rax_prev & ~0xFFFFULL)      | (dev_val & 0xFFFFULL);
                        break;
                    case 4: /* EAX */
                        rax_new = (rax_prev & ~0xFFFFFFFFULL)  | (dev_val & 0xFFFFFFFFULL);
                        break;
                    default: /* fallback = AL */
                        rax_new = (rax_prev & ~0xFFULL)        | (dev_val & 0xFFULL);
                        break;
                }


                #if IO_DEBUG_RAX
                    {
                        unsigned nbytes = io_bytes_from_access_size(io->AccessInfo.AccessSize);
                        /* hezký krátký log: index, port, bajty, původní a nový RAX + device hodnota */
                        printf("[RAX-RET] idx=%u port=0x%04X sz=%uB  RAX(prev)=0x%016llX  dev=0x%016llX  RAX(new)=0x%016llX\n",
                            (unsigned)g_io_index,
                            (unsigned)io->PortNumber,
                            (unsigned)io->AccessInfo.AccessSize,   
                            (unsigned long long)rax_prev,
                            (unsigned long long)dev_val,
                            (unsigned long long)rax_new);
                        /* extra: AL/AX/EAX detaily */
                        printf("          AL(prev)=0x%02X  AL(new)=0x%02X\n",
                            (unsigned)(rax_prev & 0xFFu),
                            (unsigned)(rax_new  & 0xFFu));
                    }
                #endif

                reg_names[1] = WHvX64RegisterRax;
                reg_vals[1].Reg64 = rax_new;
                reg_count = 2;
            }

            HRESULT set_hr = WHvSetVirtualProcessorRegisters(core->partition, 0, reg_names, reg_count, reg_vals);
            if (FAILED(set_hr)) {
                fprintf(stderr, "WHvSetVirtualProcessorRegisters failed: 0x%lx\n", set_hr);
                return -1;
            }

            /* Posuň globální index I/O až PO zpracování (společné pro oba režimy) */
            g_io_index++;
            break;
        }

        case WHvRunVpExitReasonX64Halt: {
            // HLT = NOP, ale povolíme scheduleru, aby "tekl čas"
            WHV_REGISTER_NAME ripName = WHvX64RegisterRip;
            WHV_REGISTER_VALUE ripVal;
            ripVal.Reg64 = exit_ctx.VpContext.Rip + exit_ctx.VpContext.InstructionLength;
            WHvSetVirtualProcessorRegisters(core->partition, 0, &ripName, 1, &ripVal);

            // malý yield – stačí SwitchToThread, případně Sleep(0) / Sleep(1)
            SwitchToThread();
            Sleep(0);
            break;
        }

        case WHvRunVpExitReasonX64InterruptWindow:
            /* Na WHPX tohle často neuvidíš – injekci řešíme pollováním níže. */
            codex_pic_try_inject(&core->pic, core);
            break;

        default:
            printf("Unhandled exit: %u\n", exit_ctx.ExitReason);
            return -1;
        }

        /* Periodické periferie a pokus o doručení IRQ po KAŽDÉM exitu */
        codex_pit_update(&core->pit, core);
        codex_pic_try_inject(&core->pic, core);
        codex_cga_update(core->cga);
    }

#else
    (void)core;
    fprintf(stderr, "WHPX path is Windows-only.\n");
    return -1;
#endif
    return 0;
}
