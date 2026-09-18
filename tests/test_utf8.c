/*
 * UTF-8. Most of these are the malformed cases, because the well-formed ones
 * are the easy half: what a decoder does with bad input is what decides
 * whether the loop around it terminates and whether a path check can be
 * walked past.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

/* The codepoints of a view, in order, with how many there were. */
static size_t decode_all(const char *bytes, size_t size, uint32_t *out, size_t room) {
    KitStr   rest  = kit_str_from_parts(bytes, size);
    size_t   total = 0;
    uint32_t codepoint;

    while (kit_utf8_next(&rest, &codepoint) && total < room)
        out[total++] = codepoint;
    return total;
}

TEST(ascii_decodes_one_byte_at_a_time) {
    uint32_t seen[8];
    size_t   n = decode_all("kit", 3, seen, KIT_COUNTOF(seen));

    CHECK_INT(n, 3);
    CHECK_INT(seen[0], 'k');
    CHECK_INT(seen[2], 't');
    CHECK_INT(kit_utf8_count(KIT_STR("kit")), 3);
}

TEST(each_length_decodes_to_its_codepoint) {
    uint32_t seen[8];

    /* 'é' U+00E9, '€' U+20AC, and U+1F600 in two, three and four bytes. */
    size_t n = decode_all("h\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80", 10,
                          seen, KIT_COUNTOF(seen));
    CHECK_INT(n, 4);
    CHECK_INT(seen[0], 'h');
    CHECK_INT(seen[1], 0x00e9);
    CHECK_INT(seen[2], 0x20ac);
    CHECK_INT(seen[3], 0x1f600);
}

TEST(counting_characters_is_not_counting_bytes) {
    KitStr text = KIT_STR("h\xc3\xa9llo");      /* héllo */

    CHECK_INT(text.count, 6);                   /* bytes */
    CHECK_INT(kit_utf8_count(text), 5);         /* characters */
    CHECK(kit_utf8_valid(text));
}

TEST(what_is_written_can_be_read_back) {
    const uint32_t interesting[] = {
        0, 'A', 0x7f, 0x80, 0x7ff, 0x800, 0xffff, 0x10000, 0x10ffff
    };

    for (size_t i = 0; i < KIT_COUNTOF(interesting); i++) {
        char   buf[KIT_UTF8_MAX];
        size_t written = kit_utf8_encode(interesting[i], buf);

        uint32_t back = 0;
        size_t   read = kit_utf8_decode(kit_str_from_parts(buf, written), &back);

        CHECK_INT(read, written);
        CHECK_INT(back, interesting[i]);
        CHECK(kit_utf8_valid(kit_str_from_parts(buf, written)));
    }
}

TEST(encoding_uses_the_shortest_form) {
    char buf[KIT_UTF8_MAX];

    CHECK_INT(kit_utf8_encode('A', buf), 1);
    CHECK_INT(kit_utf8_encode(0x00e9, buf), 2);
    CHECK_INT(kit_utf8_encode(0x20ac, buf), 3);
    CHECK_INT(kit_utf8_encode(0x1f600, buf), 4);
}

/* A codepoint the encoding cannot carry comes back as U+FFFD rather than as
 * bytes nothing can read. */
TEST(encoding_what_cannot_be_encoded_gives_the_replacement) {
    char     buf[KIT_UTF8_MAX];
    uint32_t back = 0;

    CHECK_INT(kit_utf8_encode(0x110000u, buf), 3);
    kit_utf8_decode(kit_str_from_parts(buf, 3), &back);
    CHECK_INT(back, KIT_UTF8_REPLACEMENT);

    CHECK_INT(kit_utf8_encode(0xd800u, buf), 3);
    kit_utf8_decode(kit_str_from_parts(buf, 3), &back);
    CHECK_INT(back, KIT_UTF8_REPLACEMENT);
}

/* The one that matters: "/" written in two bytes has to stay malformed, or a
 * check on the shortest form can be walked straight past. */
TEST(an_overlong_encoding_is_refused) {
    KitStr overlong_slash = kit_str_from_parts("\xc0\xaf", 2);
    KitStr overlong_nul   = kit_str_from_parts("\xc0\x80", 2);
    KitStr overlong_three = kit_str_from_parts("\xe0\x80\xaf", 3);

    uint32_t codepoint = 0;
    CHECK_INT(kit_utf8_decode(overlong_slash, &codepoint), 1);
    CHECK_INT(codepoint, KIT_UTF8_REPLACEMENT);
    CHECK(!kit_utf8_valid(overlong_slash));
    CHECK(!kit_utf8_valid(overlong_nul));
    CHECK(!kit_utf8_valid(overlong_three));
}

