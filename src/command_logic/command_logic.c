#include "command_logic.h"

#include <stdio.h>
#include <string.h>

extern const struct ark_command_definition __start_ark_commands[];
extern const struct ark_command_definition __stop_ark_commands[];

void ark_command_registry_init(struct ark_command_registry* registry) {
        registry->commands = __start_ark_commands;

        registry->command_count =
            (size_t)(__stop_ark_commands - __start_ark_commands);
}

void ark_command_registry_free(struct ark_command_registry* registry) {
        registry->commands      = NULL;
        registry->command_count = 0;
}

static const struct ark_command_definition* find_command(
    const struct ark_command_registry* registry, const char* name) {
        size_t i;

        for (i = 0; i < registry->command_count; ++i) {
                const struct ark_command_definition* command;

                command = &registry->commands[i];

                if (command->parent == NULL && strcmp(command->name, name) == 0)
                        return command;
        }

        return NULL;
}

static const struct ark_command_definition* find_subcommand(
    const struct ark_command_registry* registry, const char* parent,
    const char* name) {
        size_t i;

        for (i = 0; i < registry->command_count; ++i) {
                const struct ark_command_definition* command;

                command = &registry->commands[i];

                if (command->parent != NULL &&
                    strcmp(command->parent, parent) == 0 &&
                    strcmp(command->name, name) == 0)
                        return command;
        }

        return NULL;
}

int ark_command_logic_execute(struct ark_command_registry* registry, int argc,
                              char** argv) {
        const struct ark_command_definition* command;
        const struct ark_command_definition* subcommand;

        if (argc < 1) {
                fprintf(stderr, "ark: missing command\n");
                return 1;
        }

        command = find_command(registry, argv[0]);

        if (command == NULL) {
                fprintf(stderr, "ark: unknown command: %s\n", argv[0]);

                return 1;
        }

        /*
         * Try to resolve a subcommand.
         */
        if (argc >= 2) {
                subcommand = find_subcommand(registry, command->name, argv[1]);

                if (subcommand != NULL) {
                        if (subcommand->handler == NULL) {
                                fprintf(stderr,
                                        "ark: command has no handler: %s %s\n",
                                        command->name, subcommand->name);

                                return 1;
                        }

                        return subcommand->handler(argc - 2, argv + 2);
                }
        }

        /*
         * If the command itself has a handler,
         * execute it.
         */
        if (command->handler != NULL) {
                return command->handler(argc - 1, argv + 1);
        }

        /*
         * Otherwise it is a parent command and
         * requires a subcommand.
         */
        fprintf(stderr, "ark: %s: missing subcommand\n", command->name);

        return 1;
}

void ark_command_logic_help(const struct ark_command_registry* registry) {
        size_t i;

        puts("usage: ark <command> [args...]");
        puts("");
        puts("Commands:");

        for (i = 0; i < registry->command_count; ++i) {
                const struct ark_command_definition* command;

                command = &registry->commands[i];

                if (command->parent != NULL) continue;

                printf("  %-16s %s\n", command->name, command->description);
        }
}
