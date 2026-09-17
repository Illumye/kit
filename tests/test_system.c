/*
 * CLI option parsing, command execution, stopwatch and maths.
 * Includes the regression test for the truncated kit_cli_usage column.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

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
    kit_command_push_all(&c, "sh", "-c",
               "yes 'stdout line padding padding padding' | head -c 4000000; "
               "yes 'stderr line padding padding padding' | head -c 4000000 >&2",
               NULL);

    KitBuf out = {0}, err = {0};
    KitTimer sw = kit_timer_start();
    CHECK(kit_command_capture_split(&c, &out, &err, NULL));
    double ms = kit_timer_ms(sw);

    CHECK_INT(out.count, 4000000);
    CHECK_INT(err.count, 4000000);
    CHECK(kit_str_starts_with_cstr(KIT_STR(kit_buf_cstr(&out)), "stdout line"));
    CHECK(kit_str_starts_with_cstr(KIT_STR(kit_buf_cstr(&err)), "stderr line"));
    if (!CHECK(ms < (UTEST_SANITIZED ? 30000.0 : 10000.0)))
        printf("      8 MiB took %.0f ms\n", ms);

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
    kit_command_push_all(&c, "cmd", "/c",
               "for /L %i in (1,1,4000) do @(echo out padding padding padding"
               "& echo err padding padding padding 1>&2)", NULL);

    KitBuf out = {0}, err = {0};
    CHECK(kit_command_capture_split(&c, &out, &err, NULL));
    CHECK(out.count > 100000);
    CHECK(err.count > 100000);

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

int main(void) {
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
    RUN(timer_measures_forward);
    RUN(scalar_helpers);
    RUN(vector_helpers);
    return utest_report();
}
