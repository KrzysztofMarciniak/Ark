#ifndef ARK_DEPENDENCY_HANDLING_H
#define ARK_DEPENDENCY_HANDLING_H

#include "../package_handling/package_handling.h"

#define ARK_MAX_DEPENDENCY_DEPTH 128

typedef int (*ark_dependency_callback)(const struct ark_package* package,
                                       void* context);

int ark_resolve_dependencies(const char* package_name, const char* recipes_root,
                             ark_dependency_callback callback, void* context);

#endif
