#ifndef ARK_SOURCE_H
#define ARK_SOURCE_H

enum ark_archive_type {
    ARK_ARCHIVE_XZ,
    ARK_ARCHIVE_GZ,
    ARK_ARCHIVE_BZ2
};

int
ark_archive_type_from_path(
    const char *path,
    enum ark_archive_type *type
);

int
ark_source_extract(
    const char *archive,
    const char *destination
);

#endif
