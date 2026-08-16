#ifndef ARK_DEPENDENCY_HANDLING_H
#define ARK_DEPENDENCY_HANDLING_H

#include "../package_handling/package_handling.h"

#define ARK_MAX_DEPENDENCY_DEPTH 128

typedef int (*ark_dependency_callback)(const struct ark_package* package,
                                       void* context);

int ark_resolve_dependencies(const char* package_name, const char* recipes_root,
                             ark_dependency_callback callback, void* context);

/*
 * Like ark_resolve_dependencies, but for several top-level targets at
 * once, sharing one "already resolved" set across all of them so a
 * dependency common to two or more targets is only resolved once.
 * See the .c file for full contract details (per_target_ok, failure
 * semantics).
 */
int ark_resolve_dependencies_multi(char** package_names, size_t package_count,
                                   const char* recipes_root,
                                   ark_dependency_callback callback,
                                   void* context, int* per_target_ok);

#endif
