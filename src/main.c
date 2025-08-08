#include "codex_core.h"
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

static int is_bios_file(const char* name) {
    size_t len = strlen(name);
#ifdef _WIN32
    #define STRICMP _stricmp
#else
    #define STRICMP strcasecmp
#endif
    if (len >= 4 && STRICMP(name + len - 4, ".bin") == 0) return 1;
    if (len >= 3 && STRICMP(name + len - 3, ".fw") == 0) return 1;
    return 0;
}

int main(int argc, char** argv) {
    const char* program = NULL;
    const char* bios = "ami_8088_bios_31jan89.bin";
    if (argc >= 2) {
        if (argc >= 3) {
            program = argv[1];
            bios = argv[2];
        } else {
            if (is_bios_file(argv[1]))
                bios = argv[1];
            else
                program = argv[1];
        }
    }

    if (!codex_core_hypervisor_present()) {
        fprintf(stderr, "Hypervisor not present.\n");
        return 1;
    }

    CodexCore core;
    if (codex_core_init(&core, bios) != 0) {
        fprintf(stderr, "Failed to initialize Codex core.\n");
        return 1;
    }

    if (program) {
        if (codex_core_load_program(&core, program, 0x10100) != 0)
            fprintf(stderr, "Warning: failed to load program %s\n", program);
    }

    int rc = codex_core_run(&core);
    codex_core_destroy(&core);
    return rc;
}
