/* fetch.c */
#define _POSIX_C_SOURCE 200809L

#include "fetch.h"
#include "command_logic/command_logic.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define ARK_PATH_MAX 4096

#define ARK_PACKAGE_NAME_MAX    256
#define ARK_PACKAGE_VERSION_MAX 256
#define ARK_SHA256_MAX          65

struct ark_package {
    char name[ARK_PACKAGE_NAME_MAX];
    char version[ARK_PACKAGE_VERSION_MAX];
    char recipe_path[ARK_PATH_MAX];
    char source_url[ARK_PATH_MAX];
    char source_sha256[ARK_SHA256_MAX];
    int fetched;
};

struct ark_package_list {
    struct ark_package *items;
    size_t count;
    size_t capacity;
};

struct ark_repo {
    char name[ARK_PACKAGE_NAME_MAX];
    char path[ARK_PATH_MAX];
    struct ark_package_list packages;
};

struct ark_repo_list {
    struct ark_repo *items;
    size_t count;
    size_t capacity;
};


/* ------------------------------------------------------------------------- */
/* Process execution                                                        */
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
/* Filesystem                                                               */
/* ------------------------------------------------------------------------- */

static int
is_directory(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
is_regular_file(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int
mkdir_p(const char *path)
{
    char tmp[ARK_PATH_MAX];
    char *p;
    struct stat st;

    if (strlen(path) >= sizeof(tmp))
        return -1;

    strcpy(tmp, path);

    for (p = tmp + 1; *p != '\0'; ++p) {
        if (*p != '/')
            continue;

        *p = '\0';

        if (mkdir(tmp, 0755) != 0 &&
            (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode))) {
            return -1;
        }

        *p = '/';
    }

    if (mkdir(tmp, 0755) != 0 &&
        (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode))) {
        return -1;
    }

    return 0;
}


/* ------------------------------------------------------------------------- */
/* Recipe parsing                                                           */
/* ------------------------------------------------------------------------- */

/*
 * Extract:
 *
 *     ARK_SOURCE_URL="https://..."
 *
 * from recipe.sh without executing the recipe.
 *
 * Surrounding double quotes are stripped.
 */
static int
read_recipe_field(
    const char *recipe_path,
    const char *key,
    char *out,
    size_t out_size
)
{
    FILE *file;
    char line[ARK_PATH_MAX];
    size_t key_len;

    file = fopen(recipe_path, "r");

    if (file == NULL)
        return -1;

    key_len = strlen(key);

    while (fgets(line, sizeof(line), file) != NULL) {
        char *value;
        size_t value_len;

        if (strncmp(line, key, key_len) != 0)
            continue;

        if (line[key_len] != '=')
            continue;

        value = line + key_len + 1;
        value_len = strlen(value);

        while (value_len > 0 &&
               (value[value_len - 1] == '\n' ||
                value[value_len - 1] == '\r')) {
            value[--value_len] = '\0';
        }

        if (value_len >= 2 &&
            value[0] == '"' &&
            value[value_len - 1] == '"') {
            value[value_len - 1] = '\0';
            value++;
            value_len -= 2;
        }

        if (value_len >= out_size) {
            fclose(file);
            return -1;
        }

        strcpy(out, value);

        fclose(file);
        return 0;
    }

    fclose(file);

    return -1;
}


/* ------------------------------------------------------------------------- */
/* Dynamic package list                                                     */
/* ------------------------------------------------------------------------- */

