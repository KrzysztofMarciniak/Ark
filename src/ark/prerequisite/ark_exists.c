#include "ark_exists.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int
ark_check_exists(void)
{
    const char *home;
    char path[4096];
    struct stat st;

    home = getenv("HOME");

    if (home == NULL) {
        fprintf(stderr, "ark: HOME is not set\n");
        return 1;
    }

    snprintf(path, sizeof(path), "%s/.ark", home);

    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;

    fprintf(
        stderr,
        "ark: %s does not exist\n"
        "ark: create it with:\n"
        "    mkdir -p %s/.ark/recipes\n"
        "ark: then clone the official recipes:\n"
        "    git clone git@github.com:KrzysztofMarciniak/ark-recipes.git "
        "%s/.ark/recipes/ark-recipes\n",
        path,
        home,
        home
    );

    return 1;
}

