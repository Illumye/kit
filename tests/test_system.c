/*
 * CLI option parsing, command execution, stopwatch and maths.
 * Includes the regression test for the truncated kit_cli_usage column.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

/* The deadlock tests need a child that floods both streams. Borrowing the
 * shell for that made them depend on what `yes`, `head` and cmd.exe do on the
 * machine: a CI runner's shell added its own 68 bytes to stderr, and its
 * cmd.exe produced almost nothing. This binary spawns itself instead, so the
 * child is the same program everywhere and writes exactly what is asked. */
#define SPEW_LINE  "padding padding padding padding padding padding padding\n"
#define SPEW_TIMES 4000                       /* well past any pipe buffer */

static const char *self_path = NULL;

static int spew(void) {
    for (int i = 0; i < SPEW_TIMES; i++) {
        fputs("out " SPEW_LINE, stdout);
        fputs("err " SPEW_LINE, stderr);
    }
    fflush(stdout);
    fflush(stderr);
    return 0;
}

static size_t spew_bytes(void) {
    return (size_t)SPEW_TIMES * (strlen(SPEW_LINE) + 4);
}

/* Builds an argv the way main() receives it, minus argv[0].
 * The one cast keeps string literals const while handing kit_cli_parse the char**
 * it expects; the parser only reorders the pointers, never the characters. */
#define ARGV(...)                                                       \
    const char *argv_storage[] = { __VA_ARGS__ };                       \
    int    argc = (int)KIT_COUNTOF(argv_storage);                   \
    char **argv = (char **)(void *)argv_storage

/* --- option parsing ------------------------------------------------------- */

TEST(cli_parse_every_supported_form) {
    bool        verbose = false;
    const char *output  = "default";
    int         jobs    = 1;

    KitCliOpt opts[] = {
        KIT_CLI_FLAG('v', "verbose", "verbose", &verbose),
        KIT_CLI_STR ('o', "output", "FILE", "output", &output),
        KIT_CLI_INT ('j', "jobs", "N", "jobs", &jobs),
    };

    {   /* short with a separate value, and an attached one */
        ARGV("-v", "-o", "sep.txt", "-j4", "file.c");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK(verbose);
        CHECK_STR(output, "sep.txt");
        CHECK_INT(jobs, 4);
        CHECK_INT(argc, 1);
        CHECK_STR(argv[0], "file.c");
    }
    {   /* '=' separators, short and long */
        ARGV("-o=eq.txt", "--jobs=8");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK_STR(output, "eq.txt");
        CHECK_INT(jobs, 8);
        CHECK_INT(argc, 0);
    }
    {   /* long with a separate value */
        ARGV("--output", "long.txt", "--verbose");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK_STR(output, "long.txt");
        CHECK_INT(argc, 0);
    }
    {   /* negative integers */
        ARGV("--jobs=-3");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK_INT(jobs, -3);
    }
}

TEST(cli_parse_grouped_short_flags) {
    bool        a = false, b = false, c = false;
    const char *output = "default";
    int         jobs   = 0;

    KitCliOpt opts[] = {
        KIT_CLI_FLAG('a', "all",    "a", &a),
        KIT_CLI_FLAG('b', "brief",  "b", &b),
        KIT_CLI_FLAG('c', "colour", "c", &c),
        KIT_CLI_STR ('o', "output", "FILE", "output", &output),
        KIT_CLI_INT ('j', "jobs",   "N",    "jobs",   &jobs),
    };

    {   /* the whole point: -abc means -a -b -c */
        ARGV("-abc", "rest.c");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK(a); CHECK(b); CHECK(c);
        CHECK_INT(argc, 1);
        CHECK_STR(argv[0], "rest.c");
    }
    {   /* a group ending on a value option, value attached */
        a = b = false; output = "default";
        ARGV("-abofile.txt");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK(a); CHECK(b);
        CHECK_STR(output, "file.txt");
    }
    {   /* a group ending on a value option, value in the next argument */
        a = false; jobs = 0;
        ARGV("-aj", "12");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK(a);
        CHECK_INT(jobs, 12);
    }
    {   /* '=' still works at the end of a group */
        a = false; output = "default";
        ARGV("-ao=eq.txt");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK(a);
        CHECK_STR(output, "eq.txt");
    }
    {   /* an unknown letter names itself, not the whole group */
        ARGV("-azc");
        CHECK(!kit_cli_parse_arr(opts, &argc, &argv, NULL));
    }
    {   /* a flag inside a group still refuses a value */
        ARGV("-ab=1");
        CHECK(!kit_cli_parse_arr(opts, &argc, &argv, NULL));
    }
    {   /* a value option at the end of a group with nothing left */
        ARGV("-ao");
        CHECK(!kit_cli_parse_arr(opts, &argc, &argv, NULL));
    }
}

TEST(cli_parse_positionals_and_double_dash) {
    bool verbose = false;
    KitCliOpt opts[] = { KIT_CLI_FLAG('v', "verbose", "verbose", &verbose) };

    ARGV("first", "-v", "second", "--", "-v", "--not-an-option", "-");
    CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
    CHECK(verbose);
    CHECK_INT(argc, 5);
    CHECK_STR(argv[0], "first");
    CHECK_STR(argv[1], "second");
    CHECK_STR(argv[2], "-v");            /* after --, options are positional */
    CHECK_STR(argv[3], "--not-an-option");
    CHECK_STR(argv[4], "-");             /* a lone dash is positional */
}

