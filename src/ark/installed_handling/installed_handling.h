#ifndef ARK_INSTALLED_HANDLING_H
#define ARK_INSTALLED_HANDLING_H

#include <stddef.h>

#include "../package_handling/package_handling.h"

/*
 * Tracks what's currently installed under:
 *
 *   <installed_root>/<name>/version    - the installed version string
 *   <installed_root>/<name>/depends    - space-separated ARK_DEPENDS snapshot
 *   <installed_root>/<name>/explicit   - present (empty) iff the user asked
 *                                         for this package by name; absent
 *                                         means it was pulled in only as a
 *                                         dependency of something else.
 *
 * <installed_root> is normally ~/.ark/cache/installed.
 *
 * This is a record of install-time state, separate from the recipe
 * repos: recipes can change or disappear (a repo gets removed, a
 * package gets renamed) without corrupting what we know is actually
 * on disk in ~/.ark/bin.
 */

/*
 * Records that `package` is installed. If explicit is non-zero, the
 * package is marked as explicitly requested. If explicit is zero
 * but the package already has an explicit marker on disk, the
 * existing explicit marker is preserved (installing something as a
 * side-effect dependency never demotes an existing explicit install).
 */
int ark_installed_record(const char* installed_root,
                         const struct ark_package* package, int explicit);

/* Marks an already-recorded package as explicit. */
int ark_installed_mark_explicit(const char* installed_root, const char* name);

/* Removes the manifest entry for `name`. Missing entries are not an error. */
int ark_installed_forget(const char* installed_root, const char* name);

/* Non-zero if `name` has an install record at all. */
int ark_installed_exists(const char* installed_root, const char* name);

/* Non-zero if `name` is recorded as explicitly installed. */
int ark_installed_is_explicit(const char* installed_root, const char* name);

/*
 * Fills depends_out (NULL-terminated, backed by depends_storage) with
 * the dependency names recorded for `name` at install time. Returns
 * 0 on success, -1 if there's no record for `name`.
 */
int ark_installed_get_depends(const char* installed_root, const char* name,
                              char* depends_storage,
                              size_t depends_storage_size, char** depends_out,
                              size_t depends_out_capacity);

/*
 * Lists every recorded package name under installed_root. *names_out
 * is a malloc'd array of malloc'd strings, NULL-terminated; free
 * with ark_installed_free_list. Returns 0 on success (including an
 * empty list), -1 on error.
 */
int ark_installed_list(const char* installed_root, char*** names_out);

void ark_installed_free_list(char** names);

#endif
