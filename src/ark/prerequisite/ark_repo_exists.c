#include "ark_repo_exists.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ARK_PATH_MAX 4096

static const char* recipes_dir = ".ark/recipes";

static bool is_directory(const char* path) {
        struct stat st;

        return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool is_regular_file(const char* path) {
        struct stat st;

        return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool has_repo_marker(const char* path) {
        char marker_path[ARK_PATH_MAX];

        if (snprintf(marker_path, sizeof(marker_path), "%s/.repo-root", path) >=
            (int)sizeof(marker_path))
                return false;

        return is_regular_file(marker_path);
}

bool ark_check_repo(void) {
        const char* home;
        char recipes_path[ARK_PATH_MAX];
        char entry_path[ARK_PATH_MAX];
        DIR* dir;
        struct dirent* entry;

        home = getenv("HOME");

        if (home == NULL ||
            snprintf(recipes_path, sizeof(recipes_path), "%s/%s", home,
                     recipes_dir) >= (int)sizeof(recipes_path))
                return false;

        if (has_repo_marker(recipes_path)) return true;

        dir = opendir(recipes_path);

        if (dir == NULL) return false;

        while ((entry = readdir(dir)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 ||
                    strcmp(entry->d_name, "..") == 0 || entry->d_name[0] == '.')
                        continue;

                if (snprintf(entry_path, sizeof(entry_path), "%s/%s",
                             recipes_path,
                             entry->d_name) >= (int)sizeof(entry_path))
                        continue;

                if (is_directory(entry_path) && has_repo_marker(entry_path)) {
                        closedir(dir);
                        return true;
                }
        }

        closedir(dir);
        return false;
}
