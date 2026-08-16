#define _POSIX_C_SOURCE 200809L
#include "dependency_handling.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Dependency spec parsing                                                   */
/*                                                                           */
/* Grammar (each element of ark_package.depends[] is parsed with this):     */
/*                                                                           */
/*   alt   := name | '(' group ')'                                         */
/*   group := alt ( '||' alt )*                                            */
/*                                                                           */
/* i.e. plain names are required as-is; parenthesized groups (which may     */
/* themselves contain nested parenthesized groups) mean "exactly one of     */
/* these alternatives is required". Resolution tries alternatives in the    */
/* order they're written and takes the first that resolves successfully.   */
/* ------------------------------------------------------------------------- */

struct dep_alt {
        char* name;              /* non-NULL for a leaf name */
        struct dep_group* group; /* non-NULL for a nested '(' ... ')' */
};

struct dep_group {
        struct dep_alt* alts;
        size_t count;
        size_t capacity;
};

static struct dep_group* dep_group_new(void) {
        struct dep_group* g = calloc(1, sizeof(*g));
        return g;
}

static void dep_group_free(struct dep_group* g);

static void dep_alt_free(struct dep_alt* a) {
        free(a->name);
        if (a->group != NULL) dep_group_free(a->group);
}

static void dep_group_free(struct dep_group* g) {
        size_t i;

        if (g == NULL) return;

        for (i = 0; i < g->count; i++) dep_alt_free(&g->alts[i]);

        free(g->alts);
        free(g);
}

static int dep_group_push(struct dep_group* g, struct dep_alt alt) {
        if (g->count == g->capacity) {
                size_t capacity = g->capacity ? g->capacity * 2 : 4;
                struct dep_alt* alts =
                    realloc(g->alts, capacity * sizeof(*alts));

                if (alts == NULL) return -1;

                g->alts     = alts;
                g->capacity = capacity;
        }

        g->alts[g->count++] = alt;

        return 0;
}

static void skip_space(const char** p) {
        while (isspace((unsigned char)**p)) (*p)++;
}

static struct dep_group* parse_group(const char** p, int depth);

/* Parses a single alternative: either a bare name, or a nested '(' group ')'.
 */
static int parse_alt(const char** p, int depth, struct dep_alt* out) {
        memset(out, 0, sizeof(*out));

        skip_space(p);

        if (depth > ARK_MAX_DEPENDENCY_DEPTH) {
                fprintf(stderr, "ark: dependency spec nested too deeply\n");
                return -1;
        }

        if (**p == '(') {
                (*p)++; /* consume '(' */

                out->group = parse_group(p, depth + 1);
                if (out->group == NULL) return -1;

                skip_space(p);

                if (**p != ')') {
                        fprintf(stderr,
                                "ark: missing ')' in dependency spec\n");
                        return -1;
                }

                (*p)++; /* consume ')' */

                return 0;
        }

        {
                const char* start = *p;

                while (**p != '\0' && **p != '(' && **p != ')' &&
                       !isspace((unsigned char)**p) &&
                       strncmp(*p, "||", 2) != 0) {
                        (*p)++;
                }

                if (*p == start) {
                        fprintf(stderr, "ark: empty term in dependency spec\n");
                        return -1;
                }

                out->name = strndup(start, (size_t)(*p - start));

                if (out->name == NULL) return -1;
        }

        return 0;
}

/* Parses a '||'-separated sequence of alternatives, up to (but not
 * consuming) a terminating ')' or end of string. */
static struct dep_group* parse_group(const char** p, int depth) {
        struct dep_group* g = dep_group_new();

        if (g == NULL) return NULL;

        for (;;) {
                struct dep_alt alt;

                if (parse_alt(p, depth, &alt) != 0) {
                        dep_group_free(g);
                        return NULL;
                }

                if (dep_group_push(g, alt) != 0) {
                        dep_alt_free(&alt);
                        dep_group_free(g);
                        return NULL;
                }

                skip_space(p);

                if (strncmp(*p, "||", 2) == 0) {
                        *p += 2;
                        continue;
                }

                break;
        }

