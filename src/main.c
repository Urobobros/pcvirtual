#include "codex_core.h"
#include <stdio.h>

int main(int argc, char** argv) {
    const char* bios = (argc > 1) ? argv[1] : "bios.bin";
    CodexCore core;
    if (codex_core_init(&core, bios) != 0) {
        fprintf(stderr, "Failed to initialize Codex core.\n");
        return 1;
    }

    int rc = codex_core_run(&core);
    codex_core_destroy(&core);
    return rc;
}
