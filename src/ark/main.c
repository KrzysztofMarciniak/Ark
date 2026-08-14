#include <stdio.h>

#include "../hashmap/hashmap.h"

static int
command_install(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    puts("ark: install");

    return 0;
}

static int
command_source_add(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    puts("ark: source add");

    return 0;
}

static int
command_source(int argc, char **argv)
{
    struct ark_hashmap map;
    ark_command_handler handler;
    int result;

    if (ark_hashmap_init(&map, 8) != 0) {
        fprintf(stderr, "ark: failed to initialize source commands\n");
        return 1;
    }

    if (ark_hashmap_put(&map, "add", command_source_add) != 0) {
        fprintf(stderr, "ark: failed to register source add\n");
        ark_hashmap_free(&map);
        return 1;
    }

    if (argc < 1) {
        puts("ark: source: missing subcommand");
        ark_hashmap_free(&map);
        return 1;
    }

    handler = ark_hashmap_get(&map, argv[0]);

    if (handler == NULL) {
        printf("ark: source: unknown subcommand: %s\n", argv[0]);
        ark_hashmap_free(&map);
        return 1;
    }

    result = handler(argc - 1, argv + 1);

    ark_hashmap_free(&map);

    return result;
}

int
main(int argc, char **argv)
{
    struct ark_hashmap map;
    ark_command_handler handler;
    int result;

    if (ark_hashmap_init(&map, 16) != 0) {
        fprintf(stderr, "ark: failed to initialize commands\n");
        return 1;
    }

    if (ark_hashmap_put(&map, "install", command_install) != 0) {
        fprintf(stderr, "ark: failed to register install\n");
        ark_hashmap_free(&map);
        return 1;
    }

    if (ark_hashmap_put(&map, "source", command_source) != 0) {
        fprintf(stderr, "ark: failed to register source\n");
        ark_hashmap_free(&map);
        return 1;
    }

    if (argc < 2) {
        puts("usage: ark <command> [args...]");
        ark_hashmap_free(&map);
        return 1;
    }

    handler = ark_hashmap_get(&map, argv[1]);

    if (handler == NULL) {
        printf("ark: unknown command: %s\n", argv[1]);
        ark_hashmap_free(&map);
        return 1;
    }

    result = handler(argc - 2, argv + 2);

    ark_hashmap_free(&map);

    return result;
}
