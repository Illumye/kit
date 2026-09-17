/*
 * String views, string builder, hashing and path helpers.
 * Includes the regression test for the kit_path_dirname / kit_path_join overflow.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

/* kit_path_join inserts KIT_PATH_SEP, which is a backslash on Windows. */
#ifdef _WIN32
#    define SEP "\\"
#else
#    define SEP "/"
#endif

/* --- string views --------------------------------------------------------- */

TEST(str_construction_and_equality) {
    KitStr a = KIT_STR("hello");
    CHECK_INT(a.count, 5);
    CHECK_KITSTR(a, "hello");
    CHECK(kit_str_eq(a, KIT_STR_LIT("hello")));
    CHECK(kit_str_eq_cstr(a, "hello"));
    CHECK(!kit_str_eq_cstr(a, "hell"));
    CHECK(!kit_str_eq_cstr(a, "hello!"));

    KitStr empty = KIT_STR("");
    CHECK_INT(empty.count, 0);
    CHECK(kit_str_eq(empty, empty));
}

TEST(str_trim) {
    CHECK_KITSTR(kit_str_trim(KIT_STR("  \t hi \n ")), "hi");
    CHECK_KITSTR(kit_str_trim_left(KIT_STR("  hi  ")), "hi  ");
    CHECK_KITSTR(kit_str_trim_right(KIT_STR("  hi  ")), "  hi");
    CHECK_INT(kit_str_trim(KIT_STR("     ")).count, 0);   /* all whitespace */
    CHECK_INT(kit_str_trim(KIT_STR("")).count, 0);
}

TEST(str_cut) {
    KitStr line = KIT_STR("a,bb,,ccc");
    CHECK_KITSTR(kit_str_cut(&line, ','), "a");
    CHECK_KITSTR(kit_str_cut(&line, ','), "bb");
    CHECK_KITSTR(kit_str_cut(&line, ','), "");
    CHECK_KITSTR(kit_str_cut(&line, ','), "ccc");
    CHECK_INT(line.count, 0);

    /* No delimiter: the whole view is consumed. */
    KitStr one = KIT_STR("whole");
    CHECK_KITSTR(kit_str_cut(&one, ';'), "whole");
    CHECK_INT(one.count, 0);
}

TEST(str_chop_left_clamps) {
    KitStr sv = KIT_STR("abcdef");
    CHECK_KITSTR(kit_str_take(&sv, 2), "ab");
    CHECK_KITSTR(sv, "cdef");
    CHECK_KITSTR(kit_str_take(&sv, 999), "cdef");   /* clamped, not overrun */
    CHECK_INT(sv.count, 0);
}

TEST(str_prefix_and_suffix) {
    KitStr sv = KIT_STR("libutils.so");
    CHECK(kit_str_starts_with_cstr(sv, "lib"));
    CHECK(kit_str_ends_with_cstr(sv, ".so"));
    CHECK(!kit_str_starts_with_cstr(sv, "bin"));
    CHECK(kit_str_starts_with_cstr(sv, ""));           /* empty prefix always matches */
    CHECK(!kit_str_starts_with_cstr(sv, "libutils.so.extra"));  /* longer than sv */
    CHECK(kit_str_ends_with(sv, sv));
}

TEST(str_search) {
    KitStr sv = KIT_STR("hello world, hello again");

    CHECK_INT(kit_str_find_char(sv, 'w'), 6);
    CHECK_INT(kit_str_find_char(sv, 'h'), 0);
    CHECK_INT(kit_str_find_char(sv, 'z'), KIT_NPOS);
    CHECK_INT(kit_str_find_char(KIT_STR(""), 'a'), KIT_NPOS);

    CHECK_INT(kit_str_find(sv, KIT_STR("world")), 6);
    CHECK_INT(kit_str_find(sv, KIT_STR("hello")), 0);      /* first match */
    CHECK_INT(kit_str_find(sv, KIT_STR("nowhere")), KIT_NPOS);
    CHECK_INT(kit_str_find(sv, KIT_STR("")), 0);           /* empty needle */
    CHECK_INT(kit_str_find(KIT_STR("ab"), KIT_STR("abc")), KIT_NPOS);  /* longer than sv */
    CHECK_INT(kit_str_find(sv, KIT_STR("again")), 19);     /* at the very end */

    CHECK(kit_str_contains(sv, KIT_STR("world")));
    CHECK(!kit_str_contains(sv, KIT_STR("World")));
}

