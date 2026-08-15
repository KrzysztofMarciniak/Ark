#define _POSIX_C_SOURCE 200809L

#include "package_handling.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


/* ------------------------------------------------------------------------- */
/* Filesystem helpers                                                        */
/* ------------------------------------------------------------------------- */

int
ark_is_regular_file(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return 0;

    return S_ISREG(st.st_mode);
}


static int
is_directory(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return 0;

    return S_ISDIR(st.st_mode);
}


/* ------------------------------------------------------------------------- */
/* Version comparison                                                        */
/* ------------------------------------------------------------------------- */

/*
 * Compares two dot-separated, purely numeric version strings, e.g.
 * "4.4.1" vs "4.10.0". Returns <0, 0, or >0 like strcmp.
 *
 * Non-numeric components fall back to a plain string compare of
 * that component so odd version schemes don't crash, they just
 * don't sort "correctly".
 */
static int
version_compare(const char *a, const char *b)
{
    while (*a != '\0' || *b != '\0') {
        char *a_end;
        char *b_end;
        long a_num;
        long b_num;

        a_num = strtol(a, &a_end, 10);
        b_num = strtol(b, &b_end, 10);

        if (a_end != a && b_end != b) {
            if (a_num != b_num)
                return a_num < b_num ? -1 : 1;

            a = a_end;
            b = b_end;
        } else {
            /* Not a clean numeric component on one/both sides. */
            size_t a_len = strcspn(a, ".");
            size_t b_len = strcspn(b, ".");
            int cmp = strncmp(a, b, a_len < b_len ? a_len : b_len);

            if (cmp != 0)
                return cmp;

            if (a_len != b_len)
                return a_len < b_len ? -1 : 1;

            a += a_len;
            b += b_len;
        }

        if (*a == '.')
            a++;

        if (*b == '.')
            b++;
    }

    return 0;
}


/* ------------------------------------------------------------------------- */
/* Version directory selection                                               */
/* ------------------------------------------------------------------------- */

static int
find_highest_version(
    const char *package_dir,
    char *version_out,
    size_t version_out_size
)
{
    DIR *dir;
    struct dirent *entry;
    int found;

    dir = opendir(package_dir);

    if (dir == NULL)
        return -1;

    found = 0;

    while ((entry = readdir(dir)) != NULL) {
        char candidate_path[ARK_PATH_MAX];
        char recipe_path[ARK_PATH_MAX];

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        if (snprintf(
                candidate_path,
                sizeof(candidate_path),
                "%s/%s",
                package_dir,
                entry->d_name
            ) >= (int)sizeof(candidate_path))
            continue;

        if (!is_directory(candidate_path))
            continue;

        if (snprintf(
                recipe_path,
                sizeof(recipe_path),
                "%s/recipe.sh",
                candidate_path
            ) >= (int)sizeof(recipe_path))
            continue;

        if (!ark_is_regular_file(recipe_path))
            continue;

        if (!found ||
            version_compare(entry->d_name, version_out) > 0) {
            if (strlen(entry->d_name) >= version_out_size)
                continue;

            strcpy(version_out, entry->d_name);
            found = 1;
        }
    }

    closedir(dir);

    return found ? 0 : -1;
}


/* ------------------------------------------------------------------------- */
/* ARK_DEPENDS parsing                                                       */
/* ------------------------------------------------------------------------- */

/*
 * Sources recipe_path in a subshell and prints $ARK_DEPENDS, so we
 * pick up the real shell-expanded value rather than trying to parse
 * the assignment as text. recipe.sh is already trusted: install.c
 * and remove.c both source it directly to run build()/remove().
 */
static int
read_depends(
    const char *recipe_path,
    char *buffer,
    size_t buffer_size
)
{
    char command[ARK_PATH_MAX + 64];
    FILE *pipe;
    size_t length;

    if (snprintf(
            command,
            sizeof(command),
            ". \"%s\" >/dev/null 2>&1; printf '%%s' \"$ARK_DEPENDS\"",
            recipe_path
        ) >= (int)sizeof(command))
        return -1;

    pipe = popen(command, "r");

    if (pipe == NULL)
        return -1;

    length = fread(buffer, 1, buffer_size - 1, pipe);
    buffer[length] = '\0';

    pclose(pipe);

    return 0;
}


static void
split_depends(struct ark_package *package)
{
    char *token;
    char *saveptr;
    size_t i;

    i = 0;
    saveptr = NULL;

    token = strtok_r(package->depends_storage, " \t\n", &saveptr);

    while (token != NULL && i < ARK_MAX_DEPENDS) {
        package->depends[i++] = token;
        token = strtok_r(NULL, " \t\n", &saveptr);
    }

    package->depends[i] = NULL;
}


/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

/*
 * recipes_root contains one directory per repo, e.g.:
 *
 *   <recipes_root>/<repo>/<name>/<version>/recipe.sh
 *
 * ark_find_package searches every repo directory under recipes_root
 * for a matching package name, and across all matches picks the
 * highest version (see version_compare()). If more than one repo
 * ties on version for the same package, the repo encountered first
 * (readdir order) wins.
 */
int
ark_find_package(
    const char *recipes_root,
    const char *name,
    struct ark_package *package
)
{
    DIR *dir;
    struct dirent *entry;
    int found;
    char best_recipe_path[ARK_PATH_MAX];
    char best_version[ARK_VERSION_MAX];

    if (strlen(name) >= sizeof(package->name)) {
        fprintf(
            stderr,
            "ark: package name too long: %s\n",
            name
        );

        return -1;
    }

    dir = opendir(recipes_root);

    if (dir == NULL) {
        fprintf(
            stderr,
            "ark: cannot open recipes root: %s\n",
            recipes_root
        );

        return -1;
    }

    found = 0;
    best_version[0] = '\0';
    best_recipe_path[0] = '\0';

    while ((entry = readdir(dir)) != NULL) {
        char repo_path[ARK_PATH_MAX];
        char package_dir[ARK_PATH_MAX];
        char version[ARK_VERSION_MAX];

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        if (snprintf(
                repo_path,
                sizeof(repo_path),
                "%s/%s",
                recipes_root,
                entry->d_name
            ) >= (int)sizeof(repo_path))
            continue;

        if (!is_directory(repo_path))
            continue;

        if (snprintf(
                package_dir,
                sizeof(package_dir),
                "%s/%s",
                repo_path,
                name
            ) >= (int)sizeof(package_dir))
            continue;

        if (!is_directory(package_dir))
            continue;

        version[0] = '\0';

        if (find_highest_version(
                package_dir,
                version,
                sizeof(version)
            ) != 0)
            continue;

        if (!found || version_compare(version, best_version) > 0) {
            if (snprintf(
                    best_recipe_path,
                    sizeof(best_recipe_path),
                    "%s/%s/recipe.sh",
                    package_dir,
                    version
                ) >= (int)sizeof(best_recipe_path))
                continue;

            strcpy(best_version, version);
            found = 1;
        }
    }

    closedir(dir);

    if (!found)
        return -1;

    memset(package, 0, sizeof(*package));

    strcpy(package->name, name);
    strcpy(package->version, best_version);
    strcpy(package->recipe_path, best_recipe_path);

    if (read_depends(
            package->recipe_path,
            package->depends_storage,
            sizeof(package->depends_storage)
        ) != 0) {
        fprintf(
            stderr,
            "ark: failed to read ARK_DEPENDS for %s\n",
            name
        );

        return -1;
    }

    split_depends(package);

    return 0;
}
