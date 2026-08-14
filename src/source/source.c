#include "source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
has_suffix(const char *path, const char *suffix)
{
    size_t path_len;
    size_t suffix_len;

    path_len = strlen(path);
    suffix_len = strlen(suffix);

    if (path_len < suffix_len)
        return 0;

    return strcmp(
        path + path_len - suffix_len,
        suffix
    ) == 0;
}

int
ark_archive_type_from_path(
    const char *path,
    enum ark_archive_type *type
)
{
    if (has_suffix(path, ".tar.xz")) {
        *type = ARK_ARCHIVE_XZ;
        return 0;
    }

    if (has_suffix(path, ".tar.gz") ||
        has_suffix(path, ".tgz")) {
        *type = ARK_ARCHIVE_GZ;
        return 0;
    }

    if (has_suffix(path, ".tar.bz2") ||
        has_suffix(path, ".tbz2")) {
        *type = ARK_ARCHIVE_BZ2;
        return 0;
    }

    fprintf(
        stderr,
        "ark: unsupported source archive format: %s\n",
        path
    );

    return -1;
}

int
ark_source_extract(
    const char *archive,
    const char *destination
)
{
    enum ark_archive_type type;
    const char *flag;
    char command[4096];
    int result;

    if (ark_archive_type_from_path(archive, &type) != 0)
        return -1;

    switch (type) {
    case ARK_ARCHIVE_XZ:
        flag = "xJf";
        break;

    case ARK_ARCHIVE_GZ:
        flag = "xzf";
        break;

    case ARK_ARCHIVE_BZ2:
        flag = "xjf";
        break;

    default:
        return -1;
    }

    if (snprintf(
            command,
            sizeof(command),
            "mkdir -p '%s' && tar -%s '%s' -C '%s'",
            destination,
            flag,
            archive,
            destination
        ) >= (int)sizeof(command)) {
        fprintf(stderr, "ark: extraction command too long\n");
        return -1;
    }

    result = system(command);

    if (result != 0) {
        fprintf(
            stderr,
            "ark: failed to extract %s\n",
            archive
        );

        return -1;
    }

    return 0;
}
