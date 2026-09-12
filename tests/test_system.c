/*
 * CLI option parsing, command execution, stopwatch and maths.
 * Includes the regression test for the truncated opts_usage column.
 */

#define UTILS_IMPLEMENTATION
#include "../utils.h"
#include "utest.h"

/* Builds an argv the way main() receives it, minus argv[0].
 * The one cast keeps string literals const while handing opts_parse the char**
 * it expects; the parser only reorders the pointers, never the characters. */
#define ARGV(...)                                                       \
    const char *argv_storage[] = { __VA_ARGS__ };                       \
    int    argc = (int)UTILS_ARRAY_LEN(argv_storage);                   \
    char **argv = (char **)(void *)argv_storage

/* --- option parsing ------------------------------------------------------- */

TEST(opts_parse_every_supported_form) {
    bool        verbose = false;
    const char *output  = "default";
    int         jobs    = 1;

    Opt opts[] = {
        OPT_FLAG('v', "verbose", "verbose", &verbose),
        OPT_STR ('o', "output", "FILE", "output", &output),
        OPT_INT ('j', "jobs", "N", "jobs", &jobs),
    };

    {   /* short with a separate value, and an attached one */
        ARGV("-v", "-o", "sep.txt", "-j4", "file.c");
        CHECK(opts_parse_arr(opts, &argc, &argv));
        CHECK(verbose);
        CHECK_STR(output, "sep.txt");
        CHECK_INT(jobs, 4);
        CHECK_INT(argc, 1);
        CHECK_STR(argv[0], "file.c");
    }
    {   /* '=' separators, short and long */
        ARGV("-o=eq.txt", "--jobs=8");
        CHECK(opts_parse_arr(opts, &argc, &argv));
        CHECK_STR(output, "eq.txt");
        CHECK_INT(jobs, 8);
        CHECK_INT(argc, 0);
    }
    {   /* long with a separate value */
        ARGV("--output", "long.txt", "--verbose");
        CHECK(opts_parse_arr(opts, &argc, &argv));
        CHECK_STR(output, "long.txt");
        CHECK_INT(argc, 0);
    }
    {   /* negative integers */
        ARGV("--jobs=-3");
        CHECK(opts_parse_arr(opts, &argc, &argv));
        CHECK_INT(jobs, -3);
    }
}

TEST(opts_parse_grouped_short_flags) {
    bool        a = false, b = false, c = false;
    const char *output = "default";
    int         jobs   = 0;

    Opt opts[] = {
        OPT_FLAG('a', "all",    "a", &a),
        OPT_FLAG('b', "brief",  "b", &b),
        OPT_FLAG('c', "colour", "c", &c),
        OPT_STR ('o', "output", "FILE", "output", &output),
        OPT_INT ('j', "jobs",   "N",    "jobs",   &jobs),
    };

    {   /* the whole point: -abc means -a -b -c */
        ARGV("-abc", "rest.c");
        CHECK(opts_parse_arr(opts, &argc, &argv));
        CHECK(a); CHECK(b); CHECK(c);
        CHECK_INT(argc, 1);
        CHECK_STR(argv[0], "rest.c");
    }
    {   /* a group ending on a value option, value attached */
        a = b = false; output = "default";
        ARGV("-abofile.txt");
        CHECK(opts_parse_arr(opts, &argc, &argv));
        CHECK(a); CHECK(b);
        CHECK_STR(output, "file.txt");
    }
    {   /* a group ending on a value option, value in the next argument */
        a = false; jobs = 0;
        ARGV("-aj", "12");
        CHECK(opts_parse_arr(opts, &argc, &argv));
        CHECK(a);
        CHECK_INT(jobs, 12);
    }
    {   /* '=' still works at the end of a group */
        a = false; output = "default";
        ARGV("-ao=eq.txt");
        CHECK(opts_parse_arr(opts, &argc, &argv));
        CHECK(a);
        CHECK_STR(output, "eq.txt");
    }
    {   /* an unknown letter names itself, not the whole group */
        ARGV("-azc");
        CHECK(!opts_parse_arr(opts, &argc, &argv));
    }
    {   /* a flag inside a group still refuses a value */
        ARGV("-ab=1");
        CHECK(!opts_parse_arr(opts, &argc, &argv));
    }
    {   /* a value option at the end of a group with nothing left */
        ARGV("-ao");
        CHECK(!opts_parse_arr(opts, &argc, &argv));
    }
}

TEST(opts_parse_positionals_and_double_dash) {
    bool verbose = false;
    Opt opts[] = { OPT_FLAG('v', "verbose", "verbose", &verbose) };

    ARGV("first", "-v", "second", "--", "-v", "--not-an-option", "-");
    CHECK(opts_parse_arr(opts, &argc, &argv));
    CHECK(verbose);
    CHECK_INT(argc, 5);
    CHECK_STR(argv[0], "first");
    CHECK_STR(argv[1], "second");
    CHECK_STR(argv[2], "-v");            /* after --, options are positional */
    CHECK_STR(argv[3], "--not-an-option");
    CHECK_STR(argv[4], "-");             /* a lone dash is positional */
}

