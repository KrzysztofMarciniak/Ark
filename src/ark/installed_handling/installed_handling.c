#define _POSIX_C_SOURCE 200809L

#include "installed_handling.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* Path helpers                                                              */
/* ------------------------------------------------------------------------- */

static int mkdir_parents(const char* path) {
        char buffer[ARK_PATH_MAX];
        char* p;

        if (snprintf(buffer, sizeof(buffer), "%s", path) >= (int)sizeof(buffer))
                return -1;

        for (p = buffer + 1; *p != '\0'; p++) {
                if (*p == '/') {
                        *p = '\0';

                        if (mkdir(buffer, 0700) != 0 && errno != EEXIST)
                                return -1;

                        *p = '/';
                }
        }

        if (mkdir(buffer, 0700) != 0 && errno != EEXIST) return -1;

        return 0;
}

static int is_directory(const char* path) {
        struct stat st;

        if (stat(path, &st) != 0) return 0;

        return S_ISDIR(st.st_mode);
}

static int package_dir_path(const char* installed_root, const char* name,
                            char* out, size_t out_size) {
        if (snprintf(out, out_size, "%s/%s", installed_root, name) >=
            (int)out_size)
                return -1;

        return 0;
}

static int field_path(const char* package_dir, const char* field, char* out,
                      size_t out_size) {
        if (snprintf(out, out_size, "%s/%s", package_dir, field) >=
            (int)out_size)
                return -1;

        return 0;
}

static int write_field(const char* path, const char* content) {
        FILE* file;

        file = fopen(path, "w");

        if (file == NULL) return -1;

        if (content != NULL) fputs(content, file);

        fclose(file);

        return 0;
}

static int read_field(const char* path, char* buffer, size_t buffer_size) {
        FILE* file;
        size_t length;

        file = fopen(path, "r");

        if (file == NULL) return -1;

        length         = fread(buffer, 1, buffer_size - 1, file);
        buffer[length] = '\0';

        /* Trim a single trailing newline, if any. */
        if (length > 0 && buffer[length - 1] == '\n') buffer[length - 1] = '\0';

        fclose(file);

        return 0;
}

