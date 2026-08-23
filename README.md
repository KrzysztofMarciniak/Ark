# Ark

Ark is a small package manager written in C for installing software into
your own home directory. No root, no system package database, nothing
outside `~/.ark`. It's being built as the package manager for
[Simple-Linux](https://github.com/KrzysztofMarciniak/Simple-Linux).

The name comes from Noah's Ark.

## Table of contents

- [TL;DR](#tldr)
- [0. What it is, why it exists](#0-what-it-is-why-it-exists)
- [1. How to install Ark](#1-how-to-install-ark)
  - [Requirements](#requirements)
  - [Build](#build)
  - [Install the binary (optional)](#install-the-binary-optional)
- [2. How to use Ark](#2-how-to-use-ark)
  - [Prerequisite programs](#prerequisite-programs)
  - [First-time setup](#first-time-setup)
  - [Commands](#commands)
  - [Recipe lookup](#recipe-lookup)
- [3. How it was made](#3-how-it-was-made)
- [4. Contributing](#4-contributing)
  - [To Ark itself](#to-ark-itself)
  - [Recipes (ark-recipes)](#recipes-ark-recipes)

## TL;DR

```bash
# Build and install Ark
git clone git@github.com:KrzysztofMarciniak/Ark.git
cd Ark
chmod +x ./build.sh
./build.sh install

# Set up the package repository
mkdir -p ~/.ark/recipes
git clone https://github.com/KrzysztofMarciniak/ark-test-repository ~/.ark/recipes/

# Fetch package sources
ark fetch

# Add Ark's user-installed binaries to PATH
mkdir -p ~/.ark/bin
export PATH="$HOME/.ark/bin:$PATH"

# Install a package
ark install ark-hello-world

# Run the installed package
ark-hello-world

# Remove a package
ark remove ark-hello-world

# Remove orphaned packages
ark autoremove
```

## 0. What it is, why it exists

Everything Ark installs lives under `~/.ark/bin`, plus whatever else a
recipe's `build()` writes into `~/.ark/...`. Nothing goes outside the
user's home directory, and nothing needs root. The only place `root`
shows up at all is the optional `build.sh install` step, which installs
the `ark` binary itself into a system prefix. Installing/removing
packages never touches that.

Two reasons for this design:

- **Portability.** `~/.ark` is one directory tree. Copy it, tar it up,
  rsync it to another box, and your installed software goes with it.
- **No privilege needed to manage software.** No shared system state to
  break, no sudo prompts for `ark install`.

### Offline-first

Ark splits "get the source" from "build and install it":

1. Setup, needs network: clone a recipes repo into `~/.ark/recipes/`, run
   `ark fetch` once. Downloads every recipe's source, checks its checksum,
   caches it under `~/.ark/sources/`.
2. Work, no network needed: `ark install`, `ark remove`, `ark autoremove`
   only touch `~/.ark/recipes`, `~/.ark/sources`, `~/.ark/cache`.

apt/pacman/yum hit the network on every install. Ark does its networking
up front in `fetch`, then everything else works offline. Useful for
air-gapped machines, bad connectivity, or just wanting deterministic
builds.

You don't even need `ark fetch` if you populate `~/.ark/recipes/` and
`~/.ark/sources/` yourself, say by copying them from another machine.

## 1. How to install Ark

### Requirements

- A C compiler (`cc` by default, override with `CC`).
- A POSIX system. Ark uses `fork`/`execvp`/`waitpid`, `dirent.h`,
  `sys/stat.h`, and defines `_POSIX_C_SOURCE 200809L` in several files
  (see `install.c`, `fetch.c`, `remove.c`) to get there. It's not
  portable C99, it's POSIX C.
- `su`, only needed for `./build.sh install`.

### Build

```bash
./build.sh          # same as `./build.sh build`
```

Compiles every `.c` file listed in `build.sh` into one binary at
`build/ark`. No object files, no separate link step, just one `cc`
invocation with every source file named explicitly.

### Install the binary (optional)

```bash
./build.sh install
```

Builds `ark` if needed, then uses `su` to copy it to `$PREFIX/bin/ark`
(default `PREFIX=/usr/local`, override with `PREFIX`).

## 2. How to use Ark

### Prerequisite programs

Before doing anything, `ark` checks:

- `~/.ark` exists
- `~/.ark/recipes` exists
- `curl`, `tar` (with XZ support), `sha256sum`, `git`, `rm` are on `PATH`

Missing any of these, it refuses to run and says which one.

### First-time setup

```bash
mkdir -p ~/.ark/recipes
git clone https://github.com/KrzysztofMarciniak/ark-recipes.git \
    ~/.ark/recipes/ark-recipes

ark fetch

export PATH="$HOME/.ark/bin:$PATH"
```

`~/.ark/recipes/` can hold more than one recipe repo. Every directory
directly under it is treated as its own repo:

```
~/.ark/recipes/
├── ark-recipes/
└── some-other-repo/
```

### Commands

#### `ark version`

Prints the version.

#### `ark fetch`

Scans every repo under `~/.ark/recipes/` and, for anything not already
cached, downloads and verifies its source. Per package:

1. `curl -L --fail` the archive to a temp location.
2. Check it against `ARK_SOURCE_SHA256` with `sha256sum -c`.
3. Extract with `tar`, repackage the extracted tree as one canonical
   `sources.tar.xz`.
4. Store it at `~/.ark/sources/<package>/<version>/sources.tar.xz`, drop
   the temp files.

Repos are processed one at a time. Each repo's list of still-missing
packages gets written to `~/.ark/cache/unfetched/<repo>` before and after
fetching, so you can see what's outstanding if `fetch` gets interrupted.

#### `ark install <package> [package...]`

For each package named:

1. Looks up the recipe (see below) and recursively resolves everything in
   its `ARK_DEPENDS`, depth-first, with cycle detection and a 128-level
   recursion cap.
2. Installs dependencies before the package that needs them. Dependencies
   get recorded as implicitly installed.
3. Extracts `sources.tar.xz` into a fresh directory under `~/.ark/tmp`
   (not `/tmp` — `/tmp` is sometimes mounted noexec, and build systems
   routinely execute binaries they've just compiled), sources `recipe.sh`,
   calls `build()`.
4. Whatever you actually named on the command line gets marked explicit
   once installed. Its dependencies don't.

Explicit vs implicit is what `ark autoremove` uses below: pulling
something in as a dependency doesn't count as asking for it directly.

`build()` gets these env vars:

- `ARK_SOURCE_ARCHIVE` — path to `sources.tar.xz`
- `ARK_BUILD_DIR` — temp build directory
- `ARK_SOURCE_DIR` — first directory found inside `ARK_BUILD_DIR` after
  extraction (falls back to `ARK_BUILD_DIR` if there wasn't one); this is
  where `build()` starts
- `ARK_PACKAGE_NAME`, `ARK_PACKAGE_VERSION`

```bash
ark install curl
ark install openssl busybox
```

If the source hasn't been fetched, install fails and tells you to run
`ark fetch`.

#### `ark remove <package>`

Finds the recipe, sources it, calls `remove()`. The recipe is responsible
for deleting whatever it installed. Ark drops the install record after.

```bash
ark remove curl
```

#### `ark autoremove [-y|--yes]`

Removes packages that were only pulled in as dependencies and aren't
needed by anything else installed. Lists them, asks `y/N` unless you pass
`-y`/`--yes`. Runs repeatedly since removing one orphan can orphan its
own dependencies.

```bash
ark autoremove
ark autoremove -y
```

### Recipe lookup

Recipes live at `<recipes_root>/<repo>/<name>/<version>/recipe.sh`. Ark
searches every repo under `~/.ark/recipes/` for a matching name and picks
the highest version across all of them (compared numerically). There's no
way to pin a version on the command line right now, you get whatever's
highest.

## 3. How it was made

```
src/
├── command_logic/            command registry + dispatch, shared by
│                              every subcommand
└── ark/
    ├── main.c                checks prerequisites, dispatches argv
    ├── prerequisite/         startup checks (.ark exists, recipes
    │                          exist, required programs on PATH)
    ├── package_handling/      recipe lookup, ARK_DEPENDS parsing
    ├── dependency_handling/   recursive dependency resolution
    ├── installed_handling/    on-disk install manifest
    └── commands/
        ├── version/
        ├── fetch/
        ├── install/
        ├── remove/
        └── autoremove/
```

### Commands register themselves via a linker section

There's no big switch statement listing commands. Each command file uses
the `ARK_COMMAND(name, usage, description, handler)` macro from
`command_logic.h`, which drops a `struct ark_command_definition` into a
custom linker section called `ark_commands`. At startup,
`ark_command_registry_init()` reads that whole section as an array,
bounded by the linker symbols `__start_ark_commands` and
`__stop_ark_commands`. Adding a command file to `build.sh` is enough for
it to show up. There's also `ARK_SUBCOMMAND(parent, name, ...)` for
nested commands, though nothing uses it yet.

Startup order in `main()`: check prerequisites, build the registry from
the linker section, print help if no command was given, otherwise look up
the command (and a subcommand if the next arg matches one) and call the
handler.

### Recipe lookup and dependency resolution

`package_handling` walks `~/.ark/recipes/<repo>/<name>/<version>/`,
comparing version directory names numerically, and picks the newest one
that has a `recipe.sh`. It reads `ARK_DEPENDS` by sourcing the recipe in
a subshell (`. recipe.sh; printf '%s' "$ARK_DEPENDS"`) instead of
text-parsing the assignment. That's fine because `recipe.sh` is already
trusted: install and remove both source it directly to run
`build()`/`remove()`.

`dependency_handling` walks `ARK_DEPENDS` depth-first, marking each name
seen before recursing into its own dependencies, so a cycle (A needs B,
B needs A) gets caught instead of looping forever. Caps at 128 levels.
Calls back into install once per package, in dependency order.

### Install manifest

`installed_handling` tracks installed state separately from the recipes,
under `~/.ark/cache/installed/<name>/`:

```
version    the installed version string
depends    space-separated ARK_DEPENDS snapshot
explicit   present (empty file) if the user asked for this package by
           name; absent means it's only there as a dependency
```

Keeping this separate from the recipe repos matters: a repo can get
edited or removed without corrupting what Ark knows is actually on disk.
Recording a package as a dependency never demotes an existing explicit
marker.

### fetch's pipeline

`fetch.c` pulls `ARK_SOURCE_URL` and `ARK_SOURCE_SHA256` out of
`recipe.sh` with a small line parser, without sourcing it (only
`ARK_DEPENDS` gets read by sourcing, in `package_handling`). For anything
not cached, it runs `curl` and `sha256sum` via `fork`/`execvp`, never
through a shell, so nothing in a URL or path can be interpreted as shell
syntax. Extracts the verified archive, repacks it as one canonical
`sources.tar.xz` per package/version.

## 4. Contributing

### To Ark itself

Code is C99-ish POSIX, formatted with clang-format (`BasedOnStyle:
Google`, 8-space indent, 80 columns, see `.clang-format`). Run
`tools/format.sh` before committing.

To add a command:

1. Create `src/ark/commands/<name>/<name>.c` and a matching `.h`.
2. Write a handler: `int ark_command_<name>(int argc, char** argv)`,
   return 0 on success, 1 on failure.
3. Register it at the bottom:
   ```c
   ARK_COMMAND("name", "ark name [args]", "Short description",
               ark_command_name);
   ```
4. Add the `.c` file to the file list in `build.sh`'s `build()` function.
   It's not picked up automatically, only the linker-section registry is.
5. `./build.sh`.

Send a pull request. Keep functions small, prefer early returns, stick to
POSIX APIs.

### Recipes (ark-recipes)

Recipes live in the separate
[ark-recipes](https://github.com/KrzysztofMarciniak/ark-recipes) repo:

```
<package>/<version>/recipe.sh
```

Only `ARK_SOURCE_URL`, `ARK_SOURCE_SHA256`, and `ARK_DEPENDS` are actually
read by Ark right now. `ARK_TYPE` and `ARK_BINARIES` are convention for
documenting the package (what kind of build it is, what it provides) and
reserved for later, write them anyway so recipes stay self-documenting.

- `ARK_SOURCE_URL` — archive URL (`.tar.xz`, `.tar.gz`/`.tgz`, or
  `.tar.bz2`/`.tbz2`)
- `ARK_SOURCE_SHA256` — checked by `ark fetch`
- `ARK_TYPE` — free-form, e.g. "source" or "binary"
- `ARK_DEPENDS` — space-separated package names, e.g.
  `ARK_DEPENDS="zlib openssl"`, empty string if none
- `ARK_BINARIES` — free-form list of binaries this provides
- `build()` — called by install, runs inside `$ARK_SOURCE_DIR` with
  `$ARK_BUILD_DIR`, `$ARK_PACKAGE_NAME`, `$ARK_PACKAGE_VERSION` set.
  Install everything under `$HOME/.ark/...`, never system paths.
- `remove()` — called by remove, must clean up everything `build()` put
  down.

Example, a prebuilt clang release:

```bash
ARK_SOURCE_URL="https://github.com/KrzysztofMarciniak/clang-20.1.8-x86_64/raw/refs/heads/master/clang-20.1.8-x86_64.tar.xz"
ARK_SOURCE_SHA256="63e13786aaec6b1fdf3e5b59751ced2599be3cce9c450892a337a83090bb636b"
ARK_TYPE="binary"
ARK_DEPENDS=""
ARK_BINARIES="usr/bin/clang usr/bin/clang++ usr/bin/clang-cpp"

build() {
    mkdir -p "$HOME/.ark/bin"
    mkdir -p "$HOME/.ark/lib"
    cp -a "$ARK_BUILD_DIR/usr/bin/." "$HOME/.ark/bin/"
    cp -a "$ARK_BUILD_DIR/usr/lib/20" "$HOME/.ark/lib/"
}

remove() {
    rm -f "$HOME/.ark/bin/clang"
    rm -f "$HOME/.ark/bin/clang++"
    rm -f "$HOME/.ark/bin/clang-cpp"
    rm -f "$HOME/.ark/bin/clang-20"
    rm -rf "$HOME/.ark/lib/20"
}
```

To submit a recipe: write `<package>/<version>/recipe.sh`, test it
locally (point a repo dir at your working copy, `ark fetch` to check the
URL/checksum, `ark install <package>` to check `build()`, `ark remove` to
check `remove()`), then open a PR against ark-recipes.
