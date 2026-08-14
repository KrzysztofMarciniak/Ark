#define _POSIX_C_SOURCE 200809L

#include "remove.h"
#include "command_logic/command_logic.h"
#include "../../package_handling/package_handling.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>


/* ------------------------------------------------------------------------- */
/* Process                                                                   */
/* ------------------------------------------------------------------------- */

static int
run_command(char *const argv[])
{
    pid_t pid;
    int status;

    pid = fork();

    if (pid < 0)
        return -1;

    if (pid == 0) {
        execvp(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0)
        return -1;

    if (!WIFEXITED(status))
        return -1;

    return WEXITSTATUS(status);
}


/* ------------------------------------------------------------------------- */
/* Remove                                                                    */
/* ------------------------------------------------------------------------- */

static int
remove_package(const struct ark_package *package)
{
    char template[] = "/tmp/ark-remove-XXXXXX";
    char *remove_directory;
    char script_path[ARK_PATH_MAX];
    FILE *file;
    char *argv[4];
    int result;

    remove_directory = mkdtemp(template);

    if (remove_directory == NULL) {
        perror("ark: mkdtemp");
        return -1;
    }

    if (snprintf(
            script_path,
            sizeof(script_path),
            "%s/.ark-remove.sh",
            remove_directory
        ) >= (int)sizeof(script_path)) {
        rmdir(remove_directory);
        return -1;
    }

    file = fopen(script_path, "w");

    if (file == NULL) {
        perror("ark: cannot create remove script");
        rmdir(remove_directory);
        return -1;
    }

    /*
     * $1 is the recipe path.
     *
     * Source the recipe, then call its remove() function.
     */
    fputs(
        "#!/bin/sh\n"
        "set -e\n"
        ". \"$1\"\n"
        "remove\n",
        file
    );

    fclose(file);

    /*
     * Run through /bin/sh instead of executing the temporary
     * script directly. This avoids problems with noexec /tmp.
     */
    argv[0] = "/bin/sh";
    argv[1] = script_path;
    argv[2] = (char *)package->recipe_path;
    argv[3] = NULL;

    result = run_command(argv);

    unlink(script_path);
    rmdir(remove_directory);

    return result;
}


/* ------------------------------------------------------------------------- */
/* Command                                                                   */
/* ------------------------------------------------------------------------- */

int
ark_command_remove(int argc, char **argv)
{
    const char *home;
    char recipes_root[ARK_PATH_MAX];
    struct ark_package package;

    if (argc < 1) {
        fprintf(
            stderr,
            "ark: remove: missing package\n"
        );

        return 1;
    }

    home = getenv("HOME");

    if (home == NULL) {
        fprintf(
            stderr,
            "ark: HOME is not set\n"
        );

        return 1;
    }

    if (snprintf(
            recipes_root,
            sizeof(recipes_root),
            "%s/.ark/recipes",
            home
        ) >= (int)sizeof(recipes_root)) {
        fprintf(
            stderr,
            "ark: recipe path too long\n"
        );

        return 1;
    }

    memset(&package, 0, sizeof(package));

    printf(
        "==> Searching recipes for %s\n",
        argv[0]
    );

    if (ark_find_package(
            recipes_root,
            argv[0],
            &package
        ) != 0) {
        fprintf(
            stderr,
            "ark: package not found: %s\n",
            argv[0]
        );

        return 1;
    }

    printf(
        "    found: %s/%s\n",
        package.name,
        package.version
    );

    printf(
        "==> Removing %s-%s\n",
        package.name,
        package.version
    );

    if (remove_package(&package) != 0) {
        fprintf(
            stderr,
            "ark: remove failed: %s-%s\n",
            package.name,
            package.version
        );

        return 1;
    }

    printf(
        "==> Removed %s-%s\n",
        package.name,
        package.version
    );

    return 0;
}


ARK_COMMAND(
    "remove",
    "ark remove <package>",
    "Remove a package",
    ark_command_remove
);