TEST(cli_parse_rejects_bad_input) {
    bool verbose = false;
    const char *output = "x";
    int jobs = 0;
    KitCliOpt opts[] = {
        KIT_CLI_FLAG('v', "verbose", "verbose", &verbose),
        KIT_CLI_STR ('o', "output", "FILE", "output", &output),
        KIT_CLI_INT ('j', "jobs", "N", "jobs", &jobs),
    };

    { ARGV("--unknown");    CHECK(!kit_cli_parse_arr(opts, &argc, &argv, NULL)); }
    { ARGV("-z");           CHECK(!kit_cli_parse_arr(opts, &argc, &argv, NULL)); }
    { ARGV("--output");     CHECK(!kit_cli_parse_arr(opts, &argc, &argv, NULL)); }  /* value missing */
    { ARGV("--verbose=1");  CHECK(!kit_cli_parse_arr(opts, &argc, &argv, NULL)); }  /* flag takes none */
    { ARGV("--jobs=abc");   CHECK(!kit_cli_parse_arr(opts, &argc, &argv, NULL)); }
    { ARGV("--jobs=12x");   CHECK(!kit_cli_parse_arr(opts, &argc, &argv, NULL)); }
    { ARGV("--jobs=99999999999999999999"); CHECK(!kit_cli_parse_arr(opts, &argc, &argv, NULL)); }
}

/* Regression: the help column was rendered into a 48-byte buffer, which cut
 * long option names in half. */
TEST(cli_usage_prints_long_names_in_full) {
    const char *out = "x";
    KitCliOpt opts[] = {
        KIT_CLI_STR('x', "an-extremely-long-option-name-that-overflows-the-column",
                "A_VERY_LONG_METAVARIABLE_NAME", "the help text", &out),
    };

    const char *path = "utest-tmp-usage.txt";
    FILE *fp = fopen(path, "w");
    if (!CHECK(fp != NULL)) return;
    kit_cli_usage_arr(fp, "prog", opts);
    fclose(fp);

    char *text = kit_fs_read(path, NULL);
    if (!CHECK(text != NULL)) return;
    CHECK(strstr(text, "--an-extremely-long-option-name-that-overflows-the-column"
                       "=<A_VERY_LONG_METAVARIABLE_NAME>") != NULL);
    CHECK(strstr(text, "the help text") != NULL);
    CHECK(strstr(text, "Usage: prog") != NULL);
    free(text);
    remove(path);
}

TEST(cli_shift_consumes_left_to_right) {
    ARGV("prog", "a", "b");
    CHECK_STR(kit_cli_shift(&argc, &argv), "prog");
    CHECK_STR(kit_cli_shift(&argc, &argv), "a");
    CHECK_STR(kit_cli_shift(&argc, &argv), "b");
    CHECK(kit_cli_shift(&argc, &argv) == NULL);
    CHECK_INT(argc, 0);
}

/* --- command execution ---------------------------------------------------- */

#ifndef _WIN32
TEST(command_capture_collects_stdout) {
    KitCommand c = {0};
    kit_command_push_all(&c, "printf", "one\ntwo\n", NULL);

    KitBuf sb = {0};
    CHECK(kit_command_capture(&c, &sb, NULL));
    CHECK_STR(kit_buf_cstr(&sb), "one\ntwo\n");

    kit_buf_free(&sb);
    kit_command_free(&c);
}

TEST(command_capture_handles_output_larger_than_the_pipe_buffer) {
    KitCommand c = {0};
    kit_command_push_all(&c, "sh", "-c", "for i in $(seq 1 20000); do echo line$i; done", NULL);

    KitBuf sb = {0};
    CHECK(kit_command_capture(&c, &sb, NULL));
    CHECK(sb.count > 150000);
    CHECK(kit_str_starts_with_cstr(KIT_STR(kit_buf_cstr(&sb)), "line1\n"));
    CHECK(kit_str_ends_with_cstr(KIT_STR(kit_buf_cstr(&sb)), "line20000\n"));

    kit_buf_free(&sb);
    kit_command_free(&c);
}

TEST(command_capture_merged_interleaves_both_streams) {
    KitCommand c = {0};
    kit_command_push_all(&c, "sh", "-c", "echo out; echo err >&2; echo out2", NULL);

    KitBuf sb = {0};
    CHECK(kit_command_capture_merged(&c, &sb, NULL));
    CHECK(kit_str_contains(KIT_STR(kit_buf_cstr(&sb)), KIT_STR("out\n")));
    CHECK(kit_str_contains(KIT_STR(kit_buf_cstr(&sb)), KIT_STR("err\n")));
    CHECK(kit_str_contains(KIT_STR(kit_buf_cstr(&sb)), KIT_STR("out2\n")));

    /* Plain kit_command_capture must still leave stderr alone. */
    kit_buf_reset(&sb);
    kit_command_reset(&c);
    kit_command_push_all(&c, "sh", "-c", "echo out; echo err >&2", NULL);
    CHECK(kit_command_capture(&c, &sb, NULL));
    CHECK_STR(kit_buf_cstr(&sb), "out\n");

    /* Output is kept even when the child fails. */
    kit_buf_reset(&sb);
    kit_command_reset(&c);
    kit_command_push_all(&c, "sh", "-c", "echo partial >&2; exit 3", NULL);
    CHECK(!kit_command_capture_merged(&c, &sb, NULL));
    CHECK_STR(kit_buf_cstr(&sb), "partial\n");

    kit_buf_free(&sb);
    kit_command_free(&c);
}

