#include <stdio.h>
#include "openclaw.h"
#include "cli.h"

int cmd_version(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("openclaw %s\n", OPENCLAW_VERSION);
    printf("  Native C implementation for Linux\n");
    printf("  Built: %s %s\n", __DATE__, __TIME__);
    return 0;
}
