#include <stdio.h>

#include "../command_logic/command_logic.h"
#include "prerequisite/ark_directories_exist.h"
#include "prerequisite/programs_required.h"
#include "prerequisite/ark_repo_exists.h"
#include <stdbool.h>

int main(int argc, char** argv) {
        struct ark_command_registry registry;
        int result;

        if (ark_check_directory_existance()) return 1;

        if (!ark_check_repo()) {
                fprintf(stderr, "ark: no repository found inside ~/.ark/recipes\n");
                return 1;
        }

        if (ark_check_programs_required()) return 1;

        ark_command_registry_init(&registry);

        if (argc < 2) {
                ark_command_logic_help(&registry);
                ark_command_registry_free(&registry);
                return 1;
        }

        result = ark_command_logic_execute(&registry, argc - 1, argv + 1);

        ark_command_registry_free(&registry);

        return result;
}
