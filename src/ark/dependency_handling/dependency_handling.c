#define _POSIX_C_SOURCE 200809L
#include "dependency_handling.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Dependency state                                                          */
/* ------------------------------------------------------------------------- */

struct dependency_state {
        const char* recipes_root;

        ark_dependency_callback callback;
        void* context;

        char** packages;
        size_t package_count;
        size_t package_capacity;
};

/* ------------------------------------------------------------------------- */
/* Visited                                                                    */
/* ------------------------------------------------------------------------- */

static int dependency_seen(struct dependency_state* state, const char* name) {
        size_t i;

        for (i = 0; i < state->package_count; i++) {
                if (strcmp(state->packages[i], name) == 0) return 1;
        }

        return 0;
}

static int dependency_mark(struct dependency_state* state, const char* name) {
        char* copy;

        if (dependency_seen(state, name)) return 0;

        if (state->package_count == state->package_capacity) {
                size_t capacity;
                char** packages;

                capacity =
                    state->package_capacity ? state->package_capacity * 2 : 16;

                packages =
                    realloc(state->packages, capacity * sizeof(*packages));

                if (packages == NULL) return -1;

                state->packages         = packages;
                state->package_capacity = capacity;
        }

        copy = strdup(name);

        if (copy == NULL) return -1;

        state->packages[state->package_count++] = copy;

        return 0;
}

/* ------------------------------------------------------------------------- */
/* Resolver                                                                  */
/* ------------------------------------------------------------------------- */

static int resolve_package(struct dependency_state* state, const char* name,
                           int depth) {
        struct ark_package package;
        int i;

        if (depth > ARK_MAX_DEPENDENCY_DEPTH) {
                fprintf(stderr, "ark: dependency tree too deep at %s\n", name);

                return -1;
        }

        if (dependency_seen(state, name)) return 0;

        memset(&package, 0, sizeof(package));

        if (ark_find_package(state->recipes_root, name, &package) != 0) {
                fprintf(stderr, "ark: dependency not found: %s\n", name);

                return -1;
        }

        /*
         * Mark before descending so cyclic dependencies are detected.
         *
         * A -> B -> A
         *
         * stops when A is encountered again.
         */
        if (dependency_mark(state, package.name) != 0) return -1;

        for (i = 0; package.depends[i] != NULL; i++) {
                if (resolve_package(state, package.depends[i], depth + 1) != 0)
                        return -1;
        }

        if (state->callback != NULL) {
                if (state->callback(&package, state->context) != 0) return -1;
        }

        return 0;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

int ark_resolve_dependencies(const char* package_name, const char* recipes_root,
                             ark_dependency_callback callback, void* context) {
        struct dependency_state state;
        size_t i;
        int result;

        memset(&state, 0, sizeof(state));

        state.recipes_root = recipes_root;
        state.callback     = callback;
        state.context      = context;

        result = resolve_package(&state, package_name, 0);

        for (i = 0; i < state.package_count; i++) free(state.packages[i]);

        free(state.packages);

        return result;
}
