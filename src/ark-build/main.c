#include <stdio.h>

#include "../hashmap/hashmap.h"

static int
command_build(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    puts("ark-build: build");

    return 0;
}

static int
command_clean(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    puts("ark-build: clean");

    return 0;
}

int
main(int argc, char **argv)
{
    struct ark_hashmap map;
    ark_command_handler handler;
    int result;

    if (ark_hashmap_init(&map, 8) != 0) {
        fprintf(stderr, "ark-build: failed to initialize commands\n");
        return 1;
    }

    if (ark_hashmap_put(&map, "build", command_build) != 0) {
        fprintf(stderr, "ark-build: failed to register build\n");
        ark_hashmap_free(&map);
        return 1;
    }

    if (ark_hashmap_put(&map, "clean", command_clean) != 0) {
        fprintf(stderr, "ark-build: failed to register clean\n");
        ark_hashmap_free(&map);
        return 1;
    }

    if (argc < 2) {
        puts("usage: ark-build <command> [args...]");
        ark_hashmap_free(&map);
        return 1;
    }

    handler = ark_hashmap_get(&map, argv[1]);

    if (handler == NULL) {
        printf("ark-build: unknown command: %s\n", argv[1]);
        ark_hashmap_free(&map);
        return 1;
    }

    result = handler(argc - 2, argv + 2);

    ark_hashmap_free(&map);

    return result;
}
