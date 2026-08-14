#include <stdio.h>

#include "../command_logic/command_logic.h"

int
main(int argc, char **argv)
{
    struct ark_command_registry registry;
    int result;

    ark_command_registry_init(&registry);

    if (argc < 2) {
        ark_command_logic_help(&registry);
        ark_command_registry_free(&registry);
        return 1;
    }

    result = ark_command_logic_execute(
        &registry,
        argc - 1,
        argv + 1
    );

    ark_command_registry_free(&registry);

    return result;
}
