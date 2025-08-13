#pragma once

#include <stdio.h>

#ifndef FDC_DEBUG
#define FDC_DEBUG 1
#endif

#if FDC_DEBUG
#define FDCLOG(...) fprintf(stderr, "[FDC] " __VA_ARGS__)
#else
#define FDCLOG(...) (void)0
#endif

