/*
 * Glob. The interesting half is backtracking: a star that took too little has
 * to give the characters back, and a pattern built to make it do that over
 * and over must still finish, and must never recurse.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

TEST(a_pattern_with_no_wildcard_is_an_equality_test) {
    CHECK(kit_glob_match("main.c", "main.c"));
    CHECK(!kit_glob_match("main.c", "main.h"));
    CHECK(!kit_glob_match("main.c", "main.c.orig"));
    CHECK(!kit_glob_match("main.c", "Main.c"));      /* case matters */
    CHECK(kit_glob_match("", ""));
    CHECK(!kit_glob_match("", "x"));
    CHECK(!kit_glob_match("x", ""));
}

TEST(a_star_takes_anything_including_nothing) {
    CHECK(kit_glob_match("*", ""));
    CHECK(kit_glob_match("*", "anything at all"));
    CHECK(kit_glob_match("*.c", "main.c"));
    CHECK(kit_glob_match("*.c", ".c"));
    CHECK(!kit_glob_match("*.c", "main.h"));
    CHECK(kit_glob_match("main*", "main.c"));
    CHECK(kit_glob_match("main*", "main"));
    CHECK(kit_glob_match("*main*", "the main thing"));
    CHECK(kit_glob_match("**", "still fine"));
}

TEST(a_star_crosses_a_separator) {
    CHECK(kit_glob_match("src*.c", "src/deep/file.c"));
    CHECK(kit_glob_match("*", "a/b/c"));
}

TEST(a_question_mark_takes_exactly_one) {
    CHECK(kit_glob_match("test_?.o", "test_a.o"));
    CHECK(!kit_glob_match("test_?.o", "test_ab.o"));
    CHECK(!kit_glob_match("test_?.o", "test_.o"));
    CHECK(kit_glob_match("???", "abc"));
    CHECK(!kit_glob_match("???", "ab"));
}

TEST(a_class_takes_one_of_what_it_lists) {
    CHECK(kit_glob_match("[abc]at", "cat"));
    CHECK(!kit_glob_match("[abc]at", "hat"));
    CHECK(kit_glob_match("[a-z]*.txt", "notes.txt"));
    CHECK(!kit_glob_match("[a-z]*.txt", "Notes.txt"));
    CHECK(kit_glob_match("file[0-9].log", "file7.log"));
    CHECK(!kit_glob_match("file[0-9].log", "filex.log"));

    /* Several ranges and singles in one class. */
    CHECK(kit_glob_match("[a-cx-z1]*", "b!"));
    CHECK(kit_glob_match("[a-cx-z1]*", "1!"));
    CHECK(!kit_glob_match("[a-cx-z1]*", "d!"));
}

TEST(a_negated_class_takes_one_of_anything_else) {
    CHECK(kit_glob_match("[!abc]at", "hat"));
    CHECK(!kit_glob_match("[!abc]at", "cat"));
    CHECK(kit_glob_match("[^0-9]*", "letter"));
    CHECK(!kit_glob_match("[^0-9]*", "7even"));

    /* Negation still takes exactly one character. */
    CHECK(!kit_glob_match("[!x]", ""));
    CHECK(!kit_glob_match("[!x]", "ab"));
}

/* The corners of the class syntax, all of them the shell's rules. */
TEST(the_awkward_classes_behave_as_a_shell_would) {
    CHECK(kit_glob_match("[]]", "]"));            /* a first ']' is a member */
    CHECK(kit_glob_match("[!]]", "a"));
    CHECK(!kit_glob_match("[!]]", "]"));
    CHECK(kit_glob_match("[a-]", "-"));           /* a trailing '-' is itself */
    CHECK(kit_glob_match("[a-]", "a"));

    /* Unterminated: the bracket is an ordinary character. */
    CHECK(kit_glob_match("[abc", "[abc"));
    CHECK(!kit_glob_match("[abc", "a"));
}