TEST(command_capture_ex_separates_the_streams) {
    KitCommand c = {0};
    kit_command_push_all(&c, "sh", "-c", "echo to-out; echo to-err >&2", NULL);

    KitBuf out = {0}, err = {0};
    CHECK(kit_command_capture_split(&c, &out, &err, NULL));
    CHECK_STR(kit_buf_cstr(&out), "to-out\n");
    CHECK_STR(kit_buf_cstr(&err), "to-err\n");

    /* NULL leaves a stream alone. */
    kit_buf_reset(&out);
    CHECK(kit_command_capture_split(&c, &out, NULL, NULL));
    CHECK_STR(kit_buf_cstr(&out), "to-out\n");

    kit_buf_reset(&err);
    CHECK(kit_command_capture_split(&c, NULL, &err, NULL));
    CHECK_STR(kit_buf_cstr(&err), "to-err\n");

    /* The same builder for both is what kit_command_capture_merged does. */
    kit_buf_reset(&out);
    CHECK(kit_command_capture_split(&c, &out, &out, NULL));
    CHECK(kit_str_contains(KIT_STR(kit_buf_cstr(&out)), KIT_STR("to-out\n")));
    CHECK(kit_str_contains(KIT_STR(kit_buf_cstr(&out)), KIT_STR("to-err\n")));

    /* Output survives a failing child. */
    kit_buf_reset(&out); kit_buf_reset(&err);
    kit_command_reset(&c);
    kit_command_push_all(&c, "sh", "-c", "echo o; echo e >&2; exit 4", NULL);
    CHECK(!kit_command_capture_split(&c, &out, &err, NULL));
    CHECK_STR(kit_buf_cstr(&out), "o\n");
    CHECK_STR(kit_buf_cstr(&err), "e\n");

    kit_buf_free(&out); kit_buf_free(&err);
    kit_command_free(&c);
}

/* The reason the two pipes are drained together. A child that fills both well
 * past the pipe buffer deadlocks anything that reads one to the end first:
 * it blocks writing to the pipe nobody is draining. 4 MiB against a 64 KiB
 * buffer leaves no doubt. */
TEST(command_capture_ex_does_not_deadlock_on_a_full_pipe) {
    KitCommand c = {0};
    kit_command_push_all(&c, self_path, "--spew", NULL);

    KitBuf out = {0}, err = {0};
    KitTimer sw = kit_timer_start();
    CHECK(kit_command_capture_split(&c, &out, &err, NULL));
    double ms = kit_timer_ms(sw);

    CHECK_INT(out.count, spew_bytes());
    CHECK_INT(err.count, spew_bytes());
    CHECK(kit_str_starts_with_cstr(KIT_STR(kit_buf_cstr(&out)), "out "));
    CHECK(kit_str_starts_with_cstr(KIT_STR(kit_buf_cstr(&err)), "err "));
    if (!CHECK(ms < (UTEST_SANITIZED ? 30000.0 : 10000.0)))
        printf("      both streams took %.0f ms\n", ms);

    kit_buf_free(&out); kit_buf_free(&err);
    kit_command_free(&c);
}

TEST(command_run_reports_the_exit_status) {
    CHECK(kit_command_run_args("true", NULL));
    CHECK(!kit_command_run_args("false", NULL));
    CHECK(!kit_command_run_args("no-such-binary-anywhere", NULL));
}

TEST(command_reset_keeps_the_allocation) {
    KitCommand c = {0};
    kit_command_push_all(&c, "echo", "a", "b", NULL);
    CHECK_INT(c.count, 3);
    size_t cap = c.capacity;

    kit_command_reset(&c);
    CHECK_INT(c.count, 0);
    CHECK_INT(c.capacity, cap);

    kit_command_push(&c, "true");
    CHECK(kit_command_run(&c, NULL));
    CHECK_INT(c.count, 1);      /* the exec sentinel must not linger */
    kit_command_free(&c);
}

TEST(command_run_async_runs_children_in_parallel) {
    KitCommand a = {0}, b = {0};
    kit_command_push_all(&a, "sleep", "0.2", NULL);
    kit_command_push_all(&b, "sleep", "0.2", NULL);

    KitTimer sw = kit_timer_start();
    KitProcess pa = kit_command_spawn(&a, NULL);
    KitProcess pb = kit_command_spawn(&b, NULL);
    CHECK(kit_process_wait(pa, NULL));
    CHECK(kit_process_wait(pb, NULL));
    double ms = kit_timer_ms(sw);

    CHECK(ms >= 190.0);                  /* both really waited */
    CHECK(ms < (UTEST_SANITIZED ? 700.0 : 380.0));   /* but they overlapped */

    kit_command_free(&a);
    kit_command_free(&b);
}
#else /* _WIN32 */

/* The Windows command paths would otherwise be compiled and never run. cmd.exe
 * stands in for the shell; the assertions avoid exact matching because cmd
 * emits CRLF and leaves a space before a redirect. */
TEST(command_capture_on_windows) {
    KitCommand c = {0};
    kit_command_push_all(&c, "cmd", "/c", "echo to-out& echo to-err 1>&2", NULL);

    KitBuf out = {0}, err = {0};
    CHECK(kit_command_capture_split(&c, &out, &err, NULL));

    /* The point of two pipes: neither stream leaks into the other. */
    CHECK(kit_str_contains(KIT_STR(kit_buf_cstr(&out)), KIT_STR("to-out")));
    CHECK(kit_str_contains(KIT_STR(kit_buf_cstr(&err)), KIT_STR("to-err")));
    CHECK(!kit_str_contains(KIT_STR(kit_buf_cstr(&out)), KIT_STR("to-err")));
    CHECK(!kit_str_contains(KIT_STR(kit_buf_cstr(&err)), KIT_STR("to-out")));

    kit_buf_reset(&out);
    CHECK(kit_command_capture_merged(&c, &out, NULL));
    CHECK(kit_str_contains(KIT_STR(kit_buf_cstr(&out)), KIT_STR("to-out")));
    CHECK(kit_str_contains(KIT_STR(kit_buf_cstr(&out)), KIT_STR("to-err")));

    kit_buf_reset(&out);
    CHECK(kit_command_capture(&c, &out, NULL));
    CHECK(kit_str_contains(KIT_STR(kit_buf_cstr(&out)), KIT_STR("to-out")));
    CHECK(!kit_str_contains(KIT_STR(kit_buf_cstr(&out)), KIT_STR("to-err")));   /* passed through */

    kit_buf_free(&out); kit_buf_free(&err);
    kit_command_free(&c);
}