TEST(str_eq_ignorecase_is_ascii_only) {
    CHECK(kit_str_eq_nocase(KIT_STR("Hello"), KIT_STR("hELLO")));
    CHECK(kit_str_eq_nocase(KIT_STR(""), KIT_STR("")));
    CHECK(!kit_str_eq_nocase(KIT_STR("hello"), KIT_STR("hell")));
    CHECK(!kit_str_eq_nocase(KIT_STR("hello"), KIT_STR("hallo")));
    CHECK(kit_str_eq_nocase(KIT_STR("MiXeD123"), KIT_STR("mixed123")));
}

TEST(str_chop_by_sv_handles_multichar_delimiters) {
    KitStr text = KIT_STR("one::two::three");
    CHECK_KITSTR(kit_str_cut_str(&text, KIT_STR("::")), "one");
    CHECK_KITSTR(kit_str_cut_str(&text, KIT_STR("::")), "two");
    CHECK_KITSTR(kit_str_cut_str(&text, KIT_STR("::")), "three");
    CHECK_INT(text.count, 0);

    /* Absent delimiter: everything comes back at once. */
    KitStr one = KIT_STR("no delimiter here");
    CHECK_KITSTR(kit_str_cut_str(&one, KIT_STR("::")), "no delimiter here");
    CHECK_INT(one.count, 0);

    /* Adjacent delimiters produce empty fields, they are not collapsed. */
    KitStr empties = KIT_STR("a::::b");
    CHECK_KITSTR(kit_str_cut_str(&empties, KIT_STR("::")), "a");
    CHECK_KITSTR(kit_str_cut_str(&empties, KIT_STR("::")), "");
    CHECK_KITSTR(kit_str_cut_str(&empties, KIT_STR("::")), "b");
}

TEST(str_chop_right_clamps) {
    KitStr sv = KIT_STR("abcdef");
    CHECK_KITSTR(kit_str_take_right(&sv, 2), "ef");
    CHECK_KITSTR(sv, "abcd");
    CHECK_KITSTR(kit_str_take_right(&sv, 999), "abcd");
    CHECK_INT(sv.count, 0);
}

/* The loop must terminate on every shape of input, and must not collapse the
 * empty fields that sit between two delimiters. */
TEST(str_try_chop_by_delim_field_counts) {
    struct { const char *input; int expected; } cases[] = {
        { "a,b",    2 },
        { "a,b,",   2 },   /* a trailing delimiter adds no field */
        { "",       0 },   /* an empty view yields nothing */
        { ",",      1 },
        { "a,,b",   3 },   /* an empty field in the middle is kept */
        { ",,",     2 },
        { "solo",   1 },
    };

    for (size_t i = 0; i < KIT_COUNTOF(cases); i++) {
        KitStr line = KIT_STR(cases[i].input);
        KitStr field;
        int n = 0;
        while (kit_str_next(&line, ',', &field)) n++;
        if (!CHECK_INT(n, cases[i].expected))
            printf("      input was \"%s\"\n", cases[i].input);
    }
}

TEST(str_to_integers_is_strict) {
    int64_t i = 0;
    CHECK(kit_str_to_i64(KIT_STR("42"), &i));        CHECK_INT(i, 42);
    CHECK(kit_str_to_i64(KIT_STR("-42"), &i));       CHECK_INT(i, -42);
    CHECK(kit_str_to_i64(KIT_STR("+7"), &i));        CHECK_INT(i, 7);
    CHECK(kit_str_to_i64(KIT_STR("0"), &i));         CHECK_INT(i, 0);
    CHECK(kit_str_to_i64(KIT_STR("007"), &i));       CHECK_INT(i, 7);

    CHECK(kit_str_to_i64(KIT_STR("9223372036854775807"), &i));
    CHECK(i == INT64_MAX);
    CHECK(kit_str_to_i64(KIT_STR("-9223372036854775808"), &i));   /* one past INT64_MAX */
    CHECK(i == INT64_MIN);

    /* Everything below must fail, and none of it may be half-parsed. */
    CHECK(!kit_str_to_i64(KIT_STR(""), &i));
    CHECK(!kit_str_to_i64(KIT_STR("-"), &i));
    CHECK(!kit_str_to_i64(KIT_STR("12x"), &i));
    CHECK(!kit_str_to_i64(KIT_STR(" 12"), &i));       /* no implicit trimming */
    CHECK(!kit_str_to_i64(KIT_STR("12 "), &i));
    CHECK(!kit_str_to_i64(KIT_STR("1.5"), &i));
    CHECK(!kit_str_to_i64(KIT_STR("0x10"), &i));
    CHECK(!kit_str_to_i64(KIT_STR("9223372036854775808"), &i));    /* overflow */
    CHECK(!kit_str_to_i64(KIT_STR("-9223372036854775809"), &i));

    uint64_t u = 0;
    CHECK(kit_str_to_u64(KIT_STR("18446744073709551615"), &u));
    CHECK(u == UINT64_MAX);
    CHECK(!kit_str_to_u64(KIT_STR("18446744073709551616"), &u));   /* overflow */
    CHECK(!kit_str_to_u64(KIT_STR("-1"), &u));                     /* not unsigned */

    /* A failed parse must leave the destination alone. */
    i = 999;
    CHECK(!kit_str_to_i64(KIT_STR("bad"), &i));
    CHECK_INT(i, 999);
}

