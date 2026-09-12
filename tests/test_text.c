/*
 * String views, string builder, hashing and path helpers.
 * Includes the regression test for the path_dirname / path_join overflow.
 */

#define UTILS_IMPLEMENTATION
#include "../utils.h"
#include "utest.h"

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
    CHECK_STR(path_join(buf, sizeof(buf), "/usr", "lib"), "/usr/lib");
    CHECK_STR(path_join(buf, sizeof(buf), "/usr/", "lib"), "/usr/lib");
    CHECK_STR(path_join(buf, sizeof(buf), "/usr", "/lib"), "/usr/lib");
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
