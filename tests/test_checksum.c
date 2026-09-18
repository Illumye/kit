/*
 * Checksums. Both of these have published answers, so the tests are the
 * published answers: the vectors from FIPS 180-4 for SHA-256, and the values
 * gzip and zlib produce for CRC-32. An implementation that agrees with them
 * agrees with every other program that will ever read what it wrote.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

static const char *hex_of(const void *data, size_t size) {
    static char buf[KIT_SHA256_HEX_CAPACITY];
    unsigned char digest[KIT_SHA256_SIZE];

    kit_sha256(data, size, digest);
    return kit_sha256_hex(digest, buf, sizeof buf);
}

TEST(crc32_matches_the_published_values) {
    CHECK(kit_crc32(0, "123456789", 9) == 0xcbf43926u);
    CHECK(kit_crc32(0, "The quick brown fox jumps over the lazy dog", 43)
          == 0x414fa339u);
    CHECK(kit_crc32(0, "a", 1) == 0xe8b7be43u);
}

TEST(crc32_of_nothing_is_the_seed_it_was_given) {
    CHECK(kit_crc32(0, "", 0) == 0u);
    CHECK(kit_crc32(0, NULL, 16) == 0u);
    CHECK(kit_crc32(0x1234u, "", 0) == 0x1234u);
}

/* The seed is the running value, which is what lets a stream be summed in
 * pieces: the whole must equal the sum of its parts. */
TEST(crc32_in_pieces_equals_crc32_in_one_go) {
    const char *text = "The quick brown fox jumps over the lazy dog";

    uint32_t running = kit_crc32(0, text, 10);
    running = kit_crc32(running, text + 10, 20);
    running = kit_crc32(running, text + 30, 13);

    CHECK(running == kit_crc32(0, text, 43));
}

/* Zero bytes are not nothing: a checksum that ignored them would give a file
 * of NULs the same answer as an empty one. */
TEST(crc32_sees_leading_zero_bytes) {
    const unsigned char zeros[4] = { 0, 0, 0, 0 };
    CHECK(kit_crc32(0, zeros, 4) != 0u);
    CHECK(kit_crc32(0, zeros, 1) != kit_crc32(0, zeros, 2));
}

TEST(sha256_matches_the_standard_vectors) {
    CHECK_STR(hex_of("", 0),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK_STR(hex_of("abc", 3),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK_STR(hex_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

/* A million 'a', the long vector, which is the one that exercises the block
 * loop and the 64-bit length rather than a single padded block. */
TEST(sha256_matches_the_long_vector) {
    KitSha256     hash;
    unsigned char digest[KIT_SHA256_SIZE];
    char          buf[KIT_SHA256_HEX_CAPACITY];
    char          chunk[1000];

    memset(chunk, 'a', sizeof chunk);
    kit_sha256_init(&hash);
    for (int i = 0; i < 1000; i++) kit_sha256_update(&hash, chunk, sizeof chunk);
    kit_sha256_final(&hash, digest);

    CHECK_STR(kit_sha256_hex(digest, buf, sizeof buf),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

/* However the data is cut up on the way in, the digest is the same: the state
 * carries the tail of an incomplete block from one call to the next. */
TEST(sha256_does_not_care_how_the_data_is_cut_up) {
    unsigned char source[700];
    for (size_t i = 0; i < sizeof source; i++)
        source[i] = (unsigned char)(i * 7u + 3u);

    unsigned char whole[KIT_SHA256_SIZE];
    kit_sha256(source, sizeof source, whole);

    const size_t cuts[] = { 1, 63, 64, 65, 100, 128, 333 };
    for (size_t c = 0; c < KIT_COUNTOF(cuts); c++) {
        KitSha256     hash;
        unsigned char piecemeal[KIT_SHA256_SIZE];

        kit_sha256_init(&hash);
        for (size_t at = 0; at < sizeof source; at += cuts[c]) {
            size_t left = sizeof source - at;
            kit_sha256_update(&hash, source + at, left < cuts[c] ? left : cuts[c]);
        }
        kit_sha256_final(&hash, piecemeal);

        CHECK(memcmp(whole, piecemeal, KIT_SHA256_SIZE) == 0);
    }
}

/* Every block boundary, since the padding has a different shape on each side
 * of 55 bytes and of 63. */
TEST(sha256_pads_correctly_around_a_block) {
    unsigned char source[130];
    memset(source, 'z', sizeof source);

    /* 55 bytes is the last length whose padding still fits in the block it
     * ends, and 56 the first that needs another one. Both digests come from
     * sha256sum rather than from this file. */
    const char *known[] = {
        "5a383411d906da80612f366cb9b90ae54bcf6e7743b4117ace0884d98cd159ca",
        "c66a5b692b9a20229733ef8b87cfec52679c86a0c0245643484c46d4dcd82afa",
    };
    char buf[KIT_SHA256_HEX_CAPACITY];

    unsigned char digest[KIT_SHA256_SIZE];
    kit_sha256(source, 55, digest);
    CHECK_STR(kit_sha256_hex(digest, buf, sizeof buf), known[0]);

    kit_sha256(source, 56, digest);
    CHECK_STR(kit_sha256_hex(digest, buf, sizeof buf), known[1]);

    /* And the neighbours are all different from each other. */
    char seen[6][KIT_SHA256_HEX_CAPACITY];
    for (size_t i = 0; i < 6; i++) {
        kit_sha256(source, 61 + i, digest);
        kit_sha256_hex(digest, seen[i], sizeof seen[i]);
    }
    for (size_t i = 1; i < 6; i++)
        CHECK(strcmp(seen[i - 1], seen[i]) != 0);
}

TEST(a_state_can_be_reused_once_it_is_initialised_again) {
    KitSha256     hash;
    unsigned char first[KIT_SHA256_SIZE], second[KIT_SHA256_SIZE];

    kit_sha256_init(&hash);
    kit_sha256_update(&hash, "abc", 3);
    kit_sha256_final(&hash, first);

    kit_sha256_init(&hash);
    kit_sha256_update(&hash, "abc", 3);
    kit_sha256_final(&hash, second);

    CHECK(memcmp(first, second, KIT_SHA256_SIZE) == 0);
}

TEST(hex_respects_a_short_buffer) {
    unsigned char digest[KIT_SHA256_SIZE];
    kit_sha256("abc", 3, digest);

    char full[KIT_SHA256_HEX_CAPACITY];
    CHECK_INT(strlen(kit_sha256_hex(digest, full, sizeof full)), 64);

    char small[11] = { 0 };
    kit_sha256_hex(digest, small, sizeof small);
    CHECK_INT(strlen(small), 10);              /* whole bytes, terminated */
    CHECK(strncmp(small, full, 10) == 0);

    char none[1] = { 'x' };
    kit_sha256_hex(digest, none, 0);
    CHECK_INT(none[0], 'x');
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("checksum");
    RUN(crc32_matches_the_published_values);
    RUN(crc32_of_nothing_is_the_seed_it_was_given);
    RUN(crc32_in_pieces_equals_crc32_in_one_go);
    RUN(crc32_sees_leading_zero_bytes);
    RUN(sha256_matches_the_standard_vectors);
    RUN(sha256_matches_the_long_vector);
    RUN(sha256_does_not_care_how_the_data_is_cut_up);
    RUN(sha256_pads_correctly_around_a_block);
    RUN(a_state_can_be_reused_once_it_is_initialised_again);
    RUN(hex_respects_a_short_buffer);
    return utest_report();
}
