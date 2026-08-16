#define _POSIX_C_SOURCE 200809L

#include "install.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../dependency_handling/dependency_handling.h"
#include "../../installed_handling/installed_handling.h"
#include "../../package_handling/package_handling.h"
#include "command_logic/command_logic.h"

/* ------------------------------------------------------------------------- */
/* Process                                                                   */
/* ------------------------------------------------------------------------- */

static int run_command(char* const argv[]) {
        pid_t pid;
        int status;

        pid = fork();

        if (pid < 0) return -1;

        if (pid == 0) {
                execvp(argv[0], argv);
                perror(argv[0]);
                _exit(127);
        }

        if (waitpid(pid, &status, 0) < 0) return -1;

        if (!WIFEXITED(status)) return -1;

        return WEXITSTATUS(status);
}

/* ------------------------------------------------------------------------- */
/* Build                                                                     */
/* ------------------------------------------------------------------------- */

static int build_package(const struct ark_package* package,
                         const char* source_archive,
                         const char* build_directory) {
        char script_path[ARK_PATH_MAX];
        FILE* file;
        char* argv[4];
        int result;

        if (setenv("ARK_SOURCE_ARCHIVE", source_archive, 1) != 0) return -1;

        if (setenv("ARK_BUILD_DIR", build_directory, 1) != 0) return -1;

        if (setenv("ARK_PACKAGE_NAME", package->name, 1) != 0) return -1;

        if (setenv("ARK_PACKAGE_VERSION", package->version, 1) != 0) return -1;

        if (snprintf(script_path, sizeof(script_path), "%s/.ark-build.sh",
                     build_directory) >= (int)sizeof(script_path))
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
            "    SOURCE_DIR=\"$ARK_BUILD_DIR\"\n"
            "fi\n"
            "\n"
            "export ARK_SOURCE_DIR=\"$SOURCE_DIR\"\n"
            "cd \"$ARK_SOURCE_DIR\"\n"
            "\n"
            ". \"$1\"\n"
            "build\n",
            file);

        fclose(file);

        argv[0] = "/bin/sh";
        argv[1] = script_path;
        argv[2] = (char*)package->recipe_path;
        argv[3] = NULL;

        result = run_command(argv);

        unlink(script_path);

        return result;
}

/* ------------------------------------------------------------------------- */
/* Build directory                                                           */
/* ------------------------------------------------------------------------- */

static int remove_build_directory(const char* path) {
        char* argv[4];
        pid_t pid;
        int status;

        argv[0] = "rm";
        argv[1] = "-rf";
        argv[2] = (char*)path;
        argv[3] = NULL;

        pid = fork();

        if (pid < 0) return -1;

        if (pid == 0) {
                execvp(argv[0], argv);
                perror("rm");
                _exit(127);
        }

        if (waitpid(pid, &status, 0) < 0) return -1;

        if (!WIFEXITED(status)) return -1;

        return WEXITSTATUS(status);
}

/* Supported source archive extensions, tried in this order. tar -xf
 * auto-detects the actual compression from file contents regardless
 * of which of these matched, so no changes are needed downstream in
 * the extraction script -- this only decides which file to hand it. */
static const char* const source_archive_extensions[] = {"tar.xz", "tar.gz",
                                                        "tar.bz2", "tar", NULL};

/*
 * Finds the fetched source archive for package/version under
 * sources_root, trying each supported extension in turn. On success,
 * fills source_archive with the full path and returns 0. On failure
 * (no matching file for any supported extension), returns -1.
 */
static int find_source_archive(const char* sources_root,
                               const struct ark_package* package,
                               char* source_archive,
                               size_t source_archive_size) {
        int i;

        for (i = 0; source_archive_extensions[i] != NULL; i++) {
                if (snprintf(source_archive, source_archive_size,
                             "%s/%s/%s/sources.%s", sources_root, package->name,
                             package->version, source_archive_extensions[i]) >=
                    (int)source_archive_size) {
                        fprintf(stderr, "ark: source path too long\n");

                        return -1;
                }

                if (ark_is_regular_file(source_archive)) return 0;
        }

        return -1;
}

/* ------------------------------------------------------------------------- */
/* Install                                                                    */
/* ------------------------------------------------------------------------- */

