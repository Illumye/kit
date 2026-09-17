/*
 * The option parser rewrites argv in place while walking it. The invariants
 * that matter are that it never keeps a pointer it was not given, never
 * reports more arguments than it received, and always terminates.
 */

#define KIT_IMPLEMENTATION
#include "../../kit.h"
#include "fuzz_input.h"

#include <assert.h>
#include <stdlib.h>

#define MAX_ARGS 24

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 4096) return 0;
    FuzzInput in = fuzz_input(data, size);

    kit_log_set_level(KIT_LOG_CRITICAL);   /* the errors are expected, not interesting */

    /* Split the input on NUL into argv-shaped tokens, each heap allocated so
     * a read past one is a fault. */
    char  *storage[MAX_ARGS];
    char  *argv_buf[MAX_ARGS];
    int    argc = 0;

    while (argc < MAX_ARGS && fuzz_left(&in) > 0) {
        size_t         n = 0;
        const uint8_t *p = in.data + in.at;
        while (n < fuzz_left(&in) && p[n] != 0) n++;

        char *tok = malloc(n + 1);
        memcpy(tok, p, n);
        tok[n] = '\0';
        storage[argc] = argv_buf[argc] = tok;
        argc++;

        in.at += n;
        if (fuzz_left(&in) > 0) in.at++;   /* step over the separator */
    }

    const int original_argc = argc;

    bool        flag_a = false, flag_b = false, flag_c = false;
    const char *str_o  = "default";
    int         num_j  = 0;

    KitCliOpt opts[] = {
        KIT_CLI_FLAG('a', "all",     "a", &flag_a),
        KIT_CLI_FLAG('b', "brief",   "b", &flag_b),
        KIT_CLI_FLAG('c', NULL,      "c", &flag_c),
        KIT_CLI_STR ('o', "output",  "FILE", "o", &str_o),
        KIT_CLI_INT ('j', "jobs",    "N",    "j", &num_j),
        KIT_CLI_STR (0,   "only-long", "V",  "l", &str_o),
    };

    char **argv = argv_buf;
    if (kit_cli_parse_arr(opts, &argc, &argv)) {
        assert(argc <= original_argc);
        assert(argv == argv_buf);          /* rewritten in place, not moved */

        /* Every surviving argument is one the caller handed in. */
        for (int i = 0; i < argc; i++) {
            bool known = false;
            for (int j = 0; j < original_argc; j++)
                if (argv[i] == storage[j]) { known = true; break; }
            assert(known);
        }

        /* A string option can only point at one of the inputs, or the default,
         * or into the middle of one for the attached forms. */
        if (strcmp(str_o, "default") != 0) {
            bool inside = false;
            for (int j = 0; j < original_argc; j++) {
                char *s = storage[j];
                if (str_o >= s && str_o <= s + strlen(s)) { inside = true; break; }
            }
            assert(inside);
        }
    }

    /* Rendering the usage text must not depend on the parse succeeding. */
    FILE *sink = fopen("/dev/null", "w");
    if (sink) { kit_cli_usage_arr(sink, "fuzz", opts); fclose(sink); }

    for (int i = 0; i < original_argc; i++) free(storage[i]);
    return 0;
}
