#include "version.h"

#include <stdio.h>

#include "../../../command_logic/command_logic.h"

#define ARK_VERSION "0.0.2"

int ark_command_version(int argc, char** argv) {
        (void)argc;
        (void)argv;

        puts("Ark " ARK_VERSION);

        return 0;
}

ARK_COMMAND("version", "ark version", "Show Ark version", ark_command_version);
