#include <stdio.h>

#include "../command_logic/command_logic.h"
#include "prerequisite/ark_directories_exist.h"
#include "prerequisite/programs_required.h"
#include "prerequisite/ark_repo_exists.h"
#include <stdbool.h>

int main(int argc, char** argv) {
        struct ark_command_registry registry;
        int result;

        if (ark_check_directory_existance() != false) return true;
	
	if (ark_check_repo() != false){
		printf("\n");
		printf("No repository found inside ~/.ark/recipes\n");
		printf("Please check README.md");
		printf("\n");
	}

return true;

        if (ark_check_programs_required() != false) return true;

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