/* More than one pipe buffer on both streams at once, which is the case the
 * poll-free PeekNamedPipe loop exists for. */
TEST(command_capture_on_windows_does_not_deadlock) {
    KitCommand c = {0};
    kit_command_push_all(&c, self_path, "--spew", NULL);

    KitBuf out = {0}, err = {0};
    CHECK(kit_command_capture_split(&c, &out, &err, NULL));
    /* Text mode turns every \n into \r\n here, so the size is a floor. */
    CHECK(out.count >= spew_bytes());
    CHECK(err.count >= spew_bytes());
    CHECK(kit_str_starts_with_cstr(KIT_STR(kit_buf_cstr(&out)), "out "));
    CHECK(kit_str_starts_with_cstr(KIT_STR(kit_buf_cstr(&err)), "err "));

    kit_buf_free(&out); kit_buf_free(&err);
    kit_command_free(&c);
}

TEST(command_run_status_on_windows) {
    CHECK(kit_command_run_args("cmd", "/c", "exit 0", NULL));
    CHECK(!kit_command_run_args("cmd", "/c", "exit 3", NULL));
}

#endif /* !_WIN32 */

/* --- process failures ----------------------------------------------------- */

static bool failed_with(const KitError *err, KitErrorCode code, const char *fragment) {
    bool ok = (err->code == code) && strstr(err->message, fragment) != NULL;
    if (!ok)
        printf("      expected %s mentioning \"%s\", got %s: \"%s\"\n",
               kit_error_code_name(code), fragment,
               kit_error_code_name(err->code), err->message);
    return ok;
}

/* A program that is not installed used to surface as "exited with code 127",
 * which says nothing. The child now reports the exec failure through a
 * close-on-exec pipe, so the parent gives the real reason. */
TEST(command_missing_program_is_not_found) {
    KitCommand c = KIT_ZEROED;
    kit_command_push_all(&c, "no-such-binary-anywhere", "--version", NULL);

    KitError err = KIT_ZEROED;
    CHECK(kit_command_spawn(&c, &err) == KIT_PROCESS_INVALID);
    CHECK(failed_with(&err, KIT_ERR_NOT_FOUND, "no-such-binary-anywhere"));

    kit_error_clear(&err);
    CHECK(!kit_command_run(&c, &err));
    CHECK(failed_with(&err, KIT_ERR_NOT_FOUND, "no-such-binary-anywhere"));

    kit_error_clear(&err);
    KitBuf out = KIT_ZEROED;
    CHECK(!kit_command_capture(&c, &out, &err));
    CHECK(failed_with(&err, KIT_ERR_NOT_FOUND, "no-such-binary-anywhere"));
    CHECK_INT(out.count, 0);

    kit_buf_free(&out);
    kit_command_free(&c);
}

TEST(command_empty_is_invalid) {
    KitCommand c = KIT_ZEROED;
    KitError err = KIT_ZEROED;
    CHECK(kit_command_spawn(&c, &err) == KIT_PROCESS_INVALID);
    CHECK(failed_with(&err, KIT_ERR_INVALID, "empty command"));
    kit_command_free(&c);
}

TEST(process_wait_rejects_an_invalid_process) {
    KitError err = KIT_ZEROED;
    CHECK(!kit_process_wait(KIT_PROCESS_INVALID, &err));
    CHECK(failed_with(&err, KIT_ERR_INVALID, "no process"));
}

#ifndef _WIN32
TEST(command_exit_status_and_signal) {
    KitCommand c = KIT_ZEROED;
    kit_command_push_all(&c, "sh", "-c", "exit 3", NULL);

    KitError err = KIT_ZEROED;
    CHECK(!kit_command_run(&c, &err));
    CHECK(failed_with(&err, KIT_ERR_PROCESS, "exited with code 3"));
    CHECK_INT(err.native, 3);
    /* kit_command_run names the program, which kit_process_wait cannot know. */
    CHECK(kit_str_starts_with_cstr(KIT_STR(err.message), "running 'sh'"));

    kit_command_reset(&c);
    kit_command_push_all(&c, "sh", "-c", "kill -TERM $$", NULL);
    kit_error_clear(&err);
    CHECK(!kit_command_run(&c, &err));
    CHECK(failed_with(&err, KIT_ERR_PROCESS, "killed by signal 15"));
    CHECK_INT(err.native, 15);

    kit_command_free(&c);
}
#else
TEST(command_exit_status_on_windows) {
    KitError err = KIT_ZEROED;
    KitCommand c = KIT_ZEROED;
    kit_command_push_all(&c, "cmd", "/c", "exit 3", NULL);
    CHECK(!kit_command_run(&c, &err));
    CHECK(failed_with(&err, KIT_ERR_PROCESS, "exited with code 3"));
    CHECK_INT(err.native, 3);
    kit_command_free(&c);
}
#endif

/* --- option parsing failures ---------------------------------------------- */

