#include "codex_core.h"
#include "codex_pit.h"
#include "codex_pic.h"
#include "codex_nmi.h"
#include "port_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#pragma comment(lib, "WinHvPlatform.lib")

static const uint64_t GPA_LIMIT = 0x00100000; /* 1 MB */

static void mirror_bios_region(uint8_t* base, size_t size) {
    if (size == 0) return;
    for (size_t pos = size; pos < 0x10000; pos += size)
        memcpy(base + pos, base, size);
}

int codex_core_hypervisor_present(void) {
    BOOL present = FALSE;
    UINT32 len = 0;
    HRESULT hr = WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &present, sizeof(present), &len);
    return (hr == S_OK && present) ? 1 : 0;
}

int codex_core_init(CodexCore* core, const char* bios_path) {
    if (!core) return -1;
    memset(core, 0, sizeof(*core));

    const char* path = bios_path;
    FILE* bios = fopen(path, "rb");
    if (!bios && strcmp(path, "ami_8088_bios_31jan89.bin") == 0) {
        bios = fopen("ivt.fw", "rb");
        if (bios) path = "ivt.fw";
    }
    if (!bios) {
        fprintf(stderr, "Failed to open BIOS: %s\n", bios_path);
        return -1;
    }

    core->memory_size = GPA_LIMIT;
    core->memory = VirtualAlloc(NULL, core->memory_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!core->memory) {
        fclose(bios);
        fprintf(stderr, "Failed to allocate guest memory.\n");
        return -1;
    }

    memset(core->memory, 0, core->memory_size);

    size_t read = fread(core->memory + 0xF0000, 1, 0x10000, bios);
    fclose(bios);
    if (read == 0) {
        fprintf(stderr, "Failed to read BIOS image.\n");
        return -1;
    }
    if (read < 0x10000)
        mirror_bios_region(core->memory + 0xF0000, read);

    uint8_t* mem = core->memory;
    if (mem[0xFFFF0] == 0xEA) {
        printf("BIOS reset vector jumps to %02X%02X:%02X%02X\n",
               mem[0xFFFF4], mem[0xFFFF3], mem[0xFFFF2], mem[0xFFFF1]);
    } else {
        puts("Warning: BIOS reset vector is unexpected; patching.");
        mem[0xFFFF0] = 0xEA;
        mem[0xFFFF1] = 0x00;
        mem[0xFFFF2] = 0x00;
        mem[0xFFFF3] = 0x00;
        mem[0xFFFF4] = 0xF0;
    }
    printf("BIOS loaded from %s (%zu bytes)\n", path, read);
    printf("Reset vector bytes: %02X %02X %02X %02X %02X\n",
           mem[0xFFFF0], mem[0xFFFF1], mem[0xFFFF2], mem[0xFFFF3], mem[0xFFFF4]);

    HRESULT hr = WHvCreatePartition(&core->partition);
    if (FAILED(hr)) {
        fprintf(stderr, "WHvCreatePartition failed: 0x%lx\n", hr);
        return -1;
    }

    WHV_PARTITION_PROPERTY prop;
    memset(&prop, 0, sizeof(prop));
    prop.ProcessorCount = 1;
    hr = WHvSetPartitionProperty(core->partition, WHvPartitionPropertyCodeProcessorCount,
                                 &prop, sizeof(prop));
    if (FAILED(hr)) {
        fprintf(stderr, "WHvSetPartitionProperty failed: 0x%lx\n", hr);
        return -1;
    }

    hr = WHvSetupPartition(core->partition);
    if (FAILED(hr)) {
        fprintf(stderr, "WHvSetupPartition failed: 0x%lx\n", hr);
        return -1;
    }

    hr = WHvMapGpaRange(core->partition, core->memory, 0, core->memory_size,
                        WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute);
    if (FAILED(hr)) {
        fprintf(stderr, "WHvMapGpaRange failed: 0x%lx\n", hr);
        return -1;
    }

    /* Mirror the first MB to addresses 0x100000-0x1FFFFF so real-mode wrap-around works. */
    hr = WHvMapGpaRange(core->partition, core->memory, core->memory_size, core->memory_size,
                        WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute);
    if (FAILED(hr)) {
        fprintf(stderr, "WHvMapGpaRange mirror failed: 0x%lx\n", hr);
        return -1;
    }

    hr = WHvCreateVirtualProcessor(core->partition, 0, 0);
    if (FAILED(hr)) {
        fprintf(stderr, "WHvCreateVirtualProcessor failed: 0x%lx\n", hr);
        return -1;
    }

    WHV_REGISTER_NAME names[6];
    WHV_REGISTER_VALUE values[6];
    memset(values, 0, sizeof(values));

    names[0] = WHvX64RegisterRip;
    values[0].Reg64 = 0xFFF0;

    names[1] = WHvX64RegisterRflags;
    values[1].Reg64 = 0x2;

    WHV_X64_SEGMENT_REGISTER cs = {0};
    cs.Base = 0xF0000;
    cs.Limit = 0xFFFF;
    cs.Selector = 0xF000;
    cs.Attributes = 0x009B;
    names[2] = WHvX64RegisterCs;
    values[2].Segment = cs;

    WHV_X64_SEGMENT_REGISTER ds = {0};
    ds.Base = 0;
    ds.Limit = 0xFFFF;
    ds.Selector = 0;
    ds.Attributes = 0x0093;
    names[3] = WHvX64RegisterDs;
    values[3].Segment = ds;
    names[4] = WHvX64RegisterEs;
    values[4].Segment = ds;
    names[5] = WHvX64RegisterSs;
    values[5].Segment = ds;

    hr = WHvSetVirtualProcessorRegisters(core->partition, 0, names, 6, values);
    if (FAILED(hr)) {
        fprintf(stderr, "WHvSetVirtualProcessorRegisters failed: 0x%lx\n", hr);
        return -1;
    }

    codex_pit_init(&core->pit);
    codex_pic_init(&core->pic);
    codex_nmi_init(&core->nmi);

    return 0;
}