static int file_exists(const char* path) {
        struct stat st;

        return stat(path, &st) == 0;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

int ark_installed_exists(const char* installed_root, const char* name) {
        char package_dir[ARK_PATH_MAX];

        if (package_dir_path(installed_root, name, package_dir,
                             sizeof(package_dir)) != 0)
                return 0;

        return is_directory(package_dir);
}

int ark_installed_is_explicit(const char* installed_root, const char* name) {
        char package_dir[ARK_PATH_MAX];
        char explicit_path[ARK_PATH_MAX];

        if (package_dir_path(installed_root, name, package_dir,
                             sizeof(package_dir)) != 0)
                return 0;

        if (field_path(package_dir, "explicit", explicit_path,
                       sizeof(explicit_path)) != 0)
                return 0;

        return file_exists(explicit_path);
}

int ark_installed_record(const char* installed_root,
                         const struct ark_package* package, int explicit) {
        char package_dir[ARK_PATH_MAX];
        char version_path[ARK_PATH_MAX];
        char depends_path[ARK_PATH_MAX];
        char explicit_path[ARK_PATH_MAX];
        char depends_line[ARK_PATH_MAX];
        int i;
        size_t offset;
        int already_explicit;

        if (package_dir_path(installed_root, package->name, package_dir,
                             sizeof(package_dir)) != 0)
                return -1;

        already_explicit =
            ark_installed_is_explicit(installed_root, package->name);

        if (mkdir_parents(installed_root) != 0) return -1;

        if (mkdir(package_dir, 0700) != 0 && errno != EEXIST) return -1;

        if (field_path(package_dir, "version", version_path,
                       sizeof(version_path)) != 0)
                return -1;

        if (write_field(version_path, package->version) != 0) return -1;

        depends_line[0] = '\0';
        offset          = 0;

        for (i = 0; package->depends[i] != NULL; i++) {
                int written;

                written = snprintf(depends_line + offset,
                                   sizeof(depends_line) - offset, "%s%s",
                                   offset > 0 ? " " : "", package->depends[i]);

                if (written < 0 ||
                    (size_t)written >= sizeof(depends_line) - offset)
                        break;

                offset += (size_t)written;
        }

        if (field_path(package_dir, "depends", depends_path,
                       sizeof(depends_path)) != 0)
                return -1;

        if (write_field(depends_path, depends_line) != 0) return -1;

        if (field_path(package_dir, "explicit", explicit_path,
                       sizeof(explicit_path)) != 0)
                return -1;

        if (explicit || already_explicit) {
                if (write_field(explicit_path, "") != 0) return -1;
        }

        return 0;
}

int ark_installed_mark_explicit(const char* installed_root, const char* name) {
        char package_dir[ARK_PATH_MAX];
        char explicit_path[ARK_PATH_MAX];

        if (!ark_installed_exists(installed_root, name)) return -1;

        if (package_dir_path(installed_root, name, package_dir,
                             sizeof(package_dir)) != 0)
                return -1;

        if (field_path(package_dir, "explicit", explicit_path,
                       sizeof(explicit_path)) != 0)
                return -1;

        return write_field(explicit_path, "");
}

int ark_installed_forget(const char* installed_root, const char* name) {
        char package_dir[ARK_PATH_MAX];
        char version_path[ARK_PATH_MAX];
        char depends_path[ARK_PATH_MAX];
        char explicit_path[ARK_PATH_MAX];

        if (package_dir_path(installed_root, name, package_dir,
                             sizeof(package_dir)) != 0)
                return -1;

        if (!is_directory(package_dir)) return 0;

        field_path(package_dir, "version", version_path, sizeof(version_path));
        field_path(package_dir, "depends", depends_path, sizeof(depends_path));
        field_path(package_dir, "explicit", explicit_path,
                   sizeof(explicit_path));

        unlink(version_path);
        unlink(depends_path);
        unlink(explicit_path);

        if (rmdir(package_dir) != 0 && errno != ENOTEMPTY) return -1;

        return 0;
}

int ark_installed_get_depends(const char* installed_root, const char* name,
                              char* depends_storage,
                              size_t depends_storage_size, char** depends_out,
                              size_t depends_out_capacity) {
        char package_dir[ARK_PATH_MAX];
        char depends_path[ARK_PATH_MAX];
        char* token;
        char* saveptr;
        size_t count;

        if (package_dir_path(installed_root, name, package_dir,
                             sizeof(package_dir)) != 0)
                return -1;

        if (!is_directory(package_dir)) return -1;

        if (field_path(package_dir, "depends", depends_path,
                       sizeof(depends_path)) != 0)
                return -1;

        if (read_field(depends_path, depends_storage, depends_storage_size) !=
            0) {
                depends_storage[0] = '\0';
        }

        count   = 0;
        saveptr = NULL;
        token   = strtok_r(depends_storage, " \t\n", &saveptr);

        while (token != NULL && count + 1 < depends_out_capacity) {
                depends_out[count++] = token;
                token                = strtok_r(NULL, " \t\n", &saveptr);
        }

        depends_out[count] = NULL;

        return 0;
}

int ark_installed_list(const char* installed_root, char*** names_out) {
        DIR* dir;
        struct dirent* entry;
        char** names;
        size_t count;
        size_t capacity;

        *names_out = NULL;

        dir = opendir(installed_root);

        if (dir == NULL) {
                /* No installed_root yet means nothing is installed. */
                names = malloc(sizeof(*names));

                if (names == NULL) return -1;

                names[0]   = NULL;
                *names_out = names;

                return 0;
        }

        capacity = 16;
        count    = 0;
        names    = malloc(capacity * sizeof(*names));

        if (names == NULL) {
                closedir(dir);
                return -1;
        }

        while ((entry = readdir(dir)) != NULL) {
                char package_dir[ARK_PATH_MAX];

                if (strcmp(entry->d_name, ".") == 0 ||
                    strcmp(entry->d_name, "..") == 0)
                        continue;

                if (package_dir_path(installed_root, entry->d_name, package_dir,
                                     sizeof(package_dir)) != 0)
                        continue;

                if (!is_directory(package_dir)) continue;

                if (count + 1 >= capacity) {
                        char** grown;

                        capacity *= 2;
                        grown = realloc(names, capacity * sizeof(*names));

                        if (grown == NULL) {
                                closedir(dir);
                                ark_installed_free_list(names);
                                return -1;
                        }

                        names = grown;
                }

                names[count] = strdup(entry->d_name);

                if (names[count] == NULL) {
                        closedir(dir);
                        ark_installed_free_list(names);
                        return -1;
                }

                count++;
        }

        names[count] = NULL;

        closedir(dir);

        *names_out = names;

        return 0;
}

void ark_installed_free_list(char** names) {
        size_t i;

        if (names == NULL) return;

        for (i = 0; names[i] != NULL; i++) free(names[i]);

        free(names);
}
