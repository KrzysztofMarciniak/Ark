# Ark

An offline-ready, minimal package manager written in C.

## Goals

Ark treats a package source as a self-contained software archive rather than
merely a remote repository. Once a source is added, Ark downloads and stores
the metadata, source archives, and/or binary packages required to operate
from that source without network access — including the full transitive
dependency closure, not just top-level packages, so resolution and
installation never need to reach the network.

## Scope

Ark is user-installed only — there is no system-wide mode. All state (store,
sources, lockfiles, installed packages) lives under the invoking user's home
directory (e.g. `~/.ark/`), with no writes outside it and no root/setuid
requirement. Multiple users on the same machine each get an entirely
independent Ark install.

## Network access

Ark prompts for outbound network access **once per invocation**, not once
per file or request within it. Whether a command triggers the prompt
depends on whether it can be satisfied entirely from the local store:

```
ark will make outbound access [y/N]
```

| Command | Needs network? | Prompts? |
|---|---|---|
| `ark install <pkg>` — pkg + full dep closure already in store | No | No |
| `ark install <pkg>` — pkg or a dep missing from store | Yes | Yes, once |
| `ark upgrade` | Yes (checks for newer versions) | Yes, once |
| `ark source update` | Yes | Yes, once |
| `ark source add` | Yes (fetches index) | Yes, once, plus shows the URL for review since adding a source is a trust decision |
| `ark search`, `ark info`, `ark remove`, `ark source list` | No | No |

Notes:

- `-y` / `--yes` skips the prompt (for scripts, cron, provisioning). All the
  commands above accept it.
- Answering `N` aborts the transaction rather than partially applying it —
  `ark upgrade` run this way does nothing, it does not silently fall back to
  an offline-only partial upgrade.
- `ark install` of a package not yet in the store follows the same
  single-prompt rule as `ark upgrade`; it's called out separately here since
  it's easy to assume `install` is always offline.

## Concepts

- **Source** — a named, priority-ordered origin of packages (remote URL or
  local build output). Each source, once added and updated, holds a complete
  local copy of its metadata and dependency graph.
- **Store** — the local package cache under `~/.ark/store/`, shared between
  sources and `ark-build` output.
- **Lockfile** (`ark.lock`) — records the exact resolved version, source, and
  content hash for each installed package, so installs are reproducible
  across machines.

## `ark`

```sh
ark source add <name> <url> [--priority N]
ark source remove <name>
ark source update [name]
ark source verify [name]
ark source list

ark install <package>[@source] [--source <name>] [--locked]
ark remove <package>
ark update              # recompute available upgrades against local source data; writes a plan
ark upgrade [--no-lock] # execute the update plan, fetching only what's missing locally
ark search <query>
ark info <package>
ark help
ark version
```

### Command semantics

Three verbs look similar but act on different things:

| Command | Acts on | Effect |
|---|---|---|
| `ark source update [name]` | source metadata | Refreshes the index, full dependency graph, and archive/binary cache for that source. Installed packages are untouched. |
| `ark update` | installed package records | Recomputes what upgrades are available given current source metadata and writes a resolved plan. Fetches and installs nothing. |
| `ark upgrade` | installed packages | Executes the plan from `ark update` (computing one first if none is cached), fetching only what the offline source cache doesn't already have. |

Pipeline: `ark source update` → `ark update` → `ark upgrade`.

### Source priority and conflicts

Sources are priority-ordered (`--priority`, shown by `ark source list`). When
multiple sources provide the same package, the highest-priority source wins
by default. Use `<package>@<source>` or `--source <name>` on `ark install` to
override this per-install without changing global policy.

### Lockfile

`ark install` writes/updates `ark.lock` with the resolved version, source,
and hash for each package. `ark install --locked` reproduces exactly what's
in the lockfile and fails rather than silently re-resolving if that's not
possible offline. `ark upgrade --no-lock` is the explicit escape hatch for
moving off a locked state.

## Target

Ark is being built as the package manager for
[Simple-Linux](https://github.com/KrzysztofMarciniak/Simple-Linux), a Linux
distribution using uClibc-ng, BusyBox, and LLVM Clang. This shapes a few
design points:

- Package manifests record the libc and architecture a binary was built
  against, so `ark install` can refuse or warn on an ABI mismatch (e.g. a
  glibc binary on a uClibc-ng system) instead of installing something that
  fails at runtime.
- `ark-build` recipes are C programs, compiled with Clang at build time (not
  shell scripts), so there's no dependency on GNU coreutils/bash semantics
  that BusyBox's userland doesn't provide.
- Binaries produced this way are keyed to the Simple-Linux target and are
  not expected to be portable to other distros.

## `ark-build` (to be added)

```sh
ark-build <recipe>
ark-build --clean <recipe>
ark-build --source <recipe>
ark-build --binary <recipe>
ark-build all
ark-build clean
```

Recipes are C source, compiled with Clang at the time they're needed rather
than interpreted. `ark-build` writes its output into `~/.ark/store/local/`
using the same package manifest format (name, version, deps, archive hash,
source/binary flag, libc + arch tag) that remote sources use. That directory is registered as an ordinary
source:

```sh
ark source add local file:///~/.ark/store/local --priority <highest>
```

so `ark install` never distinguishes a locally built package from a
remotely fetched one — it's just another entry in the priority-ordered
source list.