        return g;
}

/* A sequence is a space-separated list of terms, ALL of which are
 * required (AND). Each term is itself an OR-chain (a single alt, or
 * several alts joined by '||', or a nested parenthesized group) --
 * exactly what parse_group already parses one instance of. */
struct dep_sequence {
        struct dep_group** terms;
        size_t count;
        size_t capacity;
};

static void dep_sequence_free(struct dep_sequence* seq) {
        size_t i;

        if (seq == NULL) return;

        for (i = 0; i < seq->count; i++) dep_group_free(seq->terms[i]);

        free(seq->terms);
        free(seq);
}

static int dep_sequence_push(struct dep_sequence* seq, struct dep_group* g) {
        if (seq->count == seq->capacity) {
                size_t capacity = seq->capacity ? seq->capacity * 2 : 4;
                struct dep_group** terms =
                    realloc(seq->terms, capacity * sizeof(*terms));

                if (terms == NULL) return -1;

                seq->terms    = terms;
                seq->capacity = capacity;
        }

        seq->terms[seq->count++] = g;

        return 0;
}

/* Parses a full dependency spec string, e.g.:
 *   "clang"
 *   "(clang || gcc)"
 *   "(clang || gcc) fastfetch (cmake || wolfssl)"
 *   "(clang || (tcc || gcc)) fastfetch"
 * Returns NULL on malformed input. */
static struct dep_sequence* parse_dependency_spec(const char* spec) {
        const char* p            = spec;
        struct dep_sequence* seq = calloc(1, sizeof(*seq));

        if (seq == NULL) return NULL;

        skip_space(&p);

        while (*p != '\0') {
                struct dep_group* term = parse_group(&p, 0);

                if (term == NULL) {
                        dep_sequence_free(seq);
                        return NULL;
                }

                if (dep_sequence_push(seq, term) != 0) {
                        dep_group_free(term);
                        dep_sequence_free(seq);
                        return NULL;
                }

                skip_space(&p);
        }

        return seq;
}

/* Cheap existence check, implemented in terms of ark_find_package so no
 * header changes are required. Discards the populated package on success. */
static int ark_package_exists(const char* recipes_root, const char* name) {
        struct ark_package package;

        memset(&package, 0, sizeof(package));

        return ark_find_package(recipes_root, name, &package) == 0;
}

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
                           int depth);

/* Tries each alternative in a group, in order, and resolves the first one
 * that exists. A leaf alternative resolves via resolve_package; a nested
 * group alternative recurses via resolve_group. Returns 0 and leaves the
 * winning package resolved/marked on success, or -1 if every alternative
 * in the group failed to resolve (with a diagnostic listing them). */