TEST(a_backslash_makes_the_next_character_ordinary) {
    CHECK(kit_glob_match("\\*.c", "*.c"));
    CHECK(!kit_glob_match("\\*.c", "main.c"));
    CHECK(kit_glob_match("a\\?c", "a?c"));
    CHECK(!kit_glob_match("a\\?c", "abc"));
    CHECK(kit_glob_match("\\[abc\\]", "[abc]"));
}

TEST(matching_is_by_character_not_by_byte) {
    /* 'é' is two bytes, and one '?' has to cover it. */
    CHECK(kit_glob_match("caf?", "caf\xc3\xa9"));
    CHECK(!kit_glob_match("caf??", "caf\xc3\xa9"));
    CHECK(kit_glob_match("*\xc3\xa9", "caf\xc3\xa9"));
    CHECK(kit_glob_match("caf\xc3\xa9", "caf\xc3\xa9"));

    /* A range over codepoints, not over the bytes that spell them. */
    CHECK(kit_glob_match("[\xc3\xa0-\xc3\xbf]*", "\xc3\xa9t\xc3\xa9"));
    CHECK(!kit_glob_match("[\xc3\xa0-\xc3\xbf]*", "ete"));
}

/* A star that swallowed too little has to give characters back, repeatedly,
 * which is the one part of this that can be got wrong quietly. */
TEST(a_star_gives_back_what_it_took_too_much_of) {
    CHECK(kit_glob_match("*b", "aaab"));
    CHECK(kit_glob_match("a*b*c", "axxbyyc"));
    CHECK(kit_glob_match("*a*b", "xxaxxb"));
    CHECK(!kit_glob_match("*a*b", "xxaxx"));
    CHECK(kit_glob_match("*.*", "a.b"));
    CHECK(kit_glob_match("a*a*a", "aaa"));
    CHECK(!kit_glob_match("a*a*a*a", "aaa"));
}

/* The shape that makes a naive matcher take exponential time, and a recursive
 * one run out of stack. It has to answer, and quickly. */
TEST(the_pattern_built_to_be_slow_still_answers) {
    char text[256];
    memset(text, 'a', sizeof text - 1);
    text[sizeof text - 1] = '\0';

    KitTimer clock = kit_timer_start();
    CHECK(!kit_glob_match("*a*a*a*a*a*a*b", text));
    CHECK(kit_glob_match("*a*a*a*a*a*a*a", text));
    CHECK(kit_timer_ms(clock) < 500.0);
}

TEST(a_missing_string_is_not_a_match) {
    CHECK(!kit_glob_match(NULL, "x"));
    CHECK(!kit_glob_match("*", NULL));
    CHECK(!kit_glob_match(NULL, NULL));
}

/* What the examples actually ask of it. */
TEST(the_patterns_a_build_uses) {
    const char *names[] = { "main.c", "greet.c", "greet.h", "notes.txt", "a.out" };
    size_t sources = 0, headers = 0;

    for (size_t i = 0; i < KIT_COUNTOF(names); i++) {
        if (kit_glob_match("*.c", names[i])) sources++;
        if (kit_glob_match("*.h", names[i])) headers++;
    }
    CHECK_INT(sources, 2);
    CHECK_INT(headers, 1);
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("glob");
    RUN(a_pattern_with_no_wildcard_is_an_equality_test);
    RUN(a_star_takes_anything_including_nothing);
    RUN(a_star_crosses_a_separator);
    RUN(a_question_mark_takes_exactly_one);
    RUN(a_class_takes_one_of_what_it_lists);
    RUN(a_negated_class_takes_one_of_anything_else);
    RUN(the_awkward_classes_behave_as_a_shell_would);
    RUN(a_backslash_makes_the_next_character_ordinary);
    RUN(matching_is_by_character_not_by_byte);
    RUN(a_star_gives_back_what_it_took_too_much_of);
    RUN(the_pattern_built_to_be_slow_still_answers);
    RUN(a_missing_string_is_not_a_match);
    RUN(the_patterns_a_build_uses);
    return utest_report();
}
