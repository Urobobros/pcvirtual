#pragma once

#include <stdint.h>

#ifdef _WIN32
#include <Windows.h>
#include <winhvplatform.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct CodexCore; /* forward declaration */

typedef struct {
    uint8_t imr_master;
    uint8_t imr_slave;
    uint8_t vector_master;
    uint8_t vector_slave;
    uint8_t expect_icw_master;
    uint8_t expect_icw_slave;
} CodexPic;

void codex_pic_init(CodexPic* pic);
void codex_pic_io_write(CodexPic* pic, uint16_t port, uint8_t value);
uint8_t codex_pic_io_read(CodexPic* pic, uint16_t port);
void codex_pic_raise_irq(CodexPic* pic, struct CodexCore* core, uint8_t irq);

#ifdef __cplusplus
}
#endif

