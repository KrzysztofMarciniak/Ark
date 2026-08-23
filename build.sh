#!/bin/sh
set -e
CC="${CC:-cc}"
CFLAGS="${CFLAGS:--std=c99 -Wall -Wextra -pedantic}"
ROOT=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
BUILD="$ROOT/build"
PREFIX="${PREFIX:-/usr/local}"
BINDIR="$PREFIX/bin"

build() {
    mkdir -p "$BUILD"
    echo "==> Building ark"
    "$CC" $CFLAGS \
        -I"$ROOT/src" \
        "$ROOT/src/command_logic/command_logic.c" \
        "$ROOT/src/ark/prerequisite/ark_directories_exist.c" \
        "$ROOT/src/ark/prerequisite/programs_required.c" \
        "$ROOT/src/ark/prerequisite/ark_repo_exists.c" \
        "$ROOT/src/ark/package_handling/package_handling.c" \
        "$ROOT/src/ark/dependency_handling/dependency_handling.c" \
        "$ROOT/src/ark/installed_handling/installed_handling.c" \
        "$ROOT/src/ark/commands/version/version.c" \
        "$ROOT/src/ark/commands/fetch/fetch.c" \
        "$ROOT/src/ark/commands/install/install.c" \
        "$ROOT/src/ark/commands/remove/remove.c" \
        "$ROOT/src/ark/commands/autoremove/autoremove.c" \
        "$ROOT/src/ark/main.c" \
        -o "$BUILD/ark"
    echo "==> Build complete"
}

install_ark() {
    if [ ! -x "$BUILD/ark" ]; then
        build
    fi
    echo "==> Installing ark to $BINDIR (requires root via su)"
    su -c "mkdir -p '$BINDIR' && cp '$BUILD/ark' '$BINDIR/ark' && chmod 755 '$BINDIR/ark'"
    echo "==> Installed to $BINDIR/ark"
}

case "${1:-build}" in
    build)
        build
        ;;
    install)
        install_ark
        ;;
    *)
        echo "Usage: $0 [build|install]" >&2
        exit 1
        ;;
esac
