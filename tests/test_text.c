/*
 * String views, string builder, hashing and path helpers.
 * Includes the regression test for the path_dirname / path_join overflow.
 */

#define UTILS_IMPLEMENTATION
#include "../utils.h"
#include "utest.h"

/* path_join inserts PATH_SEP, which is a backslash on Windows. */
#ifdef _WIN32
#    define SEP "\\"
#else
#    define SEP "/"
#endif

/* --- string views --------------------------------------------------------- */

TEST(sv_construction_and_equality) {
    String_View a = SV("hello");
    CHECK_INT(a.count, 5);
    CHECK_SV(a, "hello");
    CHECK(sv_eq(a, SV_LIT("hello")));
    CHECK(sv_eq_cstr(a, "hello"));
    CHECK(!sv_eq_cstr(a, "hell"));
    CHECK(!sv_eq_cstr(a, "hello!"));

    String_View empty = SV("");
    CHECK_INT(empty.count, 0);
    CHECK(sv_eq(empty, empty));
}

TEST(sv_trim) {
    CHECK_SV(sv_trim(SV("  \t hi \n ")), "hi");
    CHECK_SV(sv_trim_left(SV("  hi  ")), "hi  ");
    CHECK_SV(sv_trim_right(SV("  hi  ")), "  hi");
    CHECK_INT(sv_trim(SV("     ")).count, 0);   /* all whitespace */
    CHECK_INT(sv_trim(SV("")).count, 0);
}

TEST(sv_chop_by_delim) {
    String_View line = SV("a,bb,,ccc");
    CHECK_SV(sv_chop_by_delim(&line, ','), "a");
    CHECK_SV(sv_chop_by_delim(&line, ','), "bb");
    CHECK_SV(sv_chop_by_delim(&line, ','), "");
    CHECK_SV(sv_chop_by_delim(&line, ','), "ccc");
    CHECK_INT(line.count, 0);

    /* No delimiter: the whole view is consumed. */
    String_View one = SV("whole");
    CHECK_SV(sv_chop_by_delim(&one, ';'), "whole");
    CHECK_INT(one.count, 0);
}

TEST(sv_chop_left_clamps) {
    String_View sv = SV("abcdef");
    CHECK_SV(sv_chop_left(&sv, 2), "ab");
    CHECK_SV(sv, "cdef");
    CHECK_SV(sv_chop_left(&sv, 999), "cdef");   /* clamped, not overrun */
    CHECK_INT(sv.count, 0);
}

TEST(sv_prefix_and_suffix) {
    String_View sv = SV("libutils.so");
    CHECK(sv_starts_with_cstr(sv, "lib"));
    CHECK(sv_ends_with_cstr(sv, ".so"));
    CHECK(!sv_starts_with_cstr(sv, "bin"));
    CHECK(sv_starts_with_cstr(sv, ""));           /* empty prefix always matches */
    CHECK(!sv_starts_with_cstr(sv, "libutils.so.extra"));  /* longer than sv */
    CHECK(sv_ends_with(sv, sv));
}

TEST(sv_search) {
    String_View sv = SV("hello world, hello again");

    CHECK_INT(sv_index_of(sv, 'w'), 6);
    CHECK_INT(sv_index_of(sv, 'h'), 0);
    CHECK_INT(sv_index_of(sv, 'z'), SV_NPOS);
    CHECK_INT(sv_index_of(SV(""), 'a'), SV_NPOS);

    CHECK_INT(sv_index_of_sv(sv, SV("world")), 6);
    CHECK_INT(sv_index_of_sv(sv, SV("hello")), 0);      /* first match */
    CHECK_INT(sv_index_of_sv(sv, SV("nowhere")), SV_NPOS);
    CHECK_INT(sv_index_of_sv(sv, SV("")), 0);           /* empty needle */
    CHECK_INT(sv_index_of_sv(SV("ab"), SV("abc")), SV_NPOS);  /* longer than sv */
    CHECK_INT(sv_index_of_sv(sv, SV("again")), 19);     /* at the very end */

    CHECK(sv_contains(sv, SV("world")));
    CHECK(!sv_contains(sv, SV("World")));
}

TEST(sv_eq_ignorecase_is_ascii_only) {
    CHECK(sv_eq_ignorecase(SV("Hello"), SV("hELLO")));
    CHECK(sv_eq_ignorecase(SV(""), SV("")));
    CHECK(!sv_eq_ignorecase(SV("hello"), SV("hell")));
    CHECK(!sv_eq_ignorecase(SV("hello"), SV("hallo")));
    CHECK(sv_eq_ignorecase(SV("MiXeD123"), SV("mixed123")));
}

