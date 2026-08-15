#define _POSIX_C_SOURCE 200809L

#include "autoremove.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../installed_handling/installed_handling.h"
#include "../../package_handling/package_handling.h"
#include "../remove/remove.h"
#include "command_logic/command_logic.h"

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static int name_index(char** names, size_t count, const char* name) {
        size_t i;

        for (i = 0; i < count; i++) {
                if (strcmp(names[i], name) == 0) return (int)i;
        }

        return -1;
}

/*
 * Runs one pass: figures out which installed packages are orphans
 * (not explicitly installed, and not depended on by anything else
 * still installed), prints them, and — if confirmed — removes them.
 *
 * Returns 1 if at least one package was removed this pass (so the
 * caller should run another pass, since removing an orphan can
 * orphan its own dependencies), 0 if there was nothing to do or the
 * user declined, -1 on error.
 */
static int autoremove_pass(const char* recipes_root, const char* installed_root,
                           int assume_yes) {
        char** names;
        size_t count;
        size_t i;
        int* referenced;
        int* is_explicit;
        size_t orphan_count;
        int removed_any;

        if (ark_installed_list(installed_root, &names) != 0) {
                fprintf(stderr, "ark: could not read installed packages\n");

                return -1;
        }

        for (count = 0; names[count] != NULL; count++);

        if (count == 0) {
                ark_installed_free_list(names);
                printf("==> Nothing installed\n");
                return 0;
        }

        referenced  = calloc(count, sizeof(*referenced));
        is_explicit = calloc(count, sizeof(*is_explicit));

        if (referenced == NULL || is_explicit == NULL) {
                free(referenced);
                free(is_explicit);
                ark_installed_free_list(names);
                return -1;
        }

        for (i = 0; i < count; i++) {
                char depends_storage[ARK_PATH_MAX];
                char* deps[ARK_MAX_DEPENDS + 1];
                size_t k;

                is_explicit[i] =
                    ark_installed_is_explicit(installed_root, names[i]);

                if (ark_installed_get_depends(
                        installed_root, names[i], depends_storage,
                        sizeof(depends_storage), deps,
                        sizeof(deps) / sizeof(deps[0])) != 0)
                        continue;

                for (k = 0; deps[k] != NULL; k++) {
                        int idx = name_index(names, count, deps[k]);

                        if (idx >= 0) referenced[idx] = 1;
                }
        }

        orphan_count = 0;

        for (i = 0; i < count; i++) {
                if (!is_explicit[i] && !referenced[i]) orphan_count++;
        }

        if (orphan_count == 0) {
                free(referenced);
                free(is_explicit);
                ark_installed_free_list(names);
                printf("==> No orphaned packages\n");
                return 0;
        }

        printf("==> The following packages are no longer needed:\n");

        for (i = 0; i < count; i++) {
                if (!is_explicit[i] && !referenced[i])
                        printf("    %s\n", names[i]);
        }

        if (!assume_yes) {
                char response[16];

                printf("Remove them? [y/N] ");
                fflush(stdout);

                if (fgets(response, sizeof(response), stdin) == NULL ||
                    (response[0] != 'y' && response[0] != 'Y')) {
                        printf("==> Aborted\n");

                        free(referenced);
                        free(is_explicit);
                        ark_installed_free_list(names);

                        return 0;
                }
        }

        removed_any = 0;

        for (i = 0; i < count; i++) {
                struct ark_package package;

                if (is_explicit[i] || referenced[i]) continue;

                memset(&package, 0, sizeof(package));

                if (ark_find_package(recipes_root, names[i], &package) != 0) {
                        fprintf(stderr,
                                "ark: warning: recipe for %s not found; "
                                "forgetting install record without running "
                                "remove()\n",
                                names[i]);

                        ark_installed_forget(installed_root, names[i]);
                        removed_any = 1;
                        continue;
                }

                printf("==> Removing %s-%s\n", package.name, package.version);

                if (ark_remove_package(&package) != 0) {
                        fprintf(stderr, "ark: remove failed: %s-%s\n",
                                package.name, package.version);

                        continue;
                }

                ark_installed_forget(installed_root, names[i]);

                printf("==> Removed %s-%s\n", package.name, package.version);

                removed_any = 1;
        }

        free(referenced);
        free(is_explicit);
        ark_installed_free_list(names);

        return removed_any;
}

/* ------------------------------------------------------------------------- */
/* Command                                                                   */
/* ------------------------------------------------------------------------- */

int ark_command_autoremove(int argc, char** argv) {
        const char* home;
        char recipes_root[ARK_PATH_MAX];
        char installed_root[ARK_PATH_MAX];
        int assume_yes;
        int i;
        int result;

        home = getenv("HOME");

        if (home == NULL) {
                fprintf(stderr, "ark: HOME is not set\n");

                return 1;
        }

        if (snprintf(recipes_root, sizeof(recipes_root), "%s/.ark/recipes",
                     home) >= (int)sizeof(recipes_root)) {
                fprintf(stderr, "ark: recipe path too long\n");

                return 1;
        }

        if (snprintf(installed_root, sizeof(installed_root),
                     "%s/.ark/cache/installed",
                     home) >= (int)sizeof(installed_root)) {
                fprintf(stderr, "ark: installed-cache path too long\n");

                return 1;
        }

        assume_yes = 0;

        for (i = 0; i < argc; i++) {
                if (strcmp(argv[i], "-y") == 0 || strcmp(argv[i], "--yes") == 0)
                        assume_yes = 1;
        }

        /*
         * Removing an orphan can orphan its own now-unused dependencies,
         * so keep passing over the installed set until a pass removes
         * nothing (or the user declines).
         */
        for (;;) {
                result =
                    autoremove_pass(recipes_root, installed_root, assume_yes);

                if (result <= 0) break;
        }

        return result < 0 ? 1 : 0;
}

ARK_COMMAND("autoremove", "ark autoremove [-y|--yes]",
            "Remove packages that were installed only as dependencies and are "
            "no longer needed",
            ark_command_autoremove);