int codex_core_load_program(CodexCore* core, const char* path, uint32_t offset) {
    if (!core || !core->memory || !path) return -1;
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    size_t read = fread(core->memory + offset, 1, core->memory_size - offset, f);
    fclose(f);
    return (read > 0) ? 0 : -1;
}

void codex_core_destroy(CodexCore* core) {
    if (!core) return;
    if (core->partition) {
        WHvDeleteVirtualProcessor(core->partition, 0);
        WHvDeletePartition(core->partition);
    }
    if (core->memory) {
        VirtualFree(core->memory, 0, MEM_RELEASE);
    }
}

int codex_core_run(CodexCore* core) {
    if (!core) return -1;

    /* Track repeated accesses to unknown ports to avoid infinite loops when
       the BIOS polls an unimplemented device. */
    uint16_t last_unknown = 0xffff;
    unsigned unknown_count = 0;

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
            if (port >= 0x40 && port <= 0x43) {
                if (io->AccessInfo.IsWrite) {
                    codex_pit_io_write(&core->pit, port, (uint8_t)value);
                } else {
                    io->Rax = codex_pit_io_read(&core->pit, port);
                }
                port_log_io(io, "pit");
                last_unknown = 0xffff;
                unknown_count = 0;
            } else if (port == 0x20 || port == 0x21 || port == 0xA1) {
                if (io->AccessInfo.IsWrite) {
                    codex_pic_io_write(&core->pic, port, (uint8_t)value);
                } else {
                    io->Rax = codex_pic_io_read(&core->pic, port);
                }
                port_log_io(io, "pic");
                last_unknown = 0xffff;
                unknown_count = 0;
            } else if (port == CODEX_NMI_PORT) {
                if (io->AccessInfo.IsWrite) {
                    codex_nmi_io_write(&core->nmi, (uint8_t)value);
                } else {
                    io->Rax = codex_nmi_io_read(&core->nmi);
                }
                port_log_io(io, "nmi");
                last_unknown = 0xffff;
                unknown_count = 0;
            } else if (io->AccessInfo.IsWrite) {
                port_log_io(io, "unhandled");
                if (port == last_unknown) {
                    if (++unknown_count >= 16) return -1;
                } else {
                    last_unknown = port;
                    unknown_count = 1;
                }
            } else {
                io->Rax = 0; /* return zero */
                port_log_io(io, "unhandled");
                if (port == last_unknown) {
                    if (++unknown_count >= 16) return -1;
                } else {
                    last_unknown = port;
                    unknown_count = 1;
                }
            }
            break;
        }
        case WHvRunVpExitReasonX64Halt:
            /* continue running; PIT will wake on next tick */
            break;
        default:
            printf("Unhandled exit: %u\n", exit_ctx.ExitReason);
            return -1;
        }

        codex_pit_update(&core->pit, core);
    }
}

#else /* !_WIN32 */

int codex_core_init(CodexCore* core, const char* bios_path) {
    (void)core;
    (void)bios_path;
    fprintf(stderr, "codex_core_init: Windows platform required.\n");
    return -1;
}

int codex_core_load_program(CodexCore* core, const char* path, uint32_t offset) {
    (void)core; (void)path; (void)offset; return -1;
}

int codex_core_hypervisor_present(void) {
    return 0;
}

void codex_core_destroy(CodexCore* core) {
    (void)core;
}

int codex_core_run(CodexCore* core) {
    (void)core;
    fprintf(stderr, "codex_core_run: Windows platform required.\n");
    return -1;
}

#endif