TEST(cli_parse_failures_carry_a_message_for_the_user) {
    bool        verbose = false;
    const char *output  = "x";
    int         jobs    = 0;
    KitCliOpt opts[] = {
        KIT_CLI_FLAG('v', "verbose", "verbose", &verbose),
        KIT_CLI_STR ('o', "output", "FILE", "output", &output),
        KIT_CLI_INT ('j', "jobs", "N", "jobs", &jobs),
    };

    KitError err = KIT_ZEROED;
    { ARGV("--verbse");
      CHECK(!kit_cli_parse_arr(opts, &argc, &argv, &err));
      CHECK(failed_with(&err, KIT_ERR_INVALID, "unknown option '--verbse'")); }

    kit_error_clear(&err);
    { ARGV("-o");
      CHECK(!kit_cli_parse_arr(opts, &argc, &argv, &err));
      CHECK(failed_with(&err, KIT_ERR_INVALID, "requires an argument")); }

    kit_error_clear(&err);
    { ARGV("--jobs=abc");
      CHECK(!kit_cli_parse_arr(opts, &argc, &argv, &err));
      CHECK(failed_with(&err, KIT_ERR_INVALID, "expects an integer, got 'abc'")); }

    kit_error_clear(&err);
    { ARGV("--verbose=1");
      CHECK(!kit_cli_parse_arr(opts, &argc, &argv, &err));
      CHECK(failed_with(&err, KIT_ERR_INVALID, "takes no argument")); }

    kit_error_clear(&err);
    { ARGV("-vz");
      CHECK(!kit_cli_parse_arr(opts, &argc, &argv, &err));
      CHECK(failed_with(&err, KIT_ERR_INVALID, "unknown option '-z' in '-vz'")); }

    /* A group of one letter is not quoted back: it would only repeat itself. */
    kit_error_clear(&err);
    { ARGV("-z");
      CHECK(!kit_cli_parse_arr(opts, &argc, &argv, &err));
      CHECK(failed_with(&err, KIT_ERR_INVALID, "unknown option '-z'"));
      CHECK(strstr(err.message, " in ") == NULL); }
}

/* --- commands ------------------------------------------------------------- */

/* What kit_cli_usage prints for a table, as a heap string. Written in binary
 * mode so that Windows keeps the newlines the library wrote. */
static char *usage_text(const KitCliOpt *opts, size_t n_opts) {
    const char *path = "utest-tmp-usage.txt";
    FILE       *fp   = fopen(path, "wb");
    if (!fp) return NULL;
    kit_cli_usage(fp, "tool", opts, n_opts);
    fclose(fp);
    char *text = kit_fs_read(path, NULL);
    remove(path);
    return text;
}

/* The table is data a program can read back, so what each macro writes into
 * an entry is part of the contract. */
TEST(cli_command_tables_say_what_their_entries_are) {
    bool        flag = false, command = false;
    const char *text = "";
    int         number = 0;
    KitCliArgs  passed = KIT_ZEROED;

    KitCliOpt inner[] = {
        KIT_CLI_FLAG('f', "flag",   "a switch", &flag),
        KIT_CLI_STR ('s', "string", "S", "a string", &text),
        KIT_CLI_INT ('n', "number", "N", "a number", &number),
        KIT_CLI_REST("ARGS", "passed on", &passed),
    };
    KitCliOpt opts[] = {
        KIT_CLI_COMMAND     ("outer", "with a table", inner, &command),
        KIT_CLI_COMMAND_BARE("bare",  "without one",  &command),
    };

    CHECK(inner[0].type == KIT_CLI_OPT_FLAG);
    CHECK(inner[1].type == KIT_CLI_OPT_STR);
    CHECK(inner[2].type == KIT_CLI_OPT_INT);
    CHECK(inner[0].opts == NULL && inner[0].n_opts == 0);

    CHECK(inner[3].type == KIT_CLI_OPT_REST);
    CHECK(inner[3].long_name == NULL);
    CHECK_STR(inner[3].meta, "ARGS");
    CHECK(inner[3].dst == &passed);

    CHECK(opts[0].type == KIT_CLI_OPT_COMMAND);
    CHECK_STR(opts[0].long_name, "outer");
    CHECK(opts[0].opts == inner);
    CHECK_INT(opts[0].n_opts, 4);
    CHECK(opts[1].type == KIT_CLI_OPT_COMMAND);
    CHECK(opts[1].opts == NULL);
    CHECK_INT(opts[1].n_opts, 0);
}

TEST(cli_commands_choose_the_table_that_reads_the_rest) {
    bool verbose = false, release = false, build = false, clean = false;

    KitCliOpt build_opts[] = {
        KIT_CLI_FLAG('r', "release", "release", &release),
    };
    KitCliOpt opts[] = {
        KIT_CLI_FLAG        ('v', "verbose", "verbose", &verbose),
        KIT_CLI_COMMAND     ("build", "build", build_opts, &build),
        KIT_CLI_COMMAND_BARE("clean", "clean", &clean),
    };

    {   /* global options before, the command's own after, its arguments left */
        ARGV("-v", "build", "-r", "a.c", "b.c");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK(verbose); CHECK(build); CHECK(release); CHECK(!clean);
        CHECK_INT(argc, 2);
        CHECK_STR(argv[0], "a.c");
        CHECK_STR(argv[1], "b.c");
    }
    {   /* the enclosing table still applies once the command is named */
        verbose = release = build = false;
        ARGV("build", "a.c", "--verbose", "-rv");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK(build); CHECK(verbose); CHECK(release);
        CHECK_INT(argc, 1);
        CHECK_STR(argv[0], "a.c");
    }
    {   /* a bare command takes the enclosing options and nothing more */
        verbose = false;
        ARGV("clean", "-v");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK(clean); CHECK(verbose);
        CHECK_INT(argc, 0);
    }
    {   /* naming no command is not an error: what it means is the program's call */
        verbose = build = clean = false;
        ARGV("-v");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK(verbose); CHECK(!build); CHECK(!clean);
        CHECK_INT(argc, 0);
    }
}