static int resolve_group(struct dependency_state* state,
                         struct dep_group* group, int depth) {
        size_t i;

        for (i = 0; i < group->count; i++) {
                struct dep_alt* alt = &group->alts[i];

                if (alt->name != NULL) {
                        if (ark_package_exists(state->recipes_root,
                                               alt->name)) {
                                if (resolve_package(state, alt->name, depth) ==
                                    0) {
                                        return 0;
                                }
                                /* Found but failed to resolve (e.g. its own
                                 * sub-dependencies are unsatisfiable) -- do
                                 * not fall through silently on a cycle
                                 * error, but do allow trying siblings when
                                 * the failure is "not found" further down.
                                 */
                                return -1;
                        }
                } else if (alt->group != NULL) {
                        if (resolve_group(state, alt->group, depth) == 0) {
                                return 0;
                        }
                }
        }

        fprintf(stderr, "ark: no available alternative satisfies:");
        for (i = 0; i < group->count; i++) {
                struct dep_alt* alt = &group->alts[i];

                fprintf(stderr, " %s",
                        alt->name != NULL ? alt->name : "(nested group)");
                if (i + 1 < group->count) fprintf(stderr, " ||");
        }
        fprintf(stderr, "\n");

        return -1;
}

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

        /*
         * package.depends[] is whitespace-tokenized by the recipe loader,
         * so a single spec like "(a || b) c (d || e)" arrives split across
         * multiple array elements: "(a", "||", "b)", "c", "(d", "||",
         * "e)". Reassemble the full spec string before parsing it, since
         * '(' ... '||' ... ')' groups only make sense parsed as a whole.
         */
        {
                char* joined      = NULL;
                size_t joined_len = 0;

                for (i = 0; package.depends[i] != NULL; i++) {
                        size_t tok_len = strlen(package.depends[i]);
                        size_t add_len = tok_len + (joined_len ? 1 : 0);
                        char* grown = realloc(joined, joined_len + add_len + 1);

                        if (grown == NULL) {
                                free(joined);
                                return -1;
                        }

                        joined = grown;

                        if (joined_len) joined[joined_len++] = ' ';

                        memcpy(joined + joined_len, package.depends[i],
                               tok_len);
                        joined_len += tok_len;
                        joined[joined_len] = '\0';
                }

                if (joined != NULL) {
                        struct dep_sequence* seq =
                            parse_dependency_spec(joined);
                        size_t t;
                        int failed = 0;

                        if (seq == NULL) {
                                fprintf(stderr,
                                        "ark: malformed dependency spec '%s' "
                                        "in package '%s'\n",
                                        joined, package.name);
                                free(joined);
                                return -1;
                        }

                        free(joined);

                        for (t = 0; t < seq->count; t++) {
                                if (resolve_group(state, seq->terms[t],
                                                  depth + 1) != 0) {
                                        failed = 1;
                                        break;
                                }
                        }

                        dep_sequence_free(seq);

                        if (failed) return -1;
                }
        }

        if (state->callback != NULL) {
                if (state->callback(&package, state->context) != 0) return -1;
        }

        return 0;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

/*
 * Resolves dependencies for multiple top-level targets against a
 * single shared "already resolved" set, so a dependency common to
 * two or more targets is only found/built/callback'd once instead of
 * once per target that needs it.
 *
 * Targets are processed in order. If resolving one target fails, its
 * per_target_ok[i] entry is set to 0 and processing continues with
 * the next target -- a failure on one target does not abort the
 * others, matching the previous per-target-loop behavior in
 * ark_command_install. Anything already successfully resolved for an
 * earlier target (including partial progress on a target that later
 * failed) remains in the shared seen set and is not re-attempted.
 *
 * per_target_ok must point to an array of at least package_count
 * ints; each is set to 1 on success, 0 on failure. Pass NULL if you
 * don't need per-target results (e.g. all failures are fatal to you
 * anyway).
 *
 * Returns 0 if every target resolved successfully, -1 if any did.
 */
int ark_resolve_dependencies_multi(char** package_names, size_t package_count,
                                   const char* recipes_root,
                                   ark_dependency_callback callback,
                                   void* context, int* per_target_ok) {
        struct dependency_state state;
        size_t i;
        int any_failed;

        memset(&state, 0, sizeof(state));

        state.recipes_root = recipes_root;
        state.callback     = callback;
        state.context      = context;

        any_failed = 0;

        for (i = 0; i < package_count; i++) {
                int ok = resolve_package(&state, package_names[i], 0) == 0;

                if (per_target_ok != NULL) per_target_ok[i] = ok;

                if (!ok) any_failed = 1;
        }

        for (i = 0; i < state.package_count; i++) free(state.packages[i]);

        free(state.packages);

        return any_failed ? -1 : 0;
}

int ark_resolve_dependencies(const char* package_name, const char* recipes_root,
                             ark_dependency_callback callback, void* context) {
        int ok;
        int result;

        result = ark_resolve_dependencies_multi((char**)&package_name, 1,
                                                 recipes_root, callback,
                                                 context, &ok);

        return result;
}
