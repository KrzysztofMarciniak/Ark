#ifndef ARK_PACKAGE_HANDLING_H
#define ARK_PACKAGE_HANDLING_H

#include <stddef.h>

#define ARK_PATH_MAX      4096
#define ARK_NAME_MAX       256
#define ARK_VERSION_MAX     64
#define ARK_MAX_DEPENDS     64

/*
 * Recipe layout, matching the example recipe.sh:
 *
 *   <recipes_root>/<name>/<version>/recipe.sh
 *
 * The package name and version come from the directory structure,
 * not from variables inside recipe.sh. recipe.sh only declares
 * ARK_SOURCE_URL, ARK_SOURCE_SHA256, ARK_TYPE, ARK_DEPENDS,
 * ARK_BINARIES, and the build()/remove() shell functions.
 */
struct ark_package {
    char name[ARK_NAME_MAX];
    char version[ARK_VERSION_MAX];
    char recipe_path[ARK_PATH_MAX];

    /* NULL-terminated list of dependency package names. */
    char *depends[ARK_MAX_DEPENDS + 1];

    /* Backing storage for the strings pointed to by depends[]. */
    char depends_storage[ARK_PATH_MAX];
};

/*
 * Look up a package by name under recipes_root. If multiple version
 * directories exist for the package, the highest version (compared
 * as dot-separated numeric components) is selected.
 *
 * On success, fills *package and returns 0. On failure (package not
 * found, no version directories, or recipe.sh missing/unreadable),
 * returns -1 and leaves *package unspecified.
 */
int
ark_find_package(
    const char *recipes_root,
    const char *name,
    struct ark_package *package
);

int
ark_is_regular_file(const char *path);

#endif