TEST(cli_commands_nest_and_the_innermost_name_wins) {
    bool        remote = false, add = false, all = false, force = false;
    const char *format = "text";

    KitCliOpt add_opts[] = {
        KIT_CLI_FLAG('f', "force", "force", &force),
    };
    KitCliOpt remote_opts[] = {
        KIT_CLI_FLAG   ('a', "all", "all", &all),
        KIT_CLI_COMMAND("add", "add", add_opts, &add),
    };
    KitCliOpt opts[] = {
        KIT_CLI_STR    ('f', "format", "FMT", "format", &format),
        KIT_CLI_COMMAND("remote", "remote", remote_opts, &remote),
    };

    {   /* both enclosing tables reach the innermost command */
        ARGV("remote", "add", "-a", "--format=json", "origin", "-f");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK(remote); CHECK(add); CHECK(all);
        CHECK_STR(format, "json");
        CHECK(force);                       /* add's own -f, not the root's */
        CHECK_INT(argc, 1);
        CHECK_STR(argv[0], "origin");
    }
    {   /* before the command, the same letter is still the root's */
        remote = add = force = false;
        ARGV("-f", "yaml", "remote", "add");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK_STR(format, "yaml");
        CHECK(!force);
    }
    {   /* a table with commands wants one before any positional argument */
        KitError err = KIT_ZEROED;
        ARGV("remote", "origin");
        CHECK(!kit_cli_parse_arr(opts, &argc, &argv, &err));
        CHECK(failed_with(&err, KIT_ERR_INVALID, "unknown command 'origin'"));
    }
}

TEST(cli_commands_refuse_what_their_tables_do_not_know) {
    bool release = false, build = false;
    KitCliOpt build_opts[] = { KIT_CLI_FLAG('r', "release", "release", &release) };
    KitCliOpt opts[]       = { KIT_CLI_COMMAND("build", "build", build_opts, &build) };

    KitError err = KIT_ZEROED;
    { ARGV("biuld");
      CHECK(!kit_cli_parse_arr(opts, &argc, &argv, &err));
      CHECK(failed_with(&err, KIT_ERR_INVALID, "unknown command 'biuld'")); }

    /* A command's options come after it. */
    kit_error_clear(&err);
    { ARGV("--release", "build");
      CHECK(!kit_cli_parse_arr(opts, &argc, &argv, &err));
      CHECK(failed_with(&err, KIT_ERR_INVALID, "unknown option '--release'")); }

    /* A command is named as a word, never as an option. */
    kit_error_clear(&err);
    { ARGV("--build");
      CHECK(!kit_cli_parse_arr(opts, &argc, &argv, &err));
      CHECK(failed_with(&err, KIT_ERR_INVALID, "unknown option '--build'")); }

    /* A positional argument where the command should be. */
    kit_error_clear(&err);
    { ARGV("a.c", "build");
      CHECK(!kit_cli_parse_arr(opts, &argc, &argv, &err));
      CHECK(failed_with(&err, KIT_ERR_INVALID, "unknown command 'a.c'")); }
}

/* A table that leads back to itself would let the line nest commands for as
 * long as it goes on. The bound turns that into an error, and the usage,
 * which follows the same chain, stops at the same place. */
TEST(cli_commands_nest_to_a_bound) {
    bool      again = false;
    KitCliOpt loop[1];
    loop[0] = (KitCliOpt)KIT_CLI_COMMAND("again", "again", loop, &again);

    {   ARGV("again", "again", "again", "again", "again", "again", "again", "again",
             "again", "again", "again", "again", "again", "again", "again");
        CHECK(kit_cli_parse_arr(loop, &argc, &argv, NULL));
        CHECK(again);
    }
    {   KitError err = KIT_ZEROED;
        ARGV("again", "again", "again", "again", "again", "again", "again", "again",
             "again", "again", "again", "again", "again", "again", "again", "again");
        CHECK(!kit_cli_parse_arr(loop, &argc, &argv, &err));
        CHECK(failed_with(&err, KIT_ERR_RANGE, "cannot nest more than 16 deep"));
    }

    char *text = usage_text(loop, KIT_COUNTOF(loop));
    if (CHECK(text != NULL)) CHECK(strstr(text, "Usage: tool again again ") != NULL);
    free(text);
}

