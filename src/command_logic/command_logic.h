#ifndef ARK_COMMAND_LOGIC_H
#define ARK_COMMAND_LOGIC_H

#include <stddef.h>

typedef int (*ark_command_handler)(int argc, char **argv);

struct ark_command_definition {
    const char *name;
    const char *parent;
    const char *usage;
    const char *description;
    ark_command_handler handler;
};

struct ark_command_registry {
    const struct ark_command_definition *commands;
    size_t command_count;
};

#define ARK_COMMAND(name, usage, description, handler)                 \
    static const struct ark_command_definition                         \
    ark_command_definition_##handler                                   \
    __attribute__((used, section("ark_commands"))) = {                 \
        name,                                                           \
        NULL,                                                           \
        usage,                                                          \
        description,                                                    \
        handler                                                          \
    }

#define ARK_SUBCOMMAND(parent, name, usage, description, handler)      \
    static const struct ark_command_definition                         \
    ark_subcommand_definition_##handler                                \
    __attribute__((used, section("ark_commands"))) = {                 \
        name,                                                           \
        parent,                                                         \
        usage,                                                          \
        description,                                                    \
        handler                                                          \
    }

void
ark_command_registry_init(
    struct ark_command_registry *registry
);

void
ark_command_registry_free(
    struct ark_command_registry *registry
);

int
ark_command_logic_execute(
    struct ark_command_registry *registry,
    int argc,
    char **argv
);

void
ark_command_logic_help(
    const struct ark_command_registry *registry
);

#endif
