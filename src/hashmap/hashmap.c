#include "hashmap.h"

#include <stdlib.h>
#include <string.h>

#define ARK_HASHMAP_MIN_CAPACITY 8
#define ARK_HASHMAP_LOAD_NUMERATOR 7
#define ARK_HASHMAP_LOAD_DENOMINATOR 10

static unsigned long
hash_string(const char *key)
{
    unsigned long hash = 5381;
    unsigned char c;

    while ((c = (unsigned char)*key++) != 0)
        hash = ((hash << 5) + hash) ^ c;

    return hash;
}

static int
hashmap_resize(struct ark_hashmap *map, size_t new_capacity)
{
    struct ark_hashmap_entry *old_entries;
    size_t old_capacity;
    size_t i;

    old_entries = map->entries;
    old_capacity = map->capacity;

    map->entries = calloc(
        new_capacity,
        sizeof(*map->entries)
    );

    if (map->entries == NULL) {
        map->entries = old_entries;
        return -1;
    }

    map->capacity = new_capacity;
    map->count = 0;

    for (i = 0; i < old_capacity; ++i) {
        if (old_entries[i].key != NULL) {
            size_t index;
            size_t start;

            index = hash_string(old_entries[i].key) % map->capacity;
            start = index;

            while (map->entries[index].key != NULL) {
                index = (index + 1) % map->capacity;

                if (index == start) {
                    free(old_entries);
                    return -1;
                }
            }

            map->entries[index] = old_entries[i];
            map->count++;
        }
    }

    free(old_entries);
    return 0;
}

int
ark_hashmap_init(struct ark_hashmap *map, size_t capacity)
{
    if (capacity < ARK_HASHMAP_MIN_CAPACITY)
        capacity = ARK_HASHMAP_MIN_CAPACITY;

    map->entries = calloc(
        capacity,
        sizeof(*map->entries)
    );

    if (map->entries == NULL)
        return -1;

    map->capacity = capacity;
    map->count = 0;

    return 0;
}

void
ark_hashmap_free(struct ark_hashmap *map)
{
    free(map->entries);

    map->entries = NULL;
    map->capacity = 0;
    map->count = 0;
}

int
ark_hashmap_put(
    struct ark_hashmap *map,
    const char *key,
    ark_command_handler handler
)
{
    size_t index;
    size_t start;

    if (map->entries == NULL)
        return -1;

    if ((map->count + 1) * ARK_HASHMAP_LOAD_DENOMINATOR
        >= map->capacity * ARK_HASHMAP_LOAD_NUMERATOR) {

        if (hashmap_resize(map, map->capacity * 2) != 0)
            return -1;
    }

    index = hash_string(key) % map->capacity;
    start = index;

    while (map->entries[index].key != NULL) {
        if (strcmp(map->entries[index].key, key) == 0) {
            map->entries[index].handler = handler;
            return 0;
        }

        index = (index + 1) % map->capacity;

        if (index == start)
            return -1;
    }

    map->entries[index].key = key;
    map->entries[index].handler = handler;
    map->count++;

    return 0;
}

ark_command_handler
ark_hashmap_get(
    const struct ark_hashmap *map,
    const char *key
)
{
    size_t index;
    size_t start;

    if (map->entries == NULL || map->capacity == 0)
        return NULL;

    index = hash_string(key) % map->capacity;
    start = index;

    while (map->entries[index].key != NULL) {
        if (strcmp(map->entries[index].key, key) == 0)
            return map->entries[index].handler;

        index = (index + 1) % map->capacity;

        if (index == start)
            return NULL;
    }

    return NULL;
}