TEST(str_to_double_is_strict) {
    double d = 0;
    CHECK(kit_str_to_double(KIT_STR("1.5"), &d));     CHECK_DBL(d, 1.5, 1e-12);
    CHECK(kit_str_to_double(KIT_STR("-0.25"), &d));   CHECK_DBL(d, -0.25, 1e-12);
    CHECK(kit_str_to_double(KIT_STR("1e3"), &d));     CHECK_DBL(d, 1000.0, 1e-9);
    CHECK(kit_str_to_double(KIT_STR("42"), &d));      CHECK_DBL(d, 42.0, 1e-12);

    CHECK(!kit_str_to_double(KIT_STR(""), &d));
    CHECK(!kit_str_to_double(KIT_STR("1.5x"), &d));
    CHECK(!kit_str_to_double(KIT_STR("abc"), &d));
    CHECK(!kit_str_to_double(KIT_STR("1e999"), &d));   /* overflows to infinity */

    /* Longer than the internal buffer: refused, never truncated. */
    char long_number[128];
    memset(long_number, '1', sizeof(long_number));
    CHECK(!kit_str_to_double(kit_str_from_parts(long_number, sizeof(long_number)), &d));
}

TEST(str_to_cstr_and_from_parts) {
    /* A view into a larger buffer, with no terminator of its own. */
    const char *backing = "prefix-PAYLOAD-suffix";
    KitStr sv = kit_str_from_parts(backing + 7, 7);
    CHECK_KITSTR(sv, "PAYLOAD");

    char *owned = kit_str_dup(sv);
    if (CHECK(owned != NULL)) {
        CHECK_STR(owned, "PAYLOAD");
        CHECK_INT(strlen(owned), 7);
        free(owned);
    }

    char *empty = kit_str_dup(kit_str_from_parts(NULL, 0));
    if (CHECK(empty != NULL)) { CHECK_STR(empty, ""); free(empty); }
}

/* --- string builder ------------------------------------------------------- */

TEST(buf_appends) {
    KitBuf sb = {0};
    kit_buf_append(&sb, "abc");
    kit_buf_append_char(&sb, '-');
    kit_buf_append_n(&sb, "defXX", 3);
    kit_buf_append_str(&sb, KIT_STR("-ghi"));
    kit_buf_printf(&sb, "-%d-%s", 42, "end");

    CHECK_STR(kit_buf_cstr(&sb), "abc-def-ghi-42-end");
    CHECK_INT(sb.count, strlen("abc-def-ghi-42-end"));

    char *owned = kit_buf_dup(&sb);
    CHECK_STR(owned, "abc-def-ghi-42-end");
    free(owned);

    kit_buf_reset(&sb);
    CHECK_INT(sb.count, 0);
    CHECK_STR(kit_buf_cstr(&sb), "");
    kit_buf_free(&sb);
    CHECK(sb.items == NULL);
}

TEST(buf_grows_past_the_initial_capacity) {
    KitBuf sb = {0};
    for (int i = 0; i < 5000; i++) kit_buf_printf(&sb, "%04d", i);
    CHECK_INT(sb.count, 20000);
    CHECK_INT(strlen(kit_buf_cstr(&sb)), 20000);
    CHECK(strncmp(sb.items, "000000010002", 12) == 0);
    kit_buf_free(&sb);
}

/* --- hashing -------------------------------------------------------------- */