TEST(sv_chop_by_sv_handles_multichar_delimiters) {
    String_View text = SV("one::two::three");
    CHECK_SV(sv_chop_by_sv(&text, SV("::")), "one");
    CHECK_SV(sv_chop_by_sv(&text, SV("::")), "two");
    CHECK_SV(sv_chop_by_sv(&text, SV("::")), "three");
    CHECK_INT(text.count, 0);

    /* Absent delimiter: everything comes back at once. */
    String_View one = SV("no delimiter here");
    CHECK_SV(sv_chop_by_sv(&one, SV("::")), "no delimiter here");
    CHECK_INT(one.count, 0);

    /* Adjacent delimiters produce empty fields, they are not collapsed. */
    String_View empties = SV("a::::b");
    CHECK_SV(sv_chop_by_sv(&empties, SV("::")), "a");
    CHECK_SV(sv_chop_by_sv(&empties, SV("::")), "");
    CHECK_SV(sv_chop_by_sv(&empties, SV("::")), "b");
}

TEST(sv_chop_right_clamps) {
    String_View sv = SV("abcdef");
    CHECK_SV(sv_chop_right(&sv, 2), "ef");
    CHECK_SV(sv, "abcd");
    CHECK_SV(sv_chop_right(&sv, 999), "abcd");
    CHECK_INT(sv.count, 0);
}

/* The loop must terminate on every shape of input, and must not collapse the
 * empty fields that sit between two delimiters. */
TEST(sv_try_chop_by_delim_field_counts) {
    struct { const char *input; int expected; } cases[] = {
        { "a,b",    2 },
        { "a,b,",   2 },   /* a trailing delimiter adds no field */
        { "",       0 },   /* an empty view yields nothing */
        { ",",      1 },
        { "a,,b",   3 },   /* an empty field in the middle is kept */
        { ",,",     2 },
        { "solo",   1 },
    };

    for (size_t i = 0; i < UTILS_ARRAY_LEN(cases); i++) {
        String_View line = SV(cases[i].input);
        String_View field;
        int n = 0;
        while (sv_try_chop_by_delim(&line, ',', &field)) n++;
        if (!CHECK_INT(n, cases[i].expected))
            printf("      input was \"%s\"\n", cases[i].input);
    }
}

TEST(sv_to_integers_is_strict) {
    int64_t i = 0;
    CHECK(sv_to_i64(SV("42"), &i));        CHECK_INT(i, 42);
    CHECK(sv_to_i64(SV("-42"), &i));       CHECK_INT(i, -42);
    CHECK(sv_to_i64(SV("+7"), &i));        CHECK_INT(i, 7);
    CHECK(sv_to_i64(SV("0"), &i));         CHECK_INT(i, 0);
    CHECK(sv_to_i64(SV("007"), &i));       CHECK_INT(i, 7);

    CHECK(sv_to_i64(SV("9223372036854775807"), &i));
    CHECK(i == INT64_MAX);
    CHECK(sv_to_i64(SV("-9223372036854775808"), &i));   /* one past INT64_MAX */
    CHECK(i == INT64_MIN);

    /* Everything below must fail, and none of it may be half-parsed. */
    CHECK(!sv_to_i64(SV(""), &i));
    CHECK(!sv_to_i64(SV("-"), &i));
    CHECK(!sv_to_i64(SV("12x"), &i));
    CHECK(!sv_to_i64(SV(" 12"), &i));       /* no implicit trimming */
    CHECK(!sv_to_i64(SV("12 "), &i));
    CHECK(!sv_to_i64(SV("1.5"), &i));
    CHECK(!sv_to_i64(SV("0x10"), &i));
    CHECK(!sv_to_i64(SV("9223372036854775808"), &i));    /* overflow */
    CHECK(!sv_to_i64(SV("-9223372036854775809"), &i));

    uint64_t u = 0;
    CHECK(sv_to_u64(SV("18446744073709551615"), &u));
    CHECK(u == UINT64_MAX);
    CHECK(!sv_to_u64(SV("18446744073709551616"), &u));   /* overflow */
    CHECK(!sv_to_u64(SV("-1"), &u));                     /* not unsigned */

    /* A failed parse must leave the destination alone. */
    i = 999;
    CHECK(!sv_to_i64(SV("bad"), &i));
    CHECK_INT(i, 999);
}

