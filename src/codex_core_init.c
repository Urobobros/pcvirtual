/* codex_core_init.c */

#include "codex_core.h"
#include "codex_cga.h"
#include <stdio.h>
#include <string.h>

static const uint64_t GPA_LIMIT = 0x00100000; /* 1 MB */

int codex_core_hypervisor_present(void) {
    BOOL present = FALSE;
    UINT32 len = 0;
    HRESULT hr = WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &present, sizeof(present), &len);
    return (hr == S_OK && present) ? 1 : 0;
}

int codex_core_load_program(CodexCore* core, const char* path, uint32_t offset) {
    if (!core || !core->memory || !path) return -1;
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    size_t read = fread((uint8_t*)core->memory + offset, 1, core->memory_size - offset, f);
    fclose(f);
    return (read > 0) ? 0 : -1;
}

void codex_core_destroy(CodexCore* core) {
    if (!core) return;
    if (core->partition) {
        WHvDeleteVirtualProcessor(core->partition, 0);
        WHvDeletePartition(core->partition);
    }
    if (core->cga) {
        codex_cga_destroy(core->cga);
        core->cga = NULL;
    }
    if (core->memory) {
        VirtualFree(core->memory, 0, MEM_RELEASE);
    }
}

static void mirror_bios_region(uint8_t* base, size_t size) {
    if (size == 0) return;
    for (size_t pos = size; pos < 0x10000; pos += size)
        memcpy(base + pos, base, size);
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
    hr = WHvSetPartitionProperty(core->partition, WHvPartitionPropertyCodeProcessorCount, &prop, sizeof(prop));
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

    /* mirror first MB at +1MB to emulate wrap */
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

    /* real-mode reset state */
    WHV_REGISTER_NAME names[6];
    WHV_REGISTER_VALUE values[6];
    memset(values, 0, sizeof(values));

    names[0] = WHvX64RegisterRip;     values[0].Reg64 = 0xFFF0;
    names[1] = WHvX64RegisterRflags;  values[1].Reg64 = 0x2;

    WHV_X64_SEGMENT_REGISTER cs = (WHV_X64_SEGMENT_REGISTER){0};
    cs.Base = 0xF0000; cs.Limit = 0xFFFF; cs.Selector = 0xF000; cs.Attributes = 0x009B;
    names[2] = WHvX64RegisterCs; values[2].Segment = cs;

    WHV_X64_SEGMENT_REGISTER ds = (WHV_X64_SEGMENT_REGISTER){0};
    ds.Base = 0; ds.Limit = 0xFFFF; ds.Selector = 0; ds.Attributes = 0x0093;
    names[3] = WHvX64RegisterDs; values[3].Segment = ds;
    names[4] = WHvX64RegisterEs; values[4].Segment = ds;
    names[5] = WHvX64RegisterSs; values[5].Segment = ds;

    hr = WHvSetVirtualProcessorRegisters(core->partition, 0, names, 6, values);
    if (FAILED(hr)) {
        fprintf(stderr, "WHvSetVirtualProcessorRegisters failed: 0x%lx\n", hr);
        return -1;
    }

    /* devices */
    codex_pit_init(&core->pit);
    codex_pic_init(&core->pic);
    if (codex_nmi_init(&core->nmi) < 0) {
        return -1;
    }
    dma_init(&core->dma);
    core->cga = codex_cga_create(core->memory);

    return 0;
}