TEST(opts_parse_rejects_bad_input) {
    bool verbose = false;
    const char *output = "x";
    int jobs = 0;
    Opt opts[] = {
        OPT_FLAG('v', "verbose", "verbose", &verbose),
        OPT_STR ('o', "output", "FILE", "output", &output),
        OPT_INT ('j', "jobs", "N", "jobs", &jobs),
    };

    { ARGV("--unknown");    CHECK(!opts_parse_arr(opts, &argc, &argv)); }
    { ARGV("-z");           CHECK(!opts_parse_arr(opts, &argc, &argv)); }
    { ARGV("--output");     CHECK(!opts_parse_arr(opts, &argc, &argv)); }  /* value missing */
    { ARGV("--verbose=1");  CHECK(!opts_parse_arr(opts, &argc, &argv)); }  /* flag takes none */
    { ARGV("--jobs=abc");   CHECK(!opts_parse_arr(opts, &argc, &argv)); }
    { ARGV("--jobs=12x");   CHECK(!opts_parse_arr(opts, &argc, &argv)); }
    { ARGV("--jobs=99999999999999999999"); CHECK(!opts_parse_arr(opts, &argc, &argv)); }
}

/* Regression: the help column was rendered into a 48-byte buffer, which cut
 * long option names in half. */
TEST(opts_usage_prints_long_names_in_full) {
    const char *out = "x";
    Opt opts[] = {
        OPT_STR('x', "an-extremely-long-option-name-that-overflows-the-column",
                "A_VERY_LONG_METAVARIABLE_NAME", "the help text", &out),
    };

    const char *path = "utest-tmp-usage.txt";
    FILE *fp = fopen(path, "w");
    if (!CHECK(fp != NULL)) return;
    opts_usage_arr(fp, "prog", opts);
    fclose(fp);

    char *text = read_file(path);
    if (!CHECK(text != NULL)) return;
    CHECK(strstr(text, "--an-extremely-long-option-name-that-overflows-the-column"
                       "=<A_VERY_LONG_METAVARIABLE_NAME>") != NULL);
    CHECK(strstr(text, "the help text") != NULL);
    CHECK(strstr(text, "Usage: prog") != NULL);
    free(text);
    remove(path);
}

TEST(args_shift_consumes_left_to_right) {
    ARGV("prog", "a", "b");
    CHECK_STR(args_shift(&argc, &argv), "prog");
    CHECK_STR(args_shift(&argc, &argv), "a");
    CHECK_STR(args_shift(&argc, &argv), "b");
    CHECK(args_shift(&argc, &argv) == NULL);
    CHECK_INT(argc, 0);
}

/* --- command execution ---------------------------------------------------- */

#ifndef _WIN32
TEST(cmd_capture_collects_stdout) {
    Cmd c = {0};
    cmd_extend(&c, "printf", "one\ntwo\n", NULL);

    StringBuilder sb = {0};
    CHECK(cmd_capture(&c, &sb));
    CHECK_STR(sb_cstr(&sb), "one\ntwo\n");

    sb_free(&sb);
    cmd_free(&c);
}

TEST(cmd_capture_handles_output_larger_than_the_pipe_buffer) {
    Cmd c = {0};
    cmd_extend(&c, "sh", "-c", "for i in $(seq 1 20000); do echo line$i; done", NULL);

    StringBuilder sb = {0};
    CHECK(cmd_capture(&c, &sb));
    CHECK(sb.count > 150000);
    CHECK(sv_starts_with_cstr(SV(sb_cstr(&sb)), "line1\n"));
    CHECK(sv_ends_with_cstr(SV(sb_cstr(&sb)), "line20000\n"));

    sb_free(&sb);
    cmd_free(&c);
}

TEST(cmd_run_reports_the_exit_status) {
    CHECK(cmd_run_args("true", NULL));
    CHECK(!cmd_run_args("false", NULL));
    CHECK(!cmd_run_args("no-such-binary-anywhere", NULL));
}

TEST(cmd_reset_keeps_the_allocation) {
    Cmd c = {0};
    cmd_extend(&c, "echo", "a", "b", NULL);
    CHECK_INT(c.count, 3);
    size_t cap = c.capacity;

    cmd_reset(&c);
    CHECK_INT(c.count, 0);
    CHECK_INT(c.capacity, cap);

    cmd_append(&c, "true");
    CHECK(cmd_run(&c));
    CHECK_INT(c.count, 1);      /* the exec sentinel must not linger */
    cmd_free(&c);
}

TEST(cmd_run_async_runs_children_in_parallel) {
    Cmd a = {0}, b = {0};
    cmd_extend(&a, "sleep", "0.2", NULL);
    cmd_extend(&b, "sleep", "0.2", NULL);

    Stopwatch sw = sw_start();
    Proc pa = cmd_run_async(&a);
    Proc pb = cmd_run_async(&b);
    CHECK(proc_wait(pa));
    CHECK(proc_wait(pb));
    double ms = sw_elapsed_ms(sw);

    CHECK(ms >= 190.0);                  /* both really waited */
    CHECK(ms < (UTEST_SANITIZED ? 700.0 : 380.0));   /* but they overlapped */

    cmd_free(&a);
    cmd_free(&b);
}
#endif /* !_WIN32 */