TEST(sv_to_double_is_strict) {
    double d = 0;
    CHECK(sv_to_double(SV("1.5"), &d));     CHECK_DBL(d, 1.5, 1e-12);
    CHECK(sv_to_double(SV("-0.25"), &d));   CHECK_DBL(d, -0.25, 1e-12);
    CHECK(sv_to_double(SV("1e3"), &d));     CHECK_DBL(d, 1000.0, 1e-9);
    CHECK(sv_to_double(SV("42"), &d));      CHECK_DBL(d, 42.0, 1e-12);

    CHECK(!sv_to_double(SV(""), &d));
    CHECK(!sv_to_double(SV("1.5x"), &d));
    CHECK(!sv_to_double(SV("abc"), &d));
    CHECK(!sv_to_double(SV("1e999"), &d));   /* overflows to infinity */

    /* Longer than the internal buffer: refused, never truncated. */
    char long_number[128];
    memset(long_number, '1', sizeof(long_number));
    CHECK(!sv_to_double(sv_from_parts(long_number, sizeof(long_number)), &d));
}

TEST(sv_to_cstr_and_from_parts) {
    /* A view into a larger buffer, with no terminator of its own. */
    const char *backing = "prefix-PAYLOAD-suffix";
    String_View sv = sv_from_parts(backing + 7, 7);
    CHECK_SV(sv, "PAYLOAD");

    char *owned = sv_to_cstr(sv);
    if (CHECK(owned != NULL)) {
        CHECK_STR(owned, "PAYLOAD");
        CHECK_INT(strlen(owned), 7);
        free(owned);
    }

    char *empty = sv_to_cstr(sv_from_parts(NULL, 0));
    if (CHECK(empty != NULL)) { CHECK_STR(empty, ""); free(empty); }
}

/* --- string builder ------------------------------------------------------- */

TEST(sb_appends) {
    StringBuilder sb = {0};
    sb_append(&sb, "abc");
    sb_append_char(&sb, '-');
    sb_append_n(&sb, "defXX", 3);
    sb_append_sv(&sb, SV("-ghi"));
    sb_appendf(&sb, "-%d-%s", 42, "end");

    CHECK_STR(sb_cstr(&sb), "abc-def-ghi-42-end");
    CHECK_INT(sb.count, strlen("abc-def-ghi-42-end"));

    char *owned = sb_to_string(&sb);
    CHECK_STR(owned, "abc-def-ghi-42-end");
    free(owned);

    sb_reset(&sb);
    CHECK_INT(sb.count, 0);
    CHECK_STR(sb_cstr(&sb), "");
    sb_free(&sb);
    CHECK(sb.items == NULL);
}

TEST(sb_grows_past_the_initial_capacity) {
    StringBuilder sb = {0};
    for (int i = 0; i < 5000; i++) sb_appendf(&sb, "%04d", i);
    CHECK_INT(sb.count, 20000);
    CHECK_INT(strlen(sb_cstr(&sb)), 20000);
    CHECK(strncmp(sb.items, "000000010002", 12) == 0);
    sb_free(&sb);
}

/* --- hashing -------------------------------------------------------------- */

TEST(hash_is_stable_and_discriminating) {
    CHECK_INT(hash_str("utils"), hash_str("utils"));
    CHECK(hash_str("utils") != hash_str("utils "));
    CHECK_INT(hash_str("abc"), hash_bytes("abc", 3));
    CHECK_INT(hash_str(""), 5381u);   /* djb2 seed */

    /* Raw djb2 leaves too little entropy in the low bits to index a
     * power-of-two table; hash_mix32 is what makes them usable. */
    CHECK_INT(hash_mix32(0), 0u);
    CHECK_INT(hash_mix32(hash_str("utils")), hash_mix32(hash_str("utils")));
    {
        enum { CAP = 4096 };
        static int raw[CAP], mixed[CAP];
        char k[16];
        for (int i = 0; i < 3000; i++) {
            snprintf(k, sizeof(k), "key%d", i);
            raw[hash_str(k) & (CAP - 1)]++;
            mixed[hash_mix32(hash_str(k)) & (CAP - 1)]++;
        }
        int raw_used = 0, mixed_used = 0;
        for (int i = 0; i < CAP; i++) {
            if (raw[i])   raw_used++;
            if (mixed[i]) mixed_used++;
        }
        CHECK(mixed_used > raw_used);
        CHECK(mixed_used > 2000);   /* ~2126 expected for a uniform hash */
    }

    /* No collision across a small dense keyspace. */
    enum { N = 4096 };
    static uint32_t seen[N];
    char key[16];
    int collisions = 0;
    for (int i = 0; i < N; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        seen[i] = hash_str(key);
        for (int j = 0; j < i; j++) if (seen[j] == seen[i]) collisions++;
    }
    CHECK_INT(collisions, 0);
}

