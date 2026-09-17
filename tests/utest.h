/*
 * utest.h - micro test framework, single header, no dependency.
 *
 * SPDX-License-Identifier: Unlicense
 * Public domain, like the library it tests. See LICENSE.
 *
 * USAGE:
 *   #include "utest.h"
 *
 *   TEST(addition) {
 *       CHECK_INT(2 + 2, 4);
 *       CHECK(2 < 3);
 *   }
 *
 *   int main(void) {
 *       utest_begin("arithmetic");
 *       RUN(addition);
 *       return utest_report();
 *   }
 *
 * Every CHECK is a soft assertion: it records the failure and lets the test
 * carry on, so one run reports every problem instead of only the first. Each
 * CHECK also evaluates to the outcome, which is how a test bails out before
 * dereferencing something it just proved to be NULL:
 *
 *       if (!CHECK(ptr != NULL)) return;
 *
 * The exit status is 0 only when every test passed, so `make test` fails the
 * build on the first regression.
 */

#ifndef UTEST_H
#define UTEST_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#    include <io.h>
#    define UTEST__ISATTY() (_isatty(_fileno(stdout)) != 0)
#else
#    include <unistd.h>
#    define UTEST__ISATTY() (isatty(fileno(stdout)) != 0)
#endif

#if defined(__GNUC__) || defined(__clang__)
#    define UTEST__UNUSED __attribute__((unused))
#else
#    define UTEST__UNUSED
#endif

/* A sanitized build runs several times slower, so any test that asserts on a
 * wall-clock budget has to widen it rather than report a false regression. */
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#    define UTEST_SANITIZED 1
#elif defined(__has_feature)
#    if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) \
     || __has_feature(memory_sanitizer)
#        define UTEST_SANITIZED 1
#    endif
#endif
#ifndef UTEST_SANITIZED
#    define UTEST_SANITIZED 0
#endif

/* --- state ---------------------------------------------------------------- */

static const char *utest__suite      = "";
static int         utest__tests      = 0;
static int         utest__failed     = 0;
static int         utest__checks     = 0;
static int         utest__cur_fails  = 0;
static bool        utest__color      = false;

#define UTEST__RED   (utest__color ? "\x1b[31m" : "")
#define UTEST__GREEN (utest__color ? "\x1b[32m" : "")
#define UTEST__DIM   (utest__color ? "\x1b[90m" : "")
#define UTEST__RST   (utest__color ? "\x1b[0m"  : "")

static void utest_begin(const char *suite) {
    utest__suite = suite;
    utest__color = UTEST__ISATTY();
    printf("%s== suite: %s%s\n", UTEST__DIM, suite, UTEST__RST);
}

static void utest__run(const char *name, void (*fn)(void)) {
    utest__tests++;
    utest__cur_fails = 0;
    printf("  %-40s", name);
    fflush(stdout);
    fn();
    if (utest__cur_fails == 0) {
        printf(" %sok%s\n", UTEST__GREEN, UTEST__RST);
    } else {
        utest__failed++;
        printf("  %s(%d failed)%s\n", UTEST__RED, utest__cur_fails, UTEST__RST);
    }
}

static int utest_report(void) {
    printf("%s-- %s: %d tests, %d checks, %d failed%s\n\n",
           utest__failed ? UTEST__RED : UTEST__GREEN,
           utest__suite, utest__tests, utest__checks, utest__failed, UTEST__RST);
    return utest__failed == 0 ? 0 : 1;
}

/* --- assertions ----------------------------------------------------------- */

static bool utest__fail(const char *file, int line, const char *fmt, ...) {
    utest__cur_fails++;
    if (utest__cur_fails == 1) printf("\n");
    va_list ap;
    printf("    %sFAIL%s %s:%d  ", UTEST__RED, UTEST__RST, file, line);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    return false;
}

UTEST__UNUSED static bool utest__check(bool ok, const char *expr,
                                       const char *file, int line) {
    utest__checks++;
    if (ok) return true;
    return utest__fail(file, line, "%s", expr);
}

UTEST__UNUSED static bool utest__check_int(long long got, long long want,
                                           const char *expr,
                                           const char *file, int line) {
    utest__checks++;
    if (got == want) return true;
    return utest__fail(file, line, "%s == %lld, got %lld", expr, want, got);
}

UTEST__UNUSED static bool utest__check_str(const char *got, const char *want,
                                           const char *expr,
                                           const char *file, int line) {
    utest__checks++;
    if (got && want && strcmp(got, want) == 0) return true;
    return utest__fail(file, line, "%s == \"%s\", got \"%s\"",
                       expr, want ? want : "(null)", got ? got : "(null)");
}

UTEST__UNUSED static bool utest__check_dbl(double got, double want, double eps,
                                           const char *expr,
                                           const char *file, int line) {
    utest__checks++;
    if (fabs(got - want) <= eps) return true;
    return utest__fail(file, line, "%s == %g (+/-%g), got %g", expr, want, eps, got);
}

#define TEST(name) static void test_##name(void)
#define RUN(name)  utest__run(#name, test_##name)

#define CHECK(expr) \
    utest__check((expr) ? true : false, #expr, __FILE__, __LINE__)
#define CHECK_INT(got, want) \
    utest__check_int((long long)(got), (long long)(want), #got, __FILE__, __LINE__)
#define CHECK_STR(got, want) \
    utest__check_str((got), (want), #got, __FILE__, __LINE__)
#define CHECK_DBL(got, want, eps) \
    utest__check_dbl((double)(got), (double)(want), (eps), #got, __FILE__, __LINE__)

/* Available once kit.h has been included, for KitStr comparisons. */
#ifdef KIT_H
#define CHECK_KITSTR(got, want_cstr)                                        \
    utest__check_str(utest__kitstr_cstr(got), (want_cstr), #got,            \
                     __FILE__, __LINE__)

UTEST__UNUSED static const char *utest__kitstr_cstr(KitStr sv) {
    static char buf[256];
    size_t n = sv.count < sizeof(buf) - 1 ? sv.count : sizeof(buf) - 1;
    if (sv.data) memcpy(buf, sv.data, n); else n = 0;
    buf[n] = '\0';
    return buf;
}
#endif

#endif /* UTEST_H */
