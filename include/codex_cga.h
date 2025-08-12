#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CodexCga;

typedef struct CodexCga CodexCga;

CodexCga* codex_cga_create(uint8_t* memory);
void codex_cga_destroy(CodexCga* cga);
void codex_cga_update(CodexCga* cga);
void codex_cga_dump_text(CodexCga* cga, const char* path);

#ifdef __cplusplus
}
#endif