/* --- stopwatch ------------------------------------------------------------ */

TEST(stopwatch_measures_forward) {
    Stopwatch sw = sw_start();
    volatile double sink = 0;
    for (int i = 0; i < 2000000; i++) sink += i;
    UTILS_UNUSED(sink);

    double ms = sw_elapsed_ms(sw);
    CHECK(ms > 0.0);
    CHECK_DBL(sw_elapsed_s(sw) * 1000.0, ms, 50.0);
    CHECK(sw_elapsed_ms(sw) >= ms);    /* monotonic */
}

/* --- maths ---------------------------------------------------------------- */

TEST(scalar_helpers) {
    CHECK_DBL(clampf(5.0f, 0.0f, 1.0f), 1.0f, 1e-6);
    CHECK_DBL(clampf(-5.0f, 0.0f, 1.0f), 0.0f, 1e-6);
    CHECK_DBL(clampd(0.5, 0.0, 1.0), 0.5, 1e-12);
    CHECK_INT(clampi(42, 0, 10), 10);

    CHECK_DBL(lerpf(0.0f, 10.0f, 0.25f), 2.5f, 1e-6);
    CHECK_DBL(map_range(5.0f, 0.0f, 10.0f, 0.0f, 100.0f), 50.0f, 1e-4);
    CHECK_DBL(map_range(5.0f, 3.0f, 3.0f, 7.0f, 9.0f), 7.0f, 1e-6);  /* zero span */

    CHECK_INT(UTILS_MIN(3, 7), 3);
    CHECK_INT(UTILS_MAX(3, 7), 7);
    CHECK_DBL(DEG2RAD(180.0f), 3.14159265f, 1e-5);
    CHECK_DBL(RAD2DEG(DEG2RAD(90.0f)), 90.0f, 1e-4);
}

TEST(vector_helpers) {
    Vec2 a = V2(3, 4);
    CHECK_DBL(vec2_len(a), 5.0f, 1e-5);
    CHECK_DBL(vec2_len(vec2_norm(a)), 1.0f, 1e-5);
    CHECK_DBL(vec2_len(vec2_norm(V2(0, 0))), 0.0f, 1e-6);   /* no division by 0 */
    CHECK_DBL(vec2_dot(V2(1, 0), V2(0, 1)), 0.0f, 1e-6);
    CHECK_DBL(vec2_dist(V2(0, 0), V2(3, 4)), 5.0f, 1e-5);
    CHECK_DBL(vec2_add(a, V2(1, 1)).x, 4.0f, 1e-6);
    CHECK_DBL(vec2_sub(a, V2(1, 1)).y, 3.0f, 1e-6);
    CHECK_DBL(vec2_mul(a, V2(2, 2)).x, 6.0f, 1e-6);
    CHECK_DBL(vec2_scale(a, 2.0f).y, 8.0f, 1e-6);

    Vec3 x = V3(1, 0, 0), y = V3(0, 1, 0);
    Vec3 z = vec3_cross(x, y);
    CHECK_DBL(z.z, 1.0f, 1e-6);
    CHECK_DBL(vec3_dot(x, y), 0.0f, 1e-6);
    CHECK_DBL(vec3_len(V3(2, 3, 6)), 7.0f, 1e-5);
    CHECK_DBL(vec3_len(vec3_norm(V3(2, 3, 6))), 1.0f, 1e-5);
    CHECK_DBL(vec3_len(vec3_norm(V3(0, 0, 0))), 0.0f, 1e-6);
    CHECK_DBL(vec3_add(x, y).y, 1.0f, 1e-6);
    CHECK_DBL(vec3_sub(x, y).y, -1.0f, 1e-6);
    CHECK_DBL(vec3_mul(V3(2, 3, 4), V3(2, 2, 2)).z, 8.0f, 1e-6);
    CHECK_DBL(vec3_scale(V3(1, 2, 3), 3.0f).z, 9.0f, 1e-6);
}

int main(void) {
    log_set_level(LOG_CRITICAL);
    utest_begin("system");
    RUN(opts_parse_every_supported_form);
    RUN(opts_parse_grouped_short_flags);
    RUN(opts_parse_positionals_and_double_dash);
    RUN(opts_parse_rejects_bad_input);
    RUN(opts_usage_prints_long_names_in_full);
    RUN(args_shift_consumes_left_to_right);
#ifndef _WIN32
    RUN(cmd_capture_collects_stdout);
    RUN(cmd_capture_handles_output_larger_than_the_pipe_buffer);
    RUN(cmd_run_reports_the_exit_status);
    RUN(cmd_reset_keeps_the_allocation);
    RUN(cmd_run_async_runs_children_in_parallel);
#endif
    RUN(stopwatch_measures_forward);
    RUN(scalar_helpers);
    RUN(vector_helpers);
    return utest_report();
}
