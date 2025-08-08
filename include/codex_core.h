#pragma once

#include <stdint.h>
#include <stddef.h>

#include "codex_pit.h"
#include "codex_pic.h"

#ifdef _WIN32
#include <Windows.h>
#include <winhvplatform.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
typedef WHV_PARTITION_HANDLE CodexPartitionHandle;
#else
typedef void* CodexPartitionHandle;
#endif

typedef struct CodexCore {
    CodexPartitionHandle partition;
    uint8_t* memory;
    size_t memory_size;
    CodexPit pit;
    CodexPic pic;
} CodexCore;

int codex_core_init(CodexCore* core, const char* bios_path);
int codex_core_load_program(CodexCore* core, const char* path, uint32_t offset);
int codex_core_hypervisor_present(void);
void codex_core_destroy(CodexCore* core);
int codex_core_run(CodexCore* core);

#ifdef __cplusplus
}
#endif

