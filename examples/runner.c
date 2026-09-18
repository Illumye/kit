/*
 * Running other programs, which a build script, a deploy tool or a test
 * harness spends its life doing. The interesting part is not starting them,
 * it is telling apart "the program is not installed", "it ran and failed"
 * and "it worked but wrote to stderr".
 *
 * Shows: KitCommand and KitProcess, the three ways to capture output, and the
 * error codes that separate those three outcomes.
 *
 *   ./examples/runner --check cc make git
 *   ./examples/runner -- echo hello
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"

/* Is the program there, and what does it call itself? One capture, and the
 * error code answers the question the exit status cannot. */
static void probe(const char *program) {
    KitCommand cmd = KIT_ZEROED;
    kit_command_push(&cmd, program);
    kit_command_push(&cmd, "--version");

    KitBuf   out = KIT_ZEROED, errors = KIT_ZEROED;
    KitError err = KIT_ZEROED;

    if (kit_command_capture_split(&cmd, &out, &errors, &err)) {
        KitStr whole = kit_str_from_parts(out.items, out.count);
        KitStr first = kit_str_trim(kit_str_cut(&whole, '\n'));
        /* A copy on the heap, since the buffer is freed just below. */
        char  *banner = kit_str_dup(first);
        printf("  %-8s ok       %s\n", program, banner);
        free(banner);
    } else if (err.code == KIT_ERR_NOT_FOUND) {
        printf("  %-8s missing  %s\n", program, err.message);
    } else if (err.code == KIT_ERR_PROCESS) {
        /* It exists but refused: many tools answer --version with an error. */
        KitStr complaint = kit_str_trim(kit_str_from_parts(errors.items, errors.count));
        printf("  %-8s present  exit %d, said \"" KIT_STR_FMT "\"\n",
               program, err.native, KIT_STR_ARG(kit_str_take(&complaint, 40)));
    } else {
        printf("  %-8s error    %s\n", program, err.message);
    }

    kit_buf_free(&out);
    kit_buf_free(&errors);
    kit_command_free(&cmd);
}

/* The end of what a child wrote. A failing compiler prints screens of output
 * and the useful part is at the bottom, so the report keeps a fixed number of
 * lines and lets the earlier ones fall off the front. The entries are views
 * into the captured buffer, so keeping them costs nothing. */
typedef struct {
    KitStr items[10];
    size_t capacity, head, count;
} Tail;

/* A child writes whatever it likes, including bytes that are not text. Passed
 * straight to a terminal they come out as mojibake at best; decoded and
 * written back, every one of them becomes a character that can be printed.
 * The same pass drops the control characters, since a program that fails is
 * in no position to be moving the cursor around. */
static KitStr printable(KitStr line, KitBuf *into) {
    size_t   start = into->count;
    uint32_t codepoint;

    while (kit_utf8_next(&line, &codepoint)) {
        char   encoded[KIT_UTF8_MAX];
        size_t written;

        if (codepoint < 0x20 && codepoint != '\t') codepoint = KIT_UTF8_REPLACEMENT;
        written = kit_utf8_encode(codepoint, encoded);
        kit_buf_append_n(into, encoded, written);
    }
    return kit_str_from_parts(into->items + start, into->count - start);
}

/* Drains the ring: the report is printed once and the lines are consumed as
 * they go, oldest first, which is the order they were written in. */
static void report_tail(Tail *tail, size_t total_lines) {
    if (tail->count < total_lines)
        printf("  ... %zu earlier line(s)\n", total_lines - tail->count);

    KitBuf safe = KIT_ZEROED;
    while (tail->count > 0) {
        KitStr line = kit_ring_pop(tail);
        printf("  " KIT_STR_FMT "\n", KIT_STR_ARG(printable(line, &safe)));
        kit_buf_reset(&safe);
    }
    kit_buf_free(&safe);
}

/* Two children at once, each waited for separately: what kit_command_spawn is
 * for, and the reason KitProcess exists at all. */
