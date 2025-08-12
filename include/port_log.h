#pragma once

#include <stdint.h>

#ifdef _WIN32
#include <winhvplatform.h>
void port_log_io(const WHV_X64_IO_PORT_ACCESS_CONTEXT* io, const char* tag);
#else
struct WHV_X64_IO_PORT_ACCESS_CONTEXT;
static inline void port_log_io(const struct WHV_X64_IO_PORT_ACCESS_CONTEXT* io, const char* tag) {
    (void)io; (void)tag;
}
#endif

