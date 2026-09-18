/*
 * The benchmark harness. Timings cannot be asserted on, so what is checked is
 * everything around them: that the body runs the number of times promised
 * plus one warm-up, that the figures are consistent with each other, and that
 * a degenerate call comes back rather than dividing by zero.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

typedef struct {
    size_t calls;
    size_t spin;      /* how much work each call does */
    double sink;      /* somewhere for the result to go, so it is not elided */
} Work;

static void body(void *context) {
    Work *w = (Work *)context;
    w->calls++;
    for (size_t i = 0; i < w->spin; i++) w->sink += (double)i * 0.5;
}

TEST(the_body_runs_once_more_than_it_is_sampled) {
    Work     w = KIT_ZEROED;
    KitBench b = kit_bench_run("warm-up", 5, body, &w);

    CHECK_INT(w.calls, 6);          /* five samples and the warm-up */
    CHECK_INT(b.samples, 5);
    CHECK_STR(b.name, "warm-up");
}

TEST(the_figures_agree_with_each_other) {
    Work     w = { 0, 2000, 0.0 };
    KitBench b = kit_bench_run("spin", 20, body, &w);

    CHECK(b.min_ms <= b.mean_ms);
    CHECK(b.mean_ms <= b.max_ms);
    CHECK(b.min_ms >= 0.0);

    /* The mean is the total over the samples, whichever way it was worked
     * out, within what floating point costs to say so. */
    CHECK_DBL(b.mean_ms, b.total_ms / 20.0, 1e-9);
    CHECK(b.total_ms >= b.max_ms);
}

TEST(a_degenerate_call_still_comes_back) {
    Work     w = KIT_ZEROED;
    KitBench none = kit_bench_run("nothing", 10, NULL, &w);

    CHECK_INT(w.calls, 0);
    CHECK_INT(none.samples, 0);
    CHECK_DBL(none.mean_ms, 0.0, 0.0);
    CHECK_STR(none.name, "nothing");

    /* No name, and no samples asked for: one sample, and a label to print. */
    KitBench once = kit_bench_run(NULL, 0, body, &w);
    CHECK_INT(once.samples, 1);
    CHECK_INT(w.calls, 2);          /* the warm-up, then the one sample */
    CHECK_STR(once.name, "unnamed");
}

TEST(the_report_names_the_run_and_its_numbers) {
    Work     w = { 0, 500, 0.0 };
    KitBench b = kit_bench_run("counting", 3, body, &w);

    char  text[256] = KIT_ZEROED;
    FILE *tmp = tmpfile();
    if (!CHECK(tmp != NULL)) return;

    kit_bench_report(tmp, &b);
    rewind(tmp);
    size_t n = fread(text, 1, sizeof text - 1, tmp);
    text[n] = '\0';
    fclose(tmp);

    CHECK(strstr(text, "counting") != NULL);
    CHECK(strstr(text, "3 samples") != NULL);
    CHECK(strstr(text, "min ") != NULL);
    CHECK(strstr(text, "mean ") != NULL);
    CHECK(strstr(text, "max ") != NULL);
}

/* A body that does more work cannot come out faster. Ten times the work with
 * a factor of two to spare, so that a loaded machine does not fail the run. */
TEST(more_work_measures_as_more_time) {
    Work light = { 0, 20000, 0.0 };
    Work heavy = { 0, 200000, 0.0 };

    KitBench a = kit_bench_run("light", 10, body, &light);
    KitBench b = kit_bench_run("heavy", 10, body, &heavy);

    CHECK(b.min_ms > a.min_ms * 2.0);
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("bench");
    RUN(the_body_runs_once_more_than_it_is_sampled);
    RUN(the_figures_agree_with_each_other);
    RUN(a_degenerate_call_still_comes_back);
    RUN(the_report_names_the_run_and_its_numbers);
    RUN(more_work_measures_as_more_time);
    return utest_report();
}