TEST(hash_is_stable_and_discriminating) {
    CHECK_INT(kit_hash_str("utils"), kit_hash_str("utils"));
    CHECK(kit_hash_str("utils") != kit_hash_str("utils "));
    CHECK_INT(kit_hash_str("abc"), kit_hash_bytes("abc", 3));
    CHECK_INT(kit_hash_str(""), 5381u);   /* djb2 seed */

    /* Raw djb2 leaves too little entropy in the low bits to index a
     * power-of-two table; kit_hash_mix32 is what makes them usable. */
    CHECK_INT(kit_hash_mix32(0), 0u);
    CHECK_INT(kit_hash_mix32(kit_hash_str("utils")), kit_hash_mix32(kit_hash_str("utils")));
    {
        enum { CAP = 4096 };
        static int raw[CAP], mixed[CAP];
        char k[16];
        for (int i = 0; i < 3000; i++) {
            snprintf(k, sizeof(k), "key%d", i);
            raw[kit_hash_str(k) & (CAP - 1)]++;
            mixed[kit_hash_mix32(kit_hash_str(k)) & (CAP - 1)]++;
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
        seen[i] = kit_hash_str(key);
        for (int j = 0; j < i; j++) if (seen[j] == seen[i]) collisions++;
    }
    CHECK_INT(collisions, 0);
}

/* --- paths ---------------------------------------------------------------- */

TEST(path_basename_and_ext) {
    CHECK_STR(kit_path_basename("/usr/lib/libc.so"), "libc.so");
    CHECK_STR(kit_path_basename("plain.txt"), "plain.txt");
    CHECK_STR(kit_path_basename("/trailing/"), "trailing/");
    CHECK_STR(kit_path_basename("/"), "/");

    CHECK_STR(kit_path_ext("archive.tar.gz"), ".gz");
    CHECK_STR(kit_path_ext("/etc/hostname"), "");        /* no extension */
    CHECK_STR(kit_path_ext("/etc.d/hostname"), "");      /* dot in a parent only */
}

TEST(path_dirname_cases) {
    char buf[64];
    CHECK_STR(kit_path_dirname("/usr/lib/libc.so", buf, sizeof(buf)), "/usr/lib");
    CHECK_STR(kit_path_dirname("relative.txt", buf, sizeof(buf)), ".");
    CHECK_STR(kit_path_dirname("/file", buf, sizeof(buf)), "/");
    CHECK_STR(kit_path_dirname("a//b//c", buf, sizeof(buf)), "a//b");
}

TEST(path_join_cases) {
    char buf[64];
    CHECK_STR(kit_path_join(buf, sizeof(buf), "/usr", "lib"), "/usr" SEP "lib");
    CHECK_STR(kit_path_join(buf, sizeof(buf), "/usr/", "lib"), "/usr/lib");
    CHECK_STR(kit_path_join(buf, sizeof(buf), "/usr", "/lib"), "/usr" SEP "lib");
    CHECK_STR(kit_path_join(buf, sizeof(buf), "/usr/", "///lib"), "/usr/lib");
    CHECK_STR(kit_path_join(buf, sizeof(buf), "/usr", ""), "/usr");
    CHECK_STR(kit_path_join(buf, sizeof(buf), "", "lib"), "lib");
}

/* Regression: "bufsz - 1" wrapped around on an empty buffer and the following
 * memcpy wrote past the destination. Run under AddressSanitizer to catch it. */
TEST(path_helpers_respect_tiny_buffers) {
    char guard[8];

    memset(guard, '#', sizeof(guard));
    kit_path_dirname("/usr/lib/libc.so", guard, 0);
    CHECK_INT(guard[0], '#');            /* untouched */

    memset(guard, '#', sizeof(guard));
    kit_path_join(guard, 0, "/usr", "lib");
    CHECK_INT(guard[0], '#');

    char one[1];
    CHECK_STR(kit_path_dirname("/usr/lib", one, sizeof(one)), "");
    CHECK_STR(kit_path_join(one, sizeof(one), "/usr", "lib"), "");

    char small[5];
    CHECK_STR(kit_path_dirname("/usr/lib/libc.so", small, sizeof(small)), "/usr");
    CHECK_STR(kit_path_join(small, sizeof(small), "/usr", "lib"), "/usr");

    char two[2];
    CHECK_STR(kit_path_dirname("relative.txt", two, sizeof(two)), ".");
}

TEST(path_is_absolute_cases) {
    CHECK(kit_path_is_absolute("/etc"));
    CHECK(!kit_path_is_absolute("etc"));
    CHECK(!kit_path_is_absolute(""));
    CHECK(!kit_path_is_absolute("./etc"));
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("text");
    RUN(str_construction_and_equality);
    RUN(str_trim);
    RUN(str_cut);
    RUN(str_chop_left_clamps);
    RUN(str_prefix_and_suffix);
    RUN(str_search);
    RUN(str_eq_ignorecase_is_ascii_only);
    RUN(str_chop_by_sv_handles_multichar_delimiters);
    RUN(str_chop_right_clamps);
    RUN(str_try_chop_by_delim_field_counts);
    RUN(str_to_integers_is_strict);
    RUN(str_to_double_is_strict);
    RUN(str_to_cstr_and_from_parts);
    RUN(buf_appends);
    RUN(buf_grows_past_the_initial_capacity);
    RUN(hash_is_stable_and_discriminating);
    RUN(path_basename_and_ext);
    RUN(path_dirname_cases);
    RUN(path_join_cases);
    RUN(path_helpers_respect_tiny_buffers);
    RUN(path_is_absolute_cases);
    return utest_report();
}
