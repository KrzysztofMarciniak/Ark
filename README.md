# Ark
an offline-ready, minimal package manager written in C.
### Goals
Ark treats a package source as a self-contained software archive rather than merely a remote repository. Once a source is added, Ark downloads and stores the metadata, source archives, and/or binary packages required to operate from that source without network access.

### `ark`

```sh
ark source add <name> <url>
ark source remove <name>
ark source update [name]
ark source list

ark install <package>
ark remove <package>
ark update
ark upgrade

ark search <query>
ark info <package>

ark help
ark version
```

### `ark-build` (To be added)

```sh
ark-build <recipe>
ark-build --clean <recipe>
ark-build --source <recipe>
ark-build --binary <recipe>

ark-build all
ark-build clean
```


