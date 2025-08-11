/* port_log.c */

#include "port_log.h"

#ifdef _WIN32
#include <stdio.h>
#include <stdint.h>
#include <windows.h>

/* POZN.: předpokládám, že WHV_X64_IO_PORT_ACCESS_CONTEXT je deklarován v port_log.h
   nebo někde globálně v projektu. */

/* Stav loggeru */
static LARGE_INTEGER g_freq = {0};
static uint64_t      g_t0   = 0;     /* QPC start */
static uint64_t      g_idx  = 0;     /* běžící index řádku */

/* ms od prvního volání (QPC) */
static double _now_ms(void)
{
    if (!g_freq.QuadPart)
        QueryPerformanceFrequency(&g_freq);

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if (!g_t0)
        g_t0 = (uint64_t)now.QuadPart;

    return ((double)((uint64_t)now.QuadPart - g_t0) * 1000.0) / (double)g_freq.QuadPart;
}

/* Helper: hezké vypsání hodnoty podle velikosti */
static void _fmt_val(char *out, size_t n, uint8_t size, uint32_t value)
{
    switch (size) {
        case 1:  _snprintf(out, n, "0x%02X",  (unsigned)(value & 0xFFu));    break;
        case 2:  _snprintf(out, n, "0x%04X",  (unsigned)(value & 0xFFFFu));  break;
        default: _snprintf(out, n, "0x%08X",  (unsigned)(value));            break;
    }
}

void port_log_io(const WHV_X64_IO_PORT_ACCESS_CONTEXT* io, const char* tag)
{
#if defined(_DEBUG) || defined(PORT_DEBUG)
    const char *dir = io->AccessInfo.IsWrite ? "OUT" : "IN ";
    const uint8_t size = io->AccessInfo.AccessSize;
    const uint16_t port = io->PortNumber;
    const uint32_t value = (uint32_t)io->Rax;

    char vbuf[16];
    _fmt_val(vbuf, sizeof(vbuf), size, value);

    const double ms = _now_ms();
    printf("index: %llu %s port 0x%04X, size %u, value %s  # %s [%10.3f ms]\n",
           (unsigned long long)g_idx++,
           dir, port, (unsigned)size, vbuf,
           tag ? tag : "",
           ms);
#else
    (void)io; (void)tag;
#endif
}

#endif /* _WIN32 */
