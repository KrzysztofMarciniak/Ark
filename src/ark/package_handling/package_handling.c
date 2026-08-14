#define _POSIX_C_SOURCE 200809L

#include "package_handling.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>


/* ------------------------------------------------------------------------- */
/* Filesystem                                                                */
/* ------------------------------------------------------------------------- */

int
ark_is_directory(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}


int
ark_is_regular_file(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}


/* ------------------------------------------------------------------------- */
/* Package specification                                                     */
/* ------------------------------------------------------------------------- */

static int
parse_package_spec(
    const char *spec,
    char *name,
    size_t name_size,
    char *version,
    size_t version_size
)
{
    const char *separator;
    size_t name_length;
    size_t version_length;

    separator = strchr(spec, '@');

    if (separator == NULL) {
        name_length = strlen(spec);

        if (name_length == 0 ||
            name_length >= name_size)
            return -1;

        memcpy(name, spec, name_length + 1);

        version[0] = '\0';

        return 0;
    }

    name_length = (size_t)(separator - spec);
    version_length = strlen(separator + 1);

    if (name_length == 0 ||
        name_length >= name_size ||
        version_length == 0 ||
        version_length >= version_size)
        return -1;

    memcpy(name, spec, name_length);
    name[name_length] = '\0';

    memcpy(
        version,
        separator + 1,
        version_length + 1
    );

    return 0;
}


/* ------------------------------------------------------------------------- */
/* Recipe discovery                                                          */
/* ------------------------------------------------------------------------- */

int
ark_find_package(
    const char *recipes_root,
    const char *package_spec,
    struct ark_package *package
)
{
    DIR *repos;
    struct dirent *repo_entry;
    char package_name[ARK_PACKAGE_NAME_MAX];
    char requested_version[ARK_PACKAGE_VERSION_MAX];
    char package_root[ARK_PATH_MAX];

    if (parse_package_spec(
            package_spec,
            package_name,
            sizeof(package_name),
            requested_version,
            sizeof(requested_version)
        ) != 0)
        return -1;

    repos = opendir(recipes_root);

    if (repos == NULL)
        return -1;

    while ((repo_entry = readdir(repos)) != NULL) {
        DIR *versions;
        struct dirent *version_entry;

        if (strcmp(repo_entry->d_name, ".") == 0 ||
            strcmp(repo_entry->d_name, "..") == 0)
            continue;

        if (repo_entry->d_name[0] == '.')
            continue;

        if (snprintf(
                package_root,
                sizeof(package_root),
                "%s/%s/%s",
                recipes_root,
                repo_entry->d_name,
                package_name
            ) >= (int)sizeof(package_root))
            continue;

        if (!ark_is_directory(package_root))
            continue;

        versions = opendir(package_root);

        if (versions == NULL)
            continue;

        while ((version_entry = readdir(versions)) != NULL) {
            char recipe_path[ARK_PATH_MAX];

            if (strcmp(version_entry->d_name, ".") == 0 ||
                strcmp(version_entry->d_name, "..") == 0)
                continue;

            if (version_entry->d_name[0] == '.')
                continue;

            /*
             * If a version was explicitly requested, skip
             * every other version.
             */
            if (requested_version[0] != '\0' &&
                strcmp(
                    version_entry->d_name,
                    requested_version
                ) != 0)
                continue;

            if (snprintf(
                    recipe_path,
                    sizeof(recipe_path),
                    "%s/%s/recipe.sh",
                    package_root,
                    version_entry->d_name
                ) >= (int)sizeof(recipe_path))
                continue;

            if (!ark_is_regular_file(recipe_path))
                continue;

            if (strlen(package_name) >= sizeof(package->name) ||
                strlen(version_entry->d_name) >= sizeof(package->version) ||
                strlen(recipe_path) >= sizeof(package->recipe_path)) {
                closedir(versions);
                closedir(repos);
                return -1;
            }

            memset(package, 0, sizeof(*package));

            strcpy(package->name, package_name);
            strcpy(package->version, version_entry->d_name);
            strcpy(package->recipe_path, recipe_path);

            closedir(versions);
            closedir(repos);

            return 0;
        }

        closedir(versions);
    }

    closedir(repos);

    return -1;
}
