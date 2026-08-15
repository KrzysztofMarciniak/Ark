# Ark

An **offline-ready, minimal package manager written entirely in C for managing user-installed software in `.ark/bin/` on [Simple-Linux](https://github.com/KrzysztofMarciniak/Simple-Linux).**

## What "offline-ready" means

Ark is designed to work **without network access** after an initial setup phase:

1. **Setup phase (online):** Clone recipes repo, run `ark fetch` once
   - Download all source archives to local disk
   - Verify checksums
   - Cache sources in `~/.ark/sources/`

2. **Work phase (offline):** Install, remove, upgrade packages entirely locally
   - No network requests during `ark install` or `ark remove`
   - All sources and metadata already on disk
   - Works on air-gapped machines, poor connectivity, or by design choice

**Difference from traditional package managers:**
- `apt`, `pacman`, `yum`: make network requests during install (online-first)
- Ark: make one network request upfront via `fetch`, then work offline (offline-first)

This makes Ark suitable for:
- Embedded systems with expensive/unreliable connectivity
- Air-gapped deployments
- Build systems that need reproducible, deterministic package acquisition
- Distributing Simple-Linux across isolated machines
## Quick start

```bash
# One-time setup (requires network)
mkdir -p ~/.ark/recipes
git clone https://github.com/KrzysztofMarciniak/ark-recipes.git ~/.ark/recipes/ark-recipes

# Download all sources once (requires network)
ark fetch

# Now work offline, as much as you want
ark install busybox
ark install make
ark remove make
# To use packages installed by Ark, add the following to your .bashrc (or equivalent configuration file for your shell):
export PATH="$HOME/.ark/bin:$PATH"
```

> **Note:** Ark does not technically require a network connection to install packages. It only requires the package recipes and their corresponding source archives to be available locally. You can provide these yourself in `~/.ark/recipes/` and `~/.ark/sources/`, allowing Ark to operate completely offline without running `ark fetch`.

## Build Ark

```bash
./build.sh
```

Compiles `build/ark` — the main binary.

**Requirements:**
- C compiler (default: `cc`, override with `CC` env var)
- POSIX-compatible system
- `tar`, `rm`, `/bin/sh` (used by build and remove scripts)

## User setup

Users must initialize Ark before first use:

```bash
# Create the Ark directory structure
mkdir -p ~/.ark/recipes

# Clone recipes repository
git clone https://github.com/KrzysztofMarciniak/ark-recipes.git ~/.ark/recipes/ark-recipes

# Download all package sources to local disk (one-time, requires network)
ark fetch

# Done. Now install packages as needed, no more network access required.
ark install bash
ark install gcc
```

### What `ark fetch` does

Scans `~/.ark/recipes/` for all recipes, downloads each source to `~/.ark/sources/`, verifies checksums, and caches locally.

```
Process per recipe:
  1. Download source archive (curl)
  2. Verify SHA-256 checksum (sha256sum)
  3. Extract and repackage as sources.tar.xz
  4. Store in ~/.ark/sources/<package>/<version>/sources.tar.xz
```

After `ark fetch` completes, all sources are ready. **Network is no longer required.**

## Commands

### `ark install <package> [package...]`

Build and install one or more packages.

**Process:**
1. Find recipe in `~/.ark/recipes/`
2. Check for source tarball in `~/.ark/sources/<name>/<version>/sources.tar.xz`
3. If missing, fail with "source not fetched" message (run `ark fetch` first)
4. Extract source to temporary directory
5. Source the recipe and run its `build()` function
6. Clean up temporary directory

**Environment variables available in build script:**
- `ARK_SOURCE_ARCHIVE` — path to sources.tar.xz
- `ARK_BUILD_DIR` — temporary build directory
- `ARK_PACKAGE_NAME`, `ARK_PACKAGE_VERSION`
- `ARK_SOURCE_DIR` — path to extracted sources (set by wrapper)

**Example:**
```bash
ark install curl@7.85.0
ark install openssl busybox  # Multiple packages
```

### `ark remove <package>`

Uninstall a package.

**Process:**
1. Find recipe in `~/.ark/recipes/`
2. Source recipe and call its `remove()` function
3. Recipe is responsible for what files to delete

**Example:**
```bash
ark remove curl
```

## Architecture

### Command registration (linker sections)

Commands are defined via `ARK_COMMAND()` macros, which place command definitions into the `ark_commands` linker section. At startup, the registry reads from `__start_ark_commands` to `__stop_ark_commands` to discover all commands automatically.

**Startup sequence:**
1. `main()` checks prerequisites (`.ark` dir exists, recipes dir exists, required programs available)
2. `ark_command_registry_init()` populates registry from linker section
3. `ark_command_logic_execute()` routes to appropriate handler based on CLI args
4. Handler executes and returns status
5. Registry freed on exit

### Directory layout

```
src/
├── command_logic/       # Command registry, routing, help
├── source/              # Archive type detection
├── ark/                 # Main binary
│   ├── main.c
│   ├── commands/
│   │   ├── version/     # Show version
│   │   ├── fetch/       # Download all sources
│   │   ├── install/     # Build and install
│   │   └── remove/      # Uninstall
│   ├── package_handling/  # Recipe search, package spec parsing
│   └── prerequisite/    # Startup validation
```

## Recipes

Recipes live in the [ark-recipes](https://github.com/KrzysztofMarciniak/ark-recipes) repository:

```
~/.ark/recipes/ark-recipes/<package>/<version>/recipe.sh
```

### Recipe format

A recipe is a shell script that defines two functions:

#### `build()`
Called by `ark install`. Has access to:
- `$ARK_SOURCE_DIR` — extracted source directory (already set up)
- `$ARK_BUILD_DIR` — temporary work directory
- `$ARK_PACKAGE_NAME`, `$ARK_PACKAGE_VERSION`
- Standard shell commands

```bash
build() {
    cd "$ARK_SOURCE_DIR"
    ./configure --prefix=$HOME/.local
    make
    make install
}
```

#### `remove()`
Called by `ark remove`. Must clean up installed files.

```bash
remove() {
    rm -f $HOME/.local/bin/myprogram
    rm -f $HOME/.local/lib/libmylib.so
}
```

### Full example recipe

File: `~/.ark/recipes/ark-recipes/curl/7.85.0/recipe.sh`

```bash
build() {
    cd "$ARK_SOURCE_DIR"
    ./configure --prefix=$HOME/.local
    make
    make install
}

remove() {
    rm -f $HOME/.local/bin/curl
    rm -f $HOME/.local/lib/libcurl*
    rm -rf $HOME/.local/share/man/man1/curl*
}
```

## Package storage

### Sources structure

Sources are cached in:
```
~/.ark/sources/<package>/<version>/sources.tar.xz
```

`ark fetch` downloads and verifies these. `ark install` extracts them as needed.

### Package spec syntax

Packages are specified as:
- `name` — uses first version found
- `name@version` — requires exact version

Example:
```bash
ark install bash
ark install gcc@11.2.0
```

## Adding a new recipe

1. Clone or create a directory under `~/.ark/recipes/ark-recipes/`
2. Create: `~/.ark/recipes/ark-recipes/<package>/<version>/recipe.sh`
3. Define `build()` and `remove()` functions
4. Run `ark fetch` to download the source
5. Test with `ark install <package>@<version>`

## Adding a new command to Ark

1. Create `src/ark/commands/<name>/<name>.c`
2. Implement the handler function:
   ```c
   int ark_command_foo(int argc, char **argv) {
       // implementation
       return 0;  // 0 = success, 1 = failure
   }
   ```
3. Register it at the end of the file:
   ```c
   ARK_COMMAND(
       "foo",
       "ark foo [args]",
       "Short description",
       ark_command_foo
   );
   ```
4. Add the .c file to the compile command in `build.sh`
5. Run `./build.sh`

The command will be auto-discovered at runtime.

## Data structures

```c
struct ark_package {
    char name[256];
    char version[256];
    char recipe_path[4096];
};

struct ark_command_definition {
    const char *name;        // "install", "remove", etc.
    const char *parent;      // NULL for top-level commands
    const char *usage;       // "ark install <package>"
    const char *description; // "Build and install packages"
    ark_command_handler handler;  // Function pointer
};
```

## Known limitations & future work

- **No dependency resolution** — each recipe is standalone; transitive deps must be manually installed
- **No lockfiles** — no reproducible installs across machines (planned per original README)
- **No subcommands yet** — `source add/remove/list/update` are planned but not implemented
- **No conflict detection** — multiple versions can be "installed" (recipes control install location)
- **Shell-based recipes** — recipes are shell scripts, not compiled (future: C-based recipes per original goals)

## Testing

Manual testing workflow:

```bash
# Build
./build.sh

# Setup (one time)
mkdir -p ~/.ark/recipes
git clone https://github.com/KrzysztofMarciniak/ark-recipes.git ~/.ark/recipes/ark-recipes

# Fetch sources
./build/ark fetch

# Install a package
./build/ark install busybox

# Remove it
./build/ark remove busybox
```

## Design principles

- **Minimal dependencies** — only uses POSIX + tar, no external package libraries
- **User-scoped only** — no system-wide installation, no root required
- **Offline-first** — network access is upfront in `fetch`, not per-install
- **Self-contained sources** — one `ark fetch` downloads everything needed
- **Reproducible** — same sources, same recipes, same output (lockfiles planned)
