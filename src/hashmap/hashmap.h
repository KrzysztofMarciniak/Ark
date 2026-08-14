#ifndef ARK_HASHMAP_H
#define ARK_HASHMAP_H

#include <stddef.h>

typedef int (*ark_command_handler)(int argc, char **argv);

struct ark_hashmap_entry {
    const char *key;
    ark_command_handler handler;
};

struct ark_hashmap {
    struct ark_hashmap_entry *entries;
    size_t capacity;
    size_t count;
};

int
ark_hashmap_init(struct ark_hashmap *map, size_t capacity);

void
ark_hashmap_free(struct ark_hashmap *map);

int
ark_hashmap_put(
    struct ark_hashmap *map,
    const char *key,
    ark_command_handler handler
);

ark_command_handler
ark_hashmap_get(
    const struct ark_hashmap *map,
    const char *key
);

#endif