TEST(cli_rest_keeps_apart_what_follows_the_double_dash) {
    bool       verbose = false, run = false;
    KitCliArgs passed  = KIT_ZEROED;

    KitCliOpt run_opts[] = {
        KIT_CLI_REST("ARGS", "args", &passed),
    };
    KitCliOpt opts[] = {
        KIT_CLI_FLAG   ('v', "verbose", "verbose", &verbose),
        KIT_CLI_COMMAND("run", "run", run_opts, &run),
    };

    {   /* the command's own arguments on one side, the rest on the other, and
         * the rest still ends on argv's NULL, which is what exec expects */
        const char *storage[] = { "run", "a.c", "--", "--port", "8080", NULL };
        int    argc = 5;
        char **argv = (char **)(void *)storage;
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK(run);
        CHECK_INT(argc, 1);
        CHECK_STR(argv[0], "a.c");
        if (CHECK_INT(passed.count, 2)) {
            CHECK_STR(passed.items[0], "--port");
            CHECK_STR(passed.items[1], "8080");
            CHECK(passed.items[2] == NULL);
        }
    }
    {   /* "--" and nothing after it: an empty list, not a missing one */
        passed.items = NULL; passed.count = 0;
        ARGV("run", "--");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK(passed.items != NULL);
        CHECK_INT(passed.count, 0);
        CHECK_INT(argc, 0);
    }
    {   /* with no REST in force they join the positional arguments, and what
         * follows "--" is never a command either */
        passed.items = NULL; passed.count = 0; run = false;
        ARGV("-v", "--", "run", "-v");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
        CHECK(!run);
        CHECK(passed.items == NULL);
        CHECK_INT(argc, 2);
        CHECK_STR(argv[0], "run");
        CHECK_STR(argv[1], "-v");
    }

    /* A REST in an enclosing table applies inside its commands, as an option
     * would. */
    bool       build = false;
    KitCliArgs outer = KIT_ZEROED;
    KitCliOpt  root[] = {
        KIT_CLI_REST        ("ARGS", "args", &outer),
        KIT_CLI_COMMAND_BARE("build", "build", &build),
    };
    {   ARGV("build", "x.c", "--", "-O2");
        CHECK(kit_cli_parse_arr(root, &argc, &argv, NULL));
        CHECK(build);
        CHECK_INT(argc, 1);
        if (CHECK_INT(outer.count, 1)) CHECK_STR(outer.items[0], "-O2");
    }
}

TEST(cli_usage_without_commands_is_unchanged) {
    bool        verbose = false;
    const char *output  = "a.out";
    KitCliOpt opts[] = {
        KIT_CLI_FLAG('v', "verbose", "Say more", &verbose),
        KIT_CLI_STR (0,   "output",  "FILE", "Where to write", &output),
    };

    char *text = usage_text(opts, KIT_COUNTOF(opts));
    CHECK_STR(text, "Usage: tool [options] ...\n"
                    "\n"
                    "Options:\n"
                    "  -v, --verbose                      Say more\n"
                    "      --output=<FILE>                Where to write\n");
    free(text);
}

TEST(cli_usage_follows_the_command_the_parse_chose) {
    bool       verbose = false, recurse = false, release = false, build = false, run = false;
    KitCliArgs passed  = KIT_ZEROED;

    KitCliOpt build_opts[] = {
        KIT_CLI_FLAG('r', "release", "Build with optimisations", &release),
        KIT_CLI_REST("ARGS", "Handed to the compiler", &passed),
    };
    KitCliOpt opts[] = {
        KIT_CLI_FLAG        ('v', "verbose", "Say more",  &verbose),
        KIT_CLI_FLAG        ('r', "recurse", "Go deeper", &recurse),
        KIT_CLI_COMMAND     ("build", "Compile it", build_opts, &build),
        KIT_CLI_COMMAND_BARE("run",   "Run it",     &run),
    };

    /* Nothing named yet: the commands, then the options. */
    char *text = usage_text(opts, KIT_COUNTOF(opts));
    if (CHECK(text != NULL)) {
        CHECK(strstr(text, "Usage: tool [options] [<command> ...]\n") != NULL);
        CHECK(strstr(text, "\nCommands:\n  build ") != NULL);
        CHECK(strstr(text, " Compile it\n  run ") != NULL);
        CHECK(strstr(text, "\nOptions:\n  -v, --verbose ") != NULL);
        CHECK(strstr(text, "Global options") == NULL);
    }
    free(text);

    /* Once "build" is named, the usage is the command's. The root's -r is not
     * listed under the letter build has taken over, only under its name. */
    {   ARGV("build");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
    }
    text = usage_text(opts, KIT_COUNTOF(opts));
    if (CHECK(text != NULL)) {
        CHECK(strstr(text, "Usage: tool build [options] ... [-- <ARGS>...]\n") != NULL);
        CHECK(strstr(text, "\nOptions:\n  -r, --release ") != NULL);
        CHECK(strstr(text, "\n      -- <ARGS>... ") != NULL);
        CHECK(strstr(text, "\nGlobal options:\n  -v, --verbose ") != NULL);
        CHECK(strstr(text, "\n      --recurse ") != NULL);
        CHECK(strstr(text, "Commands:") == NULL);
    }
    free(text);

    /* A bare command has no options of its own to list. */
    build = false;
    {   ARGV("run");
        CHECK(kit_cli_parse_arr(opts, &argc, &argv, NULL));
    }
    text = usage_text(opts, KIT_COUNTOF(opts));
    if (CHECK(text != NULL)) {
        CHECK(strstr(text, "Usage: tool run [options] ...\n") != NULL);
        CHECK(strstr(text, "\nOptions:") == NULL);
        CHECK(strstr(text, "\nGlobal options:\n  -v, --verbose ") != NULL);
        CHECK(strstr(text, "  -r, --recurse ") != NULL);
    }
    free(text);
}

/* --- stopwatch ------------------------------------------------------------ */

TEST(timer_measures_forward) {
    KitTimer sw = kit_timer_start();
    volatile double sink = 0;
    for (int i = 0; i < 2000000; i++) sink += i;
    KIT_UNUSED(sink);

    double ms = kit_timer_ms(sw);
    CHECK(ms > 0.0);
    CHECK_DBL(kit_timer_s(sw) * 1000.0, ms, 50.0);
    CHECK(kit_timer_ms(sw) >= ms);    /* monotonic */
}

/* --- maths ---------------------------------------------------------------- */

