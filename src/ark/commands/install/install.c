#define _POSIX_C_SOURCE 200809L

#include "install.h"
#include "../../package_handling/package_handling.h"
#include "command_logic/command_logic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
/* Build                                                                     */
/* ------------------------------------------------------------------------- */

static int
build_package(
    const struct ark_package *package,
    const char *source_archive,
    const char *build_directory
)
{
    char script_path[ARK_PATH_MAX];
    FILE *file;
    char *argv[4];
    int result;

    if (setenv(
            "ARK_SOURCE_ARCHIVE",
            source_archive,
            1
        ) != 0)
        return -1;

    if (setenv(
            "ARK_BUILD_DIR",
            build_directory,
            1
        ) != 0)
        return -1;

    if (setenv(
            "ARK_PACKAGE_NAME",
            package->name,
            1
        ) != 0)
        return -1;

    if (setenv(
            "ARK_PACKAGE_VERSION",
            package->version,
            1
        ) != 0)
        return -1;

    if (snprintf(
            script_path,
            sizeof(script_path),
            "%s/.ark-build.sh",
            build_directory
        ) >= (int)sizeof(script_path))
        return -1;

    file = fopen(script_path, "w");

    if (file == NULL) {
        perror("ark: cannot create build script");
        return -1;
    }

    fputs(
        "#!/bin/sh\n"
        "set -e\n"
        "\n"
        "echo \"==> Extracting source\"\n"
        "\n"
        "tar -xf \"$ARK_SOURCE_ARCHIVE\" -C \"$ARK_BUILD_DIR\"\n"
        "\n"
        "SOURCE_DIR=$(find \"$ARK_BUILD_DIR\" "
        "-mindepth 1 -maxdepth 1 -type d | head -n 1)\n"
        "\n"
        "if [ -z \"$SOURCE_DIR\" ]; then\n"
        "    echo \"ark: could not find extracted source directory\" >&2\n"
        "    exit 1\n"
        "fi\n"
        "\n"
        "export ARK_SOURCE_DIR=\"$SOURCE_DIR\"\n"
        "cd \"$ARK_SOURCE_DIR\"\n"
        "\n"
        ". \"$1\"\n"
        "build\n",
        file
    );

    fclose(file);

    argv[0] = "/bin/sh";
    argv[1] = script_path;
    argv[2] = (char *)package->recipe_path;
    argv[3] = NULL;

    result = run_command(argv);

    unlink(script_path);

    return result;
}


/* ------------------------------------------------------------------------- */
/* Build directory                                                           */
/* ------------------------------------------------------------------------- */

static int
remove_build_directory(const char *path)
{
    char *argv[4];
    pid_t pid;
    int status;

    argv[0] = "rm";
    argv[1] = "-rf";
    argv[2] = (char *)path;
    argv[3] = NULL;

    pid = fork();

    if (pid < 0)
        return -1;

    if (pid == 0) {
        execvp(argv[0], argv);
        perror("rm");
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0)
        return -1;

    if (!WIFEXITED(status))
        return -1;

    return WEXITSTATUS(status);
}


/* ------------------------------------------------------------------------- */
/* Install                                                                    */
/* ------------------------------------------------------------------------- */

static int
install_package(
    const struct ark_package *package,
    const char *sources_root
)
{
    char source_archive[ARK_PATH_MAX];
    char template[] = "/tmp/ark-build-XXXXXX";
    char *build_directory;
    int result;

    if (snprintf(
            source_archive,
            sizeof(source_archive),
            "%s/%s/%s/sources.tar.xz",
            sources_root,
            package->name,
            package->version
        ) >= (int)sizeof(source_archive)) {
        fprintf(
            stderr,
            "ark: source path too long\n"
        );

        return -1;
    }

    if (!ark_is_regular_file(source_archive)) {
        fprintf(
            stderr,
            "ark: source not fetched: %s/%s\n",
            package->name,
            package->version
        );

        fprintf(
            stderr,
            "ark: run: ark fetch\n"
        );

        return -1;
    }

    build_directory = mkdtemp(template);

    if (build_directory == NULL) {
        perror("ark: mkdtemp");
        return -1;
    }

    printf(
        "==> Installing %s-%s\n",
        package->name,
        package->version
    );

    printf(
        "    source: %s\n",
        source_archive
    );

    printf(
        "    build:  %s\n",
        build_directory
    );

    result = build_package(
        package,
        source_archive,
        build_directory
    );

    if (result != 0) {
        fprintf(
            stderr,
            "ark: build failed: %s-%s\n",
            package->name,
            package->version
        );

        fprintf(
            stderr,
            "ark: build directory: %s\n",
            build_directory
        );

        return -1;
    }

    if (remove_build_directory(build_directory) != 0) {
        fprintf(
            stderr,
            "ark: warning: could not remove build directory: %s\n",
            build_directory
        );
    }

    printf(
        "==> Installed %s-%s\n",
        package->name,
        package->version
    );

    return 0;
}


/* ------------------------------------------------------------------------- */
/* Command                                                                   */
/* ------------------------------------------------------------------------- */

int
ark_command_install(int argc, char **argv)
{
    const char *home;
    char recipes_root[ARK_PATH_MAX];
    char sources_root[ARK_PATH_MAX];
    struct ark_package package;
    int i;
    int failed;

    if (argc < 1) {
        fprintf(
            stderr,
            "ark: install: missing package\n"
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

    if (snprintf(
            sources_root,
            sizeof(sources_root),
            "%s/.ark/sources",
            home
        ) >= (int)sizeof(sources_root)) {
        fprintf(
            stderr,
            "ark: source path too long\n"
        );

        return 1;
    }

    failed = 0;

    for (i = 0; i < argc; i++) {
        memset(&package, 0, sizeof(package));

        printf(
            "==> Searching recipes for %s\n",
            argv[i]
        );

        if (ark_find_package(
                recipes_root,
                argv[i],
                &package
            ) != 0) {
            fprintf(
                stderr,
                "ark: package not found: %s\n",
                argv[i]
            );

            failed = 1;
            continue;
        }

        printf(
            "    found: %s/%s\n",
            package.name,
            package.version
        );

        if (install_package(
                &package,
                sources_root
            ) != 0) {
            failed = 1;
            continue;
        }
    }

    return failed ? 1 : 0;
}


ARK_COMMAND(
    "install",
    "ark install <package> [package...]",
    "Build and install packages",
    ark_command_install
);