static int install_package(const struct ark_package* package,
                           const char* sources_root) {
        char source_archive[ARK_PATH_MAX];
        char ark_tmp[ARK_PATH_MAX];
        char template[ARK_PATH_MAX];
        char* build_directory;
        const char* home;
        int result;

        home = getenv("HOME");

        if (home == NULL) {
                fprintf(stderr, "ark: HOME is not set\n");

                return -1;
        }

        if (find_source_archive(sources_root, package, source_archive,
                                sizeof(source_archive)) != 0) {
                fprintf(stderr, "ark: source not fetched: %s/%s\n",
                        package->name, package->version);

                fprintf(stderr, "ark: run: ark fetch\n");

                return -1;
        }

        /*
         * Ark build directories live inside ~/.ark/tmp.
         *
         * This is intentional: /tmp may be mounted noexec, while
         * Autotools and other build systems routinely execute binaries
         * they compile during configuration.
         */
        if (snprintf(ark_tmp, sizeof(ark_tmp), "%s/.ark/tmp", home) >=
            (int)sizeof(ark_tmp)) {
                fprintf(stderr, "ark: temporary path too long\n");

                return -1;
        }

        if (mkdir(ark_tmp, 0700) != 0 && errno != EEXIST) {
                perror("ark: mkdir");
                return -1;
        }

        if (snprintf(template, sizeof(template), "%s/ark-build-XXXXXX",
                     ark_tmp) >= (int)sizeof(template)) {
                fprintf(stderr, "ark: build path too long\n");

                return -1;
        }

        build_directory = mkdtemp(template);

        if (build_directory == NULL) {
                perror("ark: mkdtemp");
                return -1;
        }

        printf("==> Installing %s-%s\n", package->name, package->version);

        printf("    source: %s\n", source_archive);

        printf("    build:  %s\n", build_directory);

        result = build_package(package, source_archive, build_directory);

        if (result != 0) {
                fprintf(stderr, "ark: build failed: %s-%s\n", package->name,
                        package->version);

                fprintf(stderr, "ark: build directory: %s\n", build_directory);

                return -1;
        }

        if (remove_build_directory(build_directory) != 0) {
                fprintf(stderr,
                        "ark: warning: could not remove build directory: %s\n",
                        build_directory);
        }

        printf("==> Installed %s-%s\n", package->name, package->version);

        return 0;
}

/* ------------------------------------------------------------------------- */
/* Dependency-driven install                                                 */
/* ------------------------------------------------------------------------- */

struct install_context {
        const char* sources_root;
        const char* installed_root;
};

/*
 * Called by ark_resolve_dependencies once per package, in dependency
 * order (a package's dependencies are always installed before it).
 * Every package resolved this way is recorded as implicitly
 * installed; the root package the user actually asked for gets
 * promoted to explicit afterwards, in ark_command_install.
 */
static int install_dependency_callback(const struct ark_package* package,
                                       void* context) {
        struct install_context* ctx = context;

        if (install_package(package, ctx->sources_root) != 0) return -1;

        if (ark_installed_record(ctx->installed_root, package, 0) != 0) {
                fprintf(stderr,
                        "ark: warning: could not record install state for %s\n",
                        package->name);
        }

        return 0;
}

/* ------------------------------------------------------------------------- */
/* Command                                                                   */
/* ------------------------------------------------------------------------- */

int ark_command_install(int argc, char** argv) {
        const char* home;
        char recipes_root[ARK_PATH_MAX];
        char sources_root[ARK_PATH_MAX];
        char installed_root[ARK_PATH_MAX];
        int i;
        int failed;

        if (argc < 1) {
                fprintf(stderr, "ark: install: missing package\n");

                return 1;
        }

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

        if (snprintf(sources_root, sizeof(sources_root), "%s/.ark/sources",
                     home) >= (int)sizeof(sources_root)) {
                fprintf(stderr, "ark: source path too long\n");

                return 1;
        }

        if (snprintf(installed_root, sizeof(installed_root),
                     "%s/.ark/cache/installed",
                     home) >= (int)sizeof(installed_root)) {
                fprintf(stderr, "ark: installed-cache path too long\n");

                return 1;
        }

        failed = 0;

        for (i = 0; i < argc; i++) {
                struct install_context context;

                printf("==> Resolving dependencies for %s\n", argv[i]);

                context.sources_root   = sources_root;
                context.installed_root = installed_root;

                if (ark_resolve_dependencies(argv[i], recipes_root,
                                             install_dependency_callback,
                                             &context) != 0) {
                        fprintf(stderr, "ark: install failed: %s\n", argv[i]);

                        failed = 1;
                        continue;
                }

                if (ark_installed_mark_explicit(installed_root, argv[i]) != 0) {
                        fprintf(stderr,
                                "ark: warning: could not mark %s as explicitly "
                                "installed\n",
                                argv[i]);
                }
        }

        return failed ? 1 : 0;
}

ARK_COMMAND("install", "ark install <package> [package...]",
            "Build and install packages", ark_command_install);