TEST(scalar_helpers) {
    CHECK_DBL(kit_clampf(5.0f, 0.0f, 1.0f), 1.0f, 1e-6);
    CHECK_DBL(kit_clampf(-5.0f, 0.0f, 1.0f), 0.0f, 1e-6);
    CHECK_DBL(kit_clampd(0.5, 0.0, 1.0), 0.5, 1e-12);
    CHECK_INT(kit_clampi(42, 0, 10), 10);

    CHECK_DBL(kit_lerpf(0.0f, 10.0f, 0.25f), 2.5f, 1e-6);
    CHECK_DBL(kit_remapf(5.0f, 0.0f, 10.0f, 0.0f, 100.0f), 50.0f, 1e-4);
    CHECK_DBL(kit_remapf(5.0f, 3.0f, 3.0f, 7.0f, 9.0f), 7.0f, 1e-6);  /* zero span */

    CHECK_INT(KIT_MIN(3, 7), 3);
    CHECK_INT(KIT_MAX(3, 7), 7);
    CHECK_DBL(KIT_DEG2RAD(180.0f), 3.14159265f, 1e-5);
    CHECK_DBL(KIT_RAD2DEG(KIT_DEG2RAD(90.0f)), 90.0f, 1e-4);
}

TEST(vector_helpers) {
    KitVec2 a = KIT_VEC2(3, 4);
    CHECK_DBL(kit_vec2_len(a), 5.0f, 1e-5);
    CHECK_DBL(kit_vec2_len(kit_vec2_norm(a)), 1.0f, 1e-5);
    CHECK_DBL(kit_vec2_len(kit_vec2_norm(KIT_VEC2(0, 0))), 0.0f, 1e-6);   /* no division by 0 */
    CHECK_DBL(kit_vec2_dot(KIT_VEC2(1, 0), KIT_VEC2(0, 1)), 0.0f, 1e-6);
    CHECK_DBL(kit_vec2_dist(KIT_VEC2(0, 0), KIT_VEC2(3, 4)), 5.0f, 1e-5);
    CHECK_DBL(kit_vec2_add(a, KIT_VEC2(1, 1)).x, 4.0f, 1e-6);
    CHECK_DBL(kit_vec2_sub(a, KIT_VEC2(1, 1)).y, 3.0f, 1e-6);
    CHECK_DBL(kit_vec2_mul(a, KIT_VEC2(2, 2)).x, 6.0f, 1e-6);
    CHECK_DBL(kit_vec2_scale(a, 2.0f).y, 8.0f, 1e-6);

    KitVec3 x = KIT_VEC3(1, 0, 0), y = KIT_VEC3(0, 1, 0);
    KitVec3 z = kit_vec3_cross(x, y);
    CHECK_DBL(z.z, 1.0f, 1e-6);
    CHECK_DBL(kit_vec3_dot(x, y), 0.0f, 1e-6);
    CHECK_DBL(kit_vec3_len(KIT_VEC3(2, 3, 6)), 7.0f, 1e-5);
    CHECK_DBL(kit_vec3_len(kit_vec3_norm(KIT_VEC3(2, 3, 6))), 1.0f, 1e-5);
    CHECK_DBL(kit_vec3_len(kit_vec3_norm(KIT_VEC3(0, 0, 0))), 0.0f, 1e-6);
    CHECK_DBL(kit_vec3_add(x, y).y, 1.0f, 1e-6);
    CHECK_DBL(kit_vec3_sub(x, y).y, -1.0f, 1e-6);
    CHECK_DBL(kit_vec3_mul(KIT_VEC3(2, 3, 4), KIT_VEC3(2, 2, 2)).z, 8.0f, 1e-6);
    CHECK_DBL(kit_vec3_scale(KIT_VEC3(1, 2, 3), 3.0f).z, 9.0f, 1e-6);
}

int main(int argc, char **argv) {
    /* Re-entered as its own child by the deadlock tests. */
    if (argc == 2 && strcmp(argv[1], "--spew") == 0) return spew();
    self_path = argv[0];

    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("system");
    RUN(cli_parse_every_supported_form);
    RUN(cli_parse_grouped_short_flags);
    RUN(cli_parse_positionals_and_double_dash);
    RUN(cli_parse_rejects_bad_input);
    RUN(cli_usage_prints_long_names_in_full);
    RUN(cli_shift_consumes_left_to_right);
#ifndef _WIN32
    RUN(command_capture_collects_stdout);
    RUN(command_capture_handles_output_larger_than_the_pipe_buffer);
    RUN(command_capture_merged_interleaves_both_streams);
    RUN(command_capture_ex_separates_the_streams);
    RUN(command_capture_ex_does_not_deadlock_on_a_full_pipe);
    RUN(command_run_reports_the_exit_status);
    RUN(command_reset_keeps_the_allocation);
    RUN(command_run_async_runs_children_in_parallel);
#else
    RUN(command_capture_on_windows);
    RUN(command_capture_on_windows_does_not_deadlock);
    RUN(command_run_status_on_windows);
#endif
    RUN(command_missing_program_is_not_found);
    RUN(command_empty_is_invalid);
    RUN(process_wait_rejects_an_invalid_process);
#ifndef _WIN32
    RUN(command_exit_status_and_signal);
#else
    RUN(command_exit_status_on_windows);
#endif
    RUN(cli_parse_failures_carry_a_message_for_the_user);
    RUN(cli_command_tables_say_what_their_entries_are);
    RUN(cli_commands_choose_the_table_that_reads_the_rest);
    RUN(cli_commands_nest_and_the_innermost_name_wins);
    RUN(cli_commands_refuse_what_their_tables_do_not_know);
    RUN(cli_commands_nest_to_a_bound);
    RUN(cli_rest_keeps_apart_what_follows_the_double_dash);
    RUN(cli_usage_without_commands_is_unchanged);
    RUN(cli_usage_follows_the_command_the_parse_chose);
    RUN(timer_measures_forward);
    RUN(scalar_helpers);
    RUN(vector_helpers);
    return utest_report();
}
