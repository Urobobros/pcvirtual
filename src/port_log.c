/* port_log.c */

#include "port_log.h"

#ifdef _WIN32
#include <stdio.h>
#include <windows.h>

static LARGE_INTEGER _dbg_freq = {0};  // Přidání proměnné pro měření frekvence

void port_log_io(const WHV_X64_IO_PORT_ACCESS_CONTEXT* io, const char* tag) {
#if defined(_DEBUG) || defined(PORT_DEBUG)
    const char* dir = io->AccessInfo.IsWrite ? "OUT" : "IN ";
    uint8_t size = io->AccessInfo.AccessSize;
    uint32_t value = (uint32_t)io->Rax;

    // Inicializace frekvence, pokud ještě nebyla nastavena
    if (_dbg_freq.QuadPart == 0) {
        QueryPerformanceFrequency(&_dbg_freq);
    }

    // Logování času mezi operacemi
    static LARGE_INTEGER _last_time = {0};
    LARGE_INTEGER _now;
    QueryPerformanceCounter(&_now);
    uint64_t time_diff = (_now.QuadPart - _last_time.QuadPart) * 1000 / _dbg_freq.QuadPart;
    printf("Time Diff: %llu ms\n", time_diff);
    _last_time = _now;

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


