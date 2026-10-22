/*
 * The option parser rewrites argv in place while walking it. The invariants
 * that matter are that it never keeps a pointer it was not given, never
 * reports more arguments than it received, and always terminates.
 *
 * Every input is parsed twice: against a flat table, and against one with
 * commands, nested commands and a rest list, where the line is read against
 * a different table at every command it names.
 */

#define KIT_IMPLEMENTATION
#include "../../kit.h"
#include "fuzz_input.h"

#include <assert.h>
#include <stdlib.h>

#define MAX_ARGS 24

/* True when p is one of the tokens, or points inside one: the attached forms
 * of a value, -ofile and --output=file, hand over the middle of an argument. */
static bool from_input(const char *p, char *const *storage, int count) {
    for (int j = 0; j < count; j++) {
        const char *s = storage[j];
        if (p >= s && p <= s + strlen(s)) return true;
    }
    return false;
}

/* Parses a fresh copy of the tokens against one table and checks what came
 * back. The usage is rendered afterwards whatever happened, since it follows
 * the commands the parse chose and must cope with a parse that stopped half
 * way. */
static void check(KitCliOpt *opts, size_t n_opts, char *const *storage, int count,
                  const char **str_o, KitCliArgs *rest) {
    char *argv_buf[MAX_ARGS + 1];
    for (int i = 0; i < count; i++) argv_buf[i] = storage[i];
    argv_buf[count] = NULL;

    char **argv = argv_buf;
    int    argc = count;
    if (kit_cli_parse(opts, n_opts, &argc, &argv, NULL)) {
        assert(argc <= count);
        assert(argv == argv_buf);          /* rewritten in place, not moved */

        /* Every surviving argument is one the caller handed in. */
        for (int i = 0; i < argc; i++) {
            bool known = false;
            for (int j = 0; j < count; j++)
                if (argv[i] == storage[j]) { known = true; break; }
            assert(known);
        }

        /* A string option can only point at one of the inputs, or the default,
         * or into the middle of one for the attached forms. */
        if (strcmp(*str_o, "default") != 0) assert(from_input(*str_o, storage, count));

        /* The rest is a window on the end of argv itself, after the positional
         * arguments, and it still ends on the NULL argv ends on. */
        if (rest && rest->items) {
            assert(rest->items >= argv_buf + argc);
            assert(rest->items + rest->count == argv_buf + count);
            assert(rest->items[rest->count] == NULL);
            assert((size_t)argc + rest->count < (size_t)count);   /* "--" itself */
        }
    }

    FILE *sink = fopen("/dev/null", "w");
    if (sink) { kit_cli_usage(sink, "fuzz", opts, n_opts); fclose(sink); }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 4096) return 0;
    FuzzInput in = fuzz_input(data, size);

    kit_log_set_level(KIT_LOG_CRITICAL);   /* the errors are expected, not interesting */

    /* Split the input on NUL into argv-shaped tokens, each heap allocated so
     * a read past one is a fault. */
    char *storage[MAX_ARGS];
    int   count = 0;

    while (count < MAX_ARGS && fuzz_left(&in) > 0) {
        size_t         n = 0;
        const uint8_t *p = in.data + in.at;
        while (n < fuzz_left(&in) && p[n] != 0) n++;

        char *tok = malloc(n + 1);
        memcpy(tok, p, n);
        tok[n] = '\0';
        storage[count++] = tok;

        in.at += n;
        if (fuzz_left(&in) > 0) in.at++;   /* step over the separator */
    }

    bool        flag_a = false, flag_b = false, flag_c = false;
    const char *str_o  = "default";
    int         num_j  = 0, when = -1;

    KitCliOpt flat[] = {
        KIT_CLI_FLAG('a', "all",     "a", &flag_a),
        KIT_CLI_FLAG('b', "brief",   "b", &flag_b),
        KIT_CLI_FLAG('c', NULL,      "c", &flag_c),
        KIT_CLI_STR ('o', "output",  "FILE", "o", &str_o),
        KIT_CLI_INT ('j', "jobs",    "N",    "j", &num_j),
        KIT_CLI_STR (0,   "only-long", "V",  "l", &str_o),
        KIT_CLI_CHOICE('w', "when", "auto||never", "w", &when),
    };
    check(flat, KIT_COUNTOF(flat), storage, count, &str_o, NULL);

    /* A choice is a position in its list or untouched, never anything else.
     * The empty middle entry is a choice like the others. */
    assert(when >= -1 && when <= 2);

    /* x takes over -a from the root, and y nests a command of its own. */
    bool       cmd_x = false, cmd_y = false, cmd_z = false, flag_x = false;
    KitCliArgs rest  = {0};
    str_o = "default";

    KitCliOpt z_opts[] = {
        KIT_CLI_REST("ARGS", "r", &rest),
    };
    KitCliOpt y_opts[] = {
        KIT_CLI_COMMAND("z", "z", z_opts, &cmd_z),
    };
    KitCliOpt x_opts[] = {
        KIT_CLI_FLAG('a', "ax", "a", &flag_x),
        KIT_CLI_REST("ARGS", "r", &rest),
    };
    KitCliOpt commands[] = {
        KIT_CLI_FLAG        ('a', "all",    "a", &flag_a),
        KIT_CLI_STR         ('o', "output", "FILE", "o", &str_o),
        KIT_CLI_COMMAND     ("x", "x", x_opts, &cmd_x),
        KIT_CLI_COMMAND     ("y", "y", y_opts, &cmd_y),
        KIT_CLI_COMMAND_BARE("w", "w", &cmd_y),
    };
    check(commands, KIT_COUNTOF(commands), storage, count, &str_o, &rest);

    for (int i = 0; i < count; i++) free(storage[i]);
    return 0;
}