/* --- paths ---------------------------------------------------------------- */

TEST(path_basename_and_ext) {
    CHECK_STR(path_basename("/usr/lib/libc.so"), "libc.so");
    CHECK_STR(path_basename("plain.txt"), "plain.txt");
    CHECK_STR(path_basename("/trailing/"), "trailing/");
    CHECK_STR(path_basename("/"), "/");

    CHECK_STR(path_ext("archive.tar.gz"), ".gz");
    CHECK_STR(path_ext("/etc/hostname"), "");        /* no extension */
    CHECK_STR(path_ext("/etc.d/hostname"), "");      /* dot in a parent only */
}

TEST(path_dirname_cases) {
    char buf[64];
    CHECK_STR(path_dirname("/usr/lib/libc.so", buf, sizeof(buf)), "/usr/lib");
    CHECK_STR(path_dirname("relative.txt", buf, sizeof(buf)), ".");
    CHECK_STR(path_dirname("/file", buf, sizeof(buf)), "/");
    CHECK_STR(path_dirname("a//b//c", buf, sizeof(buf)), "a//b");
}

TEST(path_join_cases) {
    char buf[64];
    CHECK_STR(path_join(buf, sizeof(buf), "/usr", "lib"), "/usr" SEP "lib");
    CHECK_STR(path_join(buf, sizeof(buf), "/usr/", "lib"), "/usr/lib");
    CHECK_STR(path_join(buf, sizeof(buf), "/usr", "/lib"), "/usr" SEP "lib");
    CHECK_STR(path_join(buf, sizeof(buf), "/usr/", "///lib"), "/usr/lib");
    CHECK_STR(path_join(buf, sizeof(buf), "/usr", ""), "/usr");
    CHECK_STR(path_join(buf, sizeof(buf), "", "lib"), "lib");
}

/* Regression: "bufsz - 1" wrapped around on an empty buffer and the following
 * memcpy wrote past the destination. Run under AddressSanitizer to catch it. */
TEST(path_helpers_respect_tiny_buffers) {
    char guard[8];

    memset(guard, '#', sizeof(guard));
    path_dirname("/usr/lib/libc.so", guard, 0);
    CHECK_INT(guard[0], '#');            /* untouched */

    memset(guard, '#', sizeof(guard));
    path_join(guard, 0, "/usr", "lib");
    CHECK_INT(guard[0], '#');

    char one[1];
    CHECK_STR(path_dirname("/usr/lib", one, sizeof(one)), "");
    CHECK_STR(path_join(one, sizeof(one), "/usr", "lib"), "");

    char small[5];
    CHECK_STR(path_dirname("/usr/lib/libc.so", small, sizeof(small)), "/usr");
    CHECK_STR(path_join(small, sizeof(small), "/usr", "lib"), "/usr");

    char two[2];
    CHECK_STR(path_dirname("relative.txt", two, sizeof(two)), ".");
}

TEST(path_is_absolute_cases) {
    CHECK(path_is_absolute("/etc"));
    CHECK(!path_is_absolute("etc"));
    CHECK(!path_is_absolute(""));
    CHECK(!path_is_absolute("./etc"));
}

int main(void) {
    log_set_level(LOG_CRITICAL);
    utest_begin("text");
    RUN(sv_construction_and_equality);
    RUN(sv_trim);
    RUN(sv_chop_by_delim);
    RUN(sv_chop_left_clamps);
    RUN(sv_prefix_and_suffix);
    RUN(sv_search);
    RUN(sv_eq_ignorecase_is_ascii_only);
    RUN(sv_chop_by_sv_handles_multichar_delimiters);
    RUN(sv_chop_right_clamps);
    RUN(sv_try_chop_by_delim_field_counts);
    RUN(sv_to_integers_is_strict);
    RUN(sv_to_double_is_strict);
    RUN(sv_to_cstr_and_from_parts);
    RUN(sb_appends);
    RUN(sb_grows_past_the_initial_capacity);
    RUN(hash_is_stable_and_discriminating);
    RUN(path_basename_and_ext);
    RUN(path_dirname_cases);
    RUN(path_join_cases);
    RUN(path_helpers_respect_tiny_buffers);
    RUN(path_is_absolute_cases);
    return utest_report();
}
