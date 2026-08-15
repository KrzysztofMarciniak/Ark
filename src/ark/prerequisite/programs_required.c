#include "programs_required.h"

#include <stdio.h>
#include <stdlib.h>

static const char* required_programs[] = {"curl",
                                          "tar",// requires XZ support.
                                          "sha256sum", "git", "rm", NULL};

int ark_check_programs_required(void) {
        size_t i;
        char command[256];

        for (i = 0; required_programs[i] != NULL; ++i) {
                snprintf(command, sizeof(command),
                         "command -v %s >/dev/null 2>&1", required_programs[i]);

                if (system(command) != 0) {
                        fprintf(stderr, "ark: required program not found: %s\n",
                                required_programs[i]);

                        return 1;
                }
        }

        return 0;
}
