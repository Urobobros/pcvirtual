#include "port_log.h"

#ifdef _WIN32
#include <stdio.h>

void port_log_io(const WHV_X64_IO_PORT_ACCESS_CONTEXT* io, const char* tag) {
#if defined(_DEBUG) || defined(PORT_DEBUG)
    const char* dir = io->AccessInfo.IsWrite ? "OUT" : "IN ";
    uint8_t size = io->AccessInfo.AccessSize;
    uint32_t value = (uint32_t)io->Rax;
    if (size == 1)
        printf("%s port 0x%04X, size 1, value 0x%02X  # %s\n", dir, io->PortNumber, value & 0xFF, tag);
    else if (size == 2)
        printf("%s port 0x%04X, size 2, value 0x%04X  # %s\n", dir, io->PortNumber, value & 0xFFFF, tag);
    else
        printf("%s port 0x%04X, size %u, value 0x%08X  # %s\n", dir, io->PortNumber, size, value, tag);
#else
    (void)io; (void)tag;
#endif
}
#endif

