#ifndef ARK_PACKAGE_HANDLING_H
#define ARK_PACKAGE_HANDLING_H

#include <stddef.h>

#define ARK_PATH_MAX 4096
#define ARK_PACKAGE_NAME_MAX 256
#define ARK_PACKAGE_VERSION_MAX 256

struct ark_package {
    char name[ARK_PACKAGE_NAME_MAX];
    char version[ARK_PACKAGE_VERSION_MAX];
    char recipe_path[ARK_PATH_MAX];
};

int ark_is_directory(const char *path);

int ark_is_regular_file(const char *path);

int ark_find_package(
    const char *recipes_root,
    const char *package_name,
    struct ark_package *package
);

#endif
