#!/bin/sh

set -e

CC="${CC:-cc}"
CFLAGS="${CFLAGS:--std=c99 -Wall -Wextra -pedantic}"

ROOT=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
BUILD="$ROOT/build"

mkdir -p "$BUILD"

echo "==> Building ark"

"$CC" $CFLAGS \
    "$ROOT/src/hashmap/hashmap.c" \
    "$ROOT/src/command_logic/command_logic.c" \
    "$ROOT/src/ark/commands/version/version.c" \
    "$ROOT/src/ark/main.c" \
    -o "$BUILD/ark"

echo "==> Building ark-build"

"$CC" $CFLAGS \
    "$ROOT/src/hashmap/hashmap.c" \
    "$ROOT/src/ark-build/main.c" \
    -o "$BUILD/ark-build"

echo "==> Build complete"