TEST(surrogates_and_values_past_the_last_codepoint_are_refused) {
    /* U+D800 and U+DFFF encoded as if they were ordinary, and U+110000. */
    CHECK(!kit_utf8_valid(kit_str_from_parts("\xed\xa0\x80", 3)));
    CHECK(!kit_utf8_valid(kit_str_from_parts("\xed\xbf\xbf", 3)));
    CHECK(!kit_utf8_valid(kit_str_from_parts("\xf4\x90\x80\x80", 4)));

    /* U+D7FF and U+E000, on either side of the surrogates, are fine. */
    CHECK(kit_utf8_valid(kit_str_from_parts("\xed\x9f\xbf", 3)));
    CHECK(kit_utf8_valid(kit_str_from_parts("\xee\x80\x80", 3)));
}

TEST(broken_sequences_are_refused_without_getting_stuck) {
    const char *broken[] = {
        "\x80",              /* a continuation with nothing to continue */
        "\xc3",              /* a lead byte and then the end            */
        "\xc3\x28",          /* a lead byte and something that is not a
                              * continuation                            */
        "\xe2\x82",          /* three bytes' worth, two given           */
        "\xf8\x88\x80\x80",  /* a five-byte lead, which does not exist  */
        "\xff\xfe",          /* bytes that appear in no encoding at all */
    };

    for (size_t i = 0; i < KIT_COUNTOF(broken); i++) {
        KitStr sv = KIT_STR(broken[i]);
        CHECK(!kit_utf8_valid(sv));

        /* Every step consumes at least one byte, so the loop ends. */
        size_t   steps = 0;
        uint32_t codepoint;
        while (kit_utf8_next(&sv, &codepoint)) {
            steps++;
            if (steps > 8) break;
        }
        CHECK(steps <= 8);
        CHECK_INT(sv.count, 0);
    }
}

/* Decoding is lossy on purpose; validity is the strict answer. A real U+FFFD
 * in the input is well formed and must not be reported as damage. */
TEST(a_genuine_replacement_character_is_valid) {
    KitStr real = kit_str_from_parts("\xef\xbf\xbd", 3);
    uint32_t codepoint = 0;

    CHECK(kit_utf8_valid(real));
    CHECK_INT(kit_utf8_decode(real, &codepoint), 3);
    CHECK_INT(codepoint, KIT_UTF8_REPLACEMENT);

    /* The same character, but produced by damage: one byte, not three. */
    CHECK_INT(kit_utf8_decode(kit_str_from_parts("\x80", 1), &codepoint), 1);
    CHECK_INT(codepoint, KIT_UTF8_REPLACEMENT);
}

TEST(an_empty_view_decodes_to_nothing) {
    uint32_t codepoint = 42;
    KitStr   empty     = KIT_ZEROED;

    CHECK_INT(kit_utf8_decode(empty, &codepoint), 0);
    CHECK_INT(codepoint, 0);
    CHECK(!kit_utf8_next(&empty, &codepoint));
    CHECK(kit_utf8_valid(empty));
    CHECK_INT(kit_utf8_count(empty), 0);
}

/* Any bytes at all, decoded and written back out, must produce valid UTF-8:
 * this is what makes it safe to print what another program wrote. */
TEST(anything_decoded_and_re_encoded_is_valid) {
    unsigned char noise[512];
    KitRandom     rng = kit_random_seed(4);
    for (size_t i = 0; i < sizeof noise; i++)
        noise[i] = (unsigned char)kit_random_between(&rng, 0, 255);

    KitBuf rebuilt = KIT_ZEROED;
    KitStr rest    = kit_str_from_parts((const char *)noise, sizeof noise);
    uint32_t codepoint;

    while (kit_utf8_next(&rest, &codepoint)) {
        char   buf[KIT_UTF8_MAX];
        size_t written = kit_utf8_encode(codepoint, buf);
        kit_buf_append_n(&rebuilt, buf, written);
    }

    CHECK(kit_utf8_valid(kit_str_from_parts(rebuilt.items, rebuilt.count)));
    kit_buf_free(&rebuilt);
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("utf8");
    RUN(ascii_decodes_one_byte_at_a_time);
    RUN(each_length_decodes_to_its_codepoint);
    RUN(counting_characters_is_not_counting_bytes);
    RUN(what_is_written_can_be_read_back);
    RUN(encoding_uses_the_shortest_form);
    RUN(encoding_what_cannot_be_encoded_gives_the_replacement);
    RUN(an_overlong_encoding_is_refused);
    RUN(surrogates_and_values_past_the_last_codepoint_are_refused);
    RUN(broken_sequences_are_refused_without_getting_stuck);
    RUN(a_genuine_replacement_character_is_valid);
    RUN(an_empty_view_decodes_to_nothing);
    RUN(anything_decoded_and_re_encoded_is_valid);
    return utest_report();
}