static bool in_parallel(const char *program, KitError *err) {
    KitCommand first = KIT_ZEROED, second = KIT_ZEROED;
    kit_command_push_all(&first,  program, "one", NULL);
    kit_command_push_all(&second, program, "two", NULL);

    KitTimer   clock = kit_timer_start();
    KitProcess a     = kit_command_spawn(&first, err);
    KitProcess b     = a == KIT_PROCESS_INVALID ? KIT_PROCESS_INVALID
                                                : kit_command_spawn(&second, err);

    bool ok = a != KIT_PROCESS_INVALID && b != KIT_PROCESS_INVALID;
    if (a != KIT_PROCESS_INVALID) ok = kit_process_wait(a, err) && ok;
    if (b != KIT_PROCESS_INVALID) ok = kit_process_wait(b, err) && ok;

    if (ok) printf("  both children finished in %.0f ms\n", kit_timer_ms(clock));

    kit_command_free(&first);
    kit_command_free(&second);
    return ok;
}

int main(int argc, char **argv) {
    const char *prog = kit_cli_shift(&argc, &argv);

    bool check = false, merged = false, help = false;
    KitCliOpt opts[] = {
        KIT_CLI_FLAG('c', "check",  "Report on each program named",   &check),
        KIT_CLI_FLAG('m', "merged", "Capture both streams as one",    &merged),
        KIT_CLI_FLAG('h', "help",   "Show this help",                 &help),
    };

    /* The long form, rather than the _arr macro: the same call, with the count
     * spelled out. */
    if (!kit_cli_parse(opts, KIT_COUNTOF(opts), &argc, &argv, NULL)) {
        kit_cli_usage(stderr, prog, opts, KIT_COUNTOF(opts));
        return 1;
    }
    if (help || argc == 0) {
        kit_cli_usage(stdout, prog, opts, KIT_COUNTOF(opts));

        /* The table is data, so a program can read it back. */
        printf("\n%zu options, by kind:\n", KIT_COUNTOF(opts));
        for (size_t i = 0; i < KIT_COUNTOF(opts); i++) {
            KitCliOptType kind = opts[i].type;
            printf("  -%c  %s\n", opts[i].short_name,
                   kind == KIT_CLI_OPT_FLAG ? "a switch" :
                   kind == KIT_CLI_OPT_INT  ? "a number" : "a string");
        }
        return help ? 0 : 1;
    }

    kit_log_set_fields(KIT_LOG_FIELD_LEVEL);

    if (check) {
        printf("checking %d program(s)\n", argc);
        for (int i = 0; i < argc; i++) probe(argv[i]);

        KitError err = KIT_ZEROED;
        if (!in_parallel("true", &err) && err.code != KIT_ERR_NOT_FOUND)
            KIT_WARN("%s", err.message);

        /* The shorthand, for when the failure is not worth inspecting: it
         * logs whatever went wrong and answers yes or no. */
        printf("  the shorthand says %s\n",
               kit_command_run_args("true", NULL) ? "yes" : "no");
        return 0;
    }

    /* Otherwise: run what was asked, and report properly if it fails. */
    KitCommand cmd = KIT_ZEROED;
    for (int i = 0; i < argc; i++) kit_command_push(&cmd, argv[i]);

    KitError err = KIT_ZEROED;
    KitBuf   out = KIT_ZEROED;
    bool     ok  = merged ? kit_command_capture_merged(&cmd, &out, &err)
                          : kit_command_capture(&cmd, &out, &err);

    if (!ok) {
        /* A failure is where the tail earns its place: the whole output goes
         * nowhere, only its end is shown, and the count says what was cut. */
        Tail   tail  = KIT_ZEROED;
        size_t lines = 0;
        kit_ring_init(&tail);

        KitStr rest = kit_str_from_parts(out.items, out.count), line;
        while (kit_str_next(&rest, '\n', &line)) {
            kit_ring_push(&tail, line);
            lines++;
        }

        kit_error_context(&err, "running %s", argv[0]);
        KIT_ERROR("%s (%s)", err.message, kit_error_code_name(err.code));
        report_tail(&tail, lines);
    } else {
        fputs(kit_buf_cstr(&out), stdout);
    }

    /* A KitCommand can be emptied and filled again rather than freed. */
    kit_command_reset(&cmd);
    kit_command_push(&cmd, "true");
    if (cmd.count != 1) KIT_WARN("the command should hold exactly one argument");

    kit_buf_free(&out);
    kit_command_free(&cmd);
    return ok ? 0 : 1;
}
