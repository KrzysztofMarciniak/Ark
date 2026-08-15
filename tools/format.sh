#!/bin/sh

set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

find "$ROOT/src" -type f \( \
    -name '*.c' -o \
    -name '*.h' -o \
    -name '*.cc' -o \
    -name '*.cpp' -o \
    -name '*.cxx' \
\) -exec clang-format -i {} +

printf '%s\n' "==> Formatted source files"