static void
package_list_init(struct ark_package_list *list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void
package_list_free(struct ark_package_list *list)
{
    free(list->items);

    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int
package_list_add(
    struct ark_package_list *list,
    const struct ark_package *package
)
{
    struct ark_package *items;
    size_t new_capacity;

    if (list->count < list->capacity) {
        list->items[list->count++] = *package;
        return 0;
    }

    if (list->capacity == 0)
        new_capacity = 16;
    else
        new_capacity = list->capacity * 2;

    items = realloc(
        list->items,
        new_capacity * sizeof(*items)
    );

    if (items == NULL)
        return -1;

    list->items = items;
    list->capacity = new_capacity;
    list->items[list->count++] = *package;

    return 0;
}


/* ------------------------------------------------------------------------- */
/* Dynamic repository list                                                   */
/* ------------------------------------------------------------------------- */

static void
repo_list_init(struct ark_repo_list *list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void
repo_list_free(struct ark_repo_list *list)
{
    size_t i;

    for (i = 0; i < list->count; ++i)
        package_list_free(&list->items[i].packages);

    free(list->items);

    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int
repo_list_add(
    struct ark_repo_list *list,
    const char *name,
    const char *path
)
{
    struct ark_repo *items;
    struct ark_repo *repo;
    size_t new_capacity;

    if (list->count == list->capacity) {
        if (list->capacity == 0)
            new_capacity = 8;
        else
            new_capacity = list->capacity * 2;

        items = realloc(
            list->items,
            new_capacity * sizeof(*items)
        );

        if (items == NULL)
            return -1;

        list->items = items;
        list->capacity = new_capacity;
    }

    repo = &list->items[list->count];

    memset(repo, 0, sizeof(*repo));

    if (strlen(name) >= sizeof(repo->name) ||
        strlen(path) >= sizeof(repo->path))
        return -1;

    strcpy(repo->name, name);
    strcpy(repo->path, path);

    package_list_init(&repo->packages);

    list->count++;

    return 0;
}


/* ------------------------------------------------------------------------- */
/* Repository discovery                                                      */
/* ------------------------------------------------------------------------- */

/*
 * .ark/recipes/
 *
 * Every directory immediately below this directory is a repository.
 *
 * Example:
 *
 * .ark/recipes/
 * ├── ark-recipes/
 * └── another-repo/
 */
static int
find_repositories(
    const char *recipes_directory,
    struct ark_repo_list *repos
)
{
    DIR *dir;
    struct dirent *entry;
    char path[ARK_PATH_MAX];

    dir = opendir(recipes_directory);

    if (dir == NULL)
        return -1;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        if (entry->d_name[0] == '.')
            continue;

        if (snprintf(
                path,
                sizeof(path),
                "%s/%s",
                recipes_directory,
                entry->d_name
            ) >= (int)sizeof(path)) {
            closedir(dir);
            return -1;
        }

        if (!is_directory(path))
            continue;

        if (repo_list_add(
                repos,
                entry->d_name,
                path
            ) != 0) {
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);

    return 0;
}


/* ------------------------------------------------------------------------- */
/* Package discovery                                                         */
/* ------------------------------------------------------------------------- */

/*
 * Repository layout:
 *
 * <repo>/
 *     <package>/
 *         <version>/
 *             recipe.sh
 */
static int
find_repo_packages(struct ark_repo *repo)
{
    DIR *packages;
    struct dirent *package_entry;
    char package_path[ARK_PATH_MAX];

    packages = opendir(repo->path);

    if (packages == NULL)
        return -1;

    while ((package_entry = readdir(packages)) != NULL) {
        DIR *versions;
        struct dirent *version_entry;

        if (strcmp(package_entry->d_name, ".") == 0 ||
            strcmp(package_entry->d_name, "..") == 0)
            continue;

        if (package_entry->d_name[0] == '.')
            continue;

        if (snprintf(
                package_path,
                sizeof(package_path),
                "%s/%s",
                repo->path,
                package_entry->d_name
            ) >= (int)sizeof(package_path)) {
            closedir(packages);
            return -1;
        }

        if (!is_directory(package_path))
            continue;

        versions = opendir(package_path);

        if (versions == NULL) {
            closedir(packages);
            return -1;
        }

        while ((version_entry = readdir(versions)) != NULL) {
            char version_path[ARK_PATH_MAX];
            char recipe_path[ARK_PATH_MAX];
            struct ark_package package;

            if (strcmp(version_entry->d_name, ".") == 0 ||
                strcmp(version_entry->d_name, "..") == 0)
                continue;

            if (version_entry->d_name[0] == '.')
                continue;

            if (snprintf(
                    version_path,
                    sizeof(version_path),
                    "%s/%s",
                    package_path,
                    version_entry->d_name
                ) >= (int)sizeof(version_path)) {
                closedir(versions);
                closedir(packages);
                return -1;
            }

            if (!is_directory(version_path))
                continue;

            if (snprintf(
                    recipe_path,
                    sizeof(recipe_path),
                    "%s/recipe.sh",
                    version_path
                ) >= (int)sizeof(recipe_path)) {
                closedir(versions);
                closedir(packages);
                return -1;
            }

            if (!is_regular_file(recipe_path))
                continue;

            memset(&package, 0, sizeof(package));

            if (strlen(package_entry->d_name) >=
                sizeof(package.name) ||
                strlen(version_entry->d_name) >=
                sizeof(package.version) ||
                strlen(recipe_path) >=
                sizeof(package.recipe_path)) {
                closedir(versions);
                closedir(packages);
                return -1;
            }

            strcpy(package.name, package_entry->d_name);
            strcpy(package.version, version_entry->d_name);
            strcpy(package.recipe_path, recipe_path);

            if (read_recipe_field(
                    recipe_path,
                    "ARK_SOURCE_URL",
                    package.source_url,
                    sizeof(package.source_url)
                ) != 0) {
                fprintf(
                    stderr,
                    "ark: %s: missing ARK_SOURCE_URL\n",
                    recipe_path
                );

                closedir(versions);
                closedir(packages);
                return -1;
            }

            if (read_recipe_field(
                    recipe_path,
                    "ARK_SOURCE_SHA256",
                    package.source_sha256,
                    sizeof(package.source_sha256)
                ) != 0) {
                fprintf(
                    stderr,
                    "ark: %s: missing ARK_SOURCE_SHA256\n",
                    recipe_path
                );

                closedir(versions);
                closedir(packages);
                return -1;
            }

            package.fetched = 0;

            if (package_list_add(
                    &repo->packages,
                    &package
                ) != 0) {
                closedir(versions);
                closedir(packages);
                return -1;
            }
        }

        closedir(versions);
    }

    closedir(packages);

    return 0;
}


/* ------------------------------------------------------------------------- */
/* Source store                                                              */
/* ------------------------------------------------------------------------- */

static int
package_source_path(
    const struct ark_package *package,
    const char *sources_root,
    char *path,
    size_t path_size
)
{
    if (snprintf(
            path,
            path_size,
            "%s/%s/%s/sources.tar.xz",
            sources_root,
            package->name,
            package->version
        ) >= (int)path_size)
        return -1;

    return 0;
}

static int
package_is_fetched(
    const struct ark_package *package,
    const char *sources_root
)
{
    char path[ARK_PATH_MAX];

    if (package_source_path(
            package,
            sources_root,
            path,
            sizeof(path)
        ) != 0)
        return 0;

    return is_regular_file(path);
}

static void
check_repo_packages(
    struct ark_repo *repo,
    const char *sources_root
)
{
    size_t i;

    for (i = 0; i < repo->packages.count; ++i) {
        repo->packages.items[i].fetched =
            package_is_fetched(
                &repo->packages.items[i],
                sources_root
            );
    }
}


/* ------------------------------------------------------------------------- */
/* Unfetched cache                                                           */
/* ------------------------------------------------------------------------- */

static int
write_unfetched_cache(
    const struct ark_repo *repo,
    const char *cache_root
)
{
    char directory[ARK_PATH_MAX];
    char path[ARK_PATH_MAX];
    FILE *file;
    size_t i;

    if (snprintf(
            directory,
            sizeof(directory),
            "%s/unfetched",
            cache_root
        ) >= (int)sizeof(directory))
        return -1;

    if (mkdir_p(directory) != 0)
        return -1;

    if (snprintf(
            path,
            sizeof(path),
            "%s/%s",
            directory,
            repo->name
        ) >= (int)sizeof(path))
        return -1;

    file = fopen(path, "w");

    if (file == NULL)
        return -1;

    for (i = 0; i < repo->packages.count; ++i) {
        const struct ark_package *package;

        package = &repo->packages.items[i];

        if (package->fetched)
            continue;

        fprintf(
            file,
            "%s %s\n",
            package->name,
            package->version
        );
    }

    fclose(file);

    return 0;
}


/* ------------------------------------------------------------------------- */
/* Archive handling                                                          */
/* ------------------------------------------------------------------------- */

static const char *
archive_extension(const char *url)
{
    static const char *known[] = {
        ".tar.xz",
        ".tar.gz",
        ".tgz",
        ".tar.bz2",
        ".tbz2",
        NULL
    };

    size_t url_len;
    int i;

    url_len = strlen(url);

    for (i = 0; known[i] != NULL; ++i) {
        size_t ext_len;

        ext_len = strlen(known[i]);

        /*
         * Handle URLs with query strings.
         *
         * https://example/foo.tar.gz?download=1
         */
        if (url_len >= ext_len &&
            strcmp(
                url + url_len - ext_len,
                known[i]
            ) == 0)
            return known[i];
    }

    return NULL;
}


/*
 * Extract an archive into:
 *
 *     <temporary>/source/
 *
 * We deliberately use tar instead of shell commands so there is no shell
 * interpolation of the URL or paths.
 */
static int
extract_archive(
    const char *archive,
    const char *directory,
    const char *extension
)
{
    char *argv[8];

    /*
     * tar -xf archive -C directory
     */
    (void)extension;

    argv[0] = "tar";
    argv[1] = "-xf";
    argv[2] = (char *)archive;
    argv[3] = "-C";
    argv[4] = (char *)directory;
    argv[5] = NULL;

    return run_command(argv);
}


/*
 * Repackage the extracted source as:
 *
 *     sources.tar.xz
 */
static int
repackage_source(
    const char *source_directory,
    const char *output
)
{
    char *argv[8];

    /*
     * tar -cJf output -C source_directory .
     */
    argv[0] = "tar";
    argv[1] = "-cJf";
    argv[2] = (char *)output;
    argv[3] = "-C";
    argv[4] = (char *)source_directory;
    argv[5] = ".";
    argv[6] = NULL;

    return run_command(argv);
}


/*
 * Remove a temporary directory using:
 *
 *     rm -rf
 *
 * The path is supplied as an argv element, never through a shell.
 */
static int
remove_directory(const char *directory)
{
    char *argv[4];

    argv[0] = "rm";
    argv[1] = "-rf";
    argv[2] = (char *)directory;
    argv[3] = NULL;

    return run_command(argv);
}


/* ------------------------------------------------------------------------- */
/* Fetch one package                                                         */
/* ------------------------------------------------------------------------- */

static int
fetch_package(
    const struct ark_package *package,
    const char *sources_root
)
{
    char destination[ARK_PATH_MAX];
    char temporary_root[ARK_PATH_MAX];
    char download_path[ARK_PATH_MAX];
    char extracted_path[ARK_PATH_MAX];
    char archive_path[ARK_PATH_MAX];
    char checksum_file[ARK_PATH_MAX];
    char expected_line[ARK_PATH_MAX];
    char *curl_argv[9];
    char *verify_argv[4];
    FILE *file;
    int result;

    /*
     * Canonical source location:
     *
     * ~/.ark/sources/<package>/<version>/
     */
    if (snprintf(
            destination,
            sizeof(destination),
            "%s/%s/%s",
            sources_root,
            package->name,
            package->version
        ) >= (int)sizeof(destination)) {
        fprintf(stderr, "ark: source path too long\n");
        return -1;
    }

    if (mkdir_p(destination) != 0) {
        fprintf(
            stderr,
            "ark: cannot create source directory: %s\n",
            destination
        );
        return -1;
    }

    /*
     * Everything temporary lives inside the package directory.
     */
    if (snprintf(
            temporary_root,
            sizeof(temporary_root),
            "%s/.fetch",
            destination
        ) >= (int)sizeof(temporary_root))
        return -1;

    if (mkdir_p(temporary_root) != 0)
        return -1;

    if (snprintf(
            download_path,
            sizeof(download_path),
            "%s/source.download",
            temporary_root
        ) >= (int)sizeof(download_path))
        return -1;

    if (snprintf(
            extracted_path,
            sizeof(extracted_path),
            "%s/extracted",
            temporary_root
        ) >= (int)sizeof(extracted_path))
        return -1;

    if (snprintf(
            archive_path,
            sizeof(archive_path),
            "%s/sources.tar.xz",
            destination
        ) >= (int)sizeof(archive_path))
        return -1;

    if (snprintf(
            checksum_file,
            sizeof(checksum_file),
            "%s/checksum",
            temporary_root
        ) >= (int)sizeof(checksum_file))
        return -1;

    if (strlen(package->source_sha256) != 64) {
        fprintf(
            stderr,
            "ark: %s/%s: invalid SHA-256\n",
            package->name,
            package->version
        );
        remove_directory(temporary_root);
        return -1;
    }

    printf(
        "    fetching %s-%s\n",
        package->name,
        package->version
    );

    /*
     * Download.
     */
    curl_argv[0] = "curl";
    curl_argv[1] = "-L";
    curl_argv[2] = "--fail";
    curl_argv[3] = "--silent";
    curl_argv[4] = "--show-error";
    curl_argv[5] = "-o";
    curl_argv[6] = download_path;
    curl_argv[7] = (char *)package->source_url;
    curl_argv[8] = NULL;

    result = run_command(curl_argv);


    if (result != 0) {
        fprintf(stderr, "ark: curl failed\n");
        remove_directory(temporary_root);
        return -1;
    }

    /*
     * Write checksum manifest.
     */
    if (snprintf(
            expected_line,
            sizeof(expected_line),
            "%s  %s\n",
            package->source_sha256,
            download_path
        ) >= (int)sizeof(expected_line)) {
        remove_directory(temporary_root);
        return -1;
    }

    file = fopen(checksum_file, "w");

    if (file == NULL) {
        remove_directory(temporary_root);
        return -1;
    }

    fputs(expected_line, file);
    fclose(file);

    /*
     * Verify.
     */
    verify_argv[0] = "sha256sum";
    verify_argv[1] = "-c";
    verify_argv[2] = checksum_file;
    verify_argv[3] = NULL;

    result = run_command(verify_argv);

    if (result != 0) {
        fprintf(
            stderr,
            "ark: checksum mismatch: %s-%s\n",
            package->name,
            package->version
        );

        remove_directory(temporary_root);
        return -1;
    }

    printf(
        "    verified %s-%s\n",
        package->name,
        package->version
    );

    /*
     * Extract.
     */
    if (mkdir_p(extracted_path) != 0) {
        remove_directory(temporary_root);
        return -1;
    }

    if (extract_archive(
            download_path,
            extracted_path,
            archive_extension(package->source_url)
        ) != 0) {
        fprintf(
            stderr,
            "ark: failed to extract %s\n",
            package->source_url
        );

        remove_directory(temporary_root);
        return -1;
    }

    /*
     * Repackage everything into Ark's canonical format.
     *
     * sources.tar.xz
     */
    if (repackage_source(
            extracted_path,
            archive_path
        ) != 0) {
        fprintf(
            stderr,
            "ark: failed to create %s\n",
            archive_path
        );

        unlink(archive_path);
        remove_directory(temporary_root);
        return -1;
    }

    /*
     * The canonical archive now exists.
     *
     * Remove all temporary state.
     */
    remove_directory(temporary_root);

    printf(
        "    cached %s/%s/sources.tar.xz\n",
        package->name,
        package->version
    );

    return 0;
}


/* ------------------------------------------------------------------------- */
/* Process repository                                                        */
/* ------------------------------------------------------------------------- */

static int
process_repository(
    struct ark_repo *repo,
    const char *sources_root,
    const char *cache_root
)
{
    size_t i;
    size_t unfetched;

    printf(
        "==> Repository: %s\n",
        repo->name
    );

    if (find_repo_packages(repo) != 0) {
        fprintf(
            stderr,
            "ark: failed to scan repository: %s\n",
            repo->name
        );
        return -1;
    }

    printf(
        "    found %zu package(s)\n",
        repo->packages.count
    );

    /*
     * Check the canonical source store.
     */
    check_repo_packages(
        repo,
        sources_root
    );

    unfetched = 0;

    for (i = 0; i < repo->packages.count; ++i) {
        struct ark_package *package;

        package = &repo->packages.items[i];

        if (package->fetched) {
            printf(
                "    exists: %s/%s\n",
                package->name,
                package->version
            );
        } else {
            printf(
                "    missing: %s/%s\n",
                package->name,
                package->version
            );

            unfetched++;
        }
    }

    /*
     * Write the repository-specific unfetched list BEFORE fetching.
     */
    if (write_unfetched_cache(
            repo,
            cache_root
        ) != 0) {
        fprintf(
            stderr,
            "ark: cannot write unfetched cache for %s\n",
            repo->name
        );
        return -1;
    }

    if (unfetched == 0) {
        printf(
            "    all packages already fetched\n"
        );
        return 0;
    }

    /*
     * Fetch only this repository's missing packages.
     */
    for (i = 0; i < repo->packages.count; ++i) {
        struct ark_package *package;

        package = &repo->packages.items[i];

        if (package->fetched)
            continue;

        if (fetch_package(
                package,
                sources_root
            ) != 0)
            return -1;

        package->fetched = 1;
    }

    /*
     * Rewrite the cache after fetching.
     *
     * Normally this produces an empty file when everything succeeded.
     */
    if (write_unfetched_cache(
            repo,
            cache_root
        ) != 0) {
        fprintf(
            stderr,
            "ark: cannot update unfetched cache for %s\n",
            repo->name
        );
        return -1;
    }

    return 0;
}


/* ------------------------------------------------------------------------- */
/* Command                                                                   */
/* ------------------------------------------------------------------------- */

int
ark_command_fetch(int argc, char **argv)
{
    const char *home;
    char recipes_root[ARK_PATH_MAX];
    char sources_root[ARK_PATH_MAX];
    char cache_root[ARK_PATH_MAX];
    struct ark_repo_list repos;
    size_t i;

    (void)argc;
    (void)argv;

    home = getenv("HOME");

    if (home == NULL) {
        fprintf(stderr, "ark: HOME is not set\n");
        return 1;
    }

    if (snprintf(
            recipes_root,
            sizeof(recipes_root),
            "%s/.ark/recipes",
            home
        ) >= (int)sizeof(recipes_root)) {
        fprintf(stderr, "ark: recipe path too long\n");
        return 1;
    }

    if (snprintf(
            sources_root,
            sizeof(sources_root),
            "%s/.ark/sources",
            home
        ) >= (int)sizeof(sources_root)) {
        fprintf(stderr, "ark: source path too long\n");
        return 1;
    }

    if (snprintf(
            cache_root,
            sizeof(cache_root),
            "%s/.ark/cache",
            home
        ) >= (int)sizeof(cache_root)) {
        fprintf(stderr, "ark: cache path too long\n");
        return 1;
    }

    repo_list_init(&repos);

    if (find_repositories(
            recipes_root,
            &repos
        ) != 0) {
        fprintf(
            stderr,
            "ark: failed to find repositories\n"
        );

        repo_list_free(&repos);
        return 1;
    }

    puts("==> Fetching sources");

    /*
     * Repositories are deliberately processed sequentially.
     *
     * Repository A:
     *     discover -> check -> cache -> fetch
     *
     * then Repository B:
     *     discover -> check -> cache -> fetch
     */
    for (i = 0; i < repos.count; ++i) {
        if (process_repository(
                &repos.items[i],
                sources_root,
                cache_root
            ) != 0) {
            repo_list_free(&repos);
            return 1;
        }
    }

    repo_list_free(&repos);

    puts("==> Sources ready");

    return 0;
}


ARK_COMMAND(
    "fetch",
    "ark fetch",
    "Fetch sources for all recipes",
    ark_command_fetch
);
