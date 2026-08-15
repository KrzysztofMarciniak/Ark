#ifndef ARK_COMMAND_REMOVE_H
#define ARK_COMMAND_REMOVE_H

#include "../../package_handling/package_handling.h"

int
ark_command_remove(int argc, char **argv);

/*
 * Runs a package's remove() recipe function. Exposed so other
 * commands (e.g. autoremove) can remove a package without going
 * through the ark_command_remove argv/recipe-lookup path.
 */
int
ark_remove_package(const struct ark_package *package);

#endif
