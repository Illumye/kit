/*
 * Human-readable formatting. Most of these are boundaries: the value just
 * below a unit and the one just above it, plus the values that rounding would
 * push into a unit that already has a name of its own.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

#include <math.h>

static const char *size_of(uint64_t bytes) {
    static char buf[KIT_FMT_CAPACITY];
    return kit_fmt_size(buf, sizeof buf, bytes);
}

static const char *duration_of(double seconds) {
    static char buf[KIT_FMT_CAPACITY];
    return kit_fmt_duration(buf, sizeof buf, seconds);
}

TEST(size_shows_exact_bytes_below_a_kibibyte) {
    CHECK_STR(size_of(0), "0 B");
    CHECK_STR(size_of(1), "1 B");
    CHECK_STR(size_of(412), "412 B");
    CHECK_STR(size_of(1023), "1023 B");
}

TEST(size_switches_unit_at_each_multiple_of_1024) {
    CHECK_STR(size_of(1024), "1.0 KiB");
    CHECK_STR(size_of(1536), "1.5 KiB");
    CHECK_STR(size_of(1024u * 1024u), "1.0 MiB");
    CHECK_STR(size_of(3355443u), "3.2 MiB");
    CHECK_STR(size_of(1024ull * 1024ull * 1024ull), "1.0 GiB");
    CHECK_STR(size_of(1024ull * 1024ull * 1024ull * 1024ull), "1.0 TiB");
}

/* 1048575 bytes is 1023.999 KiB, which one decimal place rounds to a number
 * that should have been carried into the next unit. */
TEST(size_carries_a_value_that_rounding_would_fill) {
    CHECK_STR(size_of(1024u * 1024u - 1u), "1.0 MiB");
    CHECK_STR(size_of(1024ull * 1024ull * 1024ull - 1ull), "1.0 GiB");
}

TEST(size_tops_out_at_exbibytes) {
    CHECK_STR(size_of(UINT64_MAX), "16.0 EiB");
}

TEST(size_respects_a_tiny_buffer) {
    char small[4] = { 'x', 'x', 'x', 'x' };
    char none[1]  = { 'z' };

    kit_fmt_size(small, sizeof small, 4096);
    CHECK_INT(strlen(small), 3);           /* written, and terminated */

    kit_fmt_size(none, 0, 4096);
    CHECK_INT(none[0], 'z');               /* a bufsz of 0 touches nothing */
}

TEST(duration_has_no_way_to_fail) {
    CHECK_STR(duration_of(0.0), "0 ms");
    CHECK_STR(duration_of(-5.0), "0 ms");
    CHECK_STR(duration_of(NAN), "0 ms");
    CHECK_STR(duration_of(INFINITY), "104166666666d 16h");   /* the cap */
}

TEST(duration_keeps_the_unit_a_reader_expects) {
    CHECK_STR(duration_of(0.00042), "0.42 ms");
    CHECK_STR(duration_of(0.85), "850 ms");
    CHECK_STR(duration_of(1.5), "1.50 s");
    CHECK_STR(duration_of(59.0), "59.00 s");
    CHECK_STR(duration_of(134.0), "2m 14s");
    CHECK_STR(duration_of(3900.0), "1h 05m");
    CHECK_STR(duration_of(100000.0), "1d 03h");
}

TEST(duration_carries_a_value_that_rounding_would_fill) {
    CHECK_STR(duration_of(0.9999), "1.00 s");
    CHECK_STR(duration_of(59.999), "1m 00s");
    CHECK_STR(duration_of(3599.9), "1h 00m");
    CHECK_STR(duration_of(86399.9), "1d 00h");
}

TEST(duration_pads_the_smaller_unit) {
    CHECK_STR(duration_of(60.0), "1m 00s");
    CHECK_STR(duration_of(65.0), "1m 05s");
    CHECK_STR(duration_of(3600.0), "1h 00m");
    CHECK_STR(duration_of(86400.0), "1d 00h");
}

/* The dump goes to a stream, so it is read back from one: a temporary file,
 * which is the only spelling both POSIX and Windows agree on. */
static const char *dumped(const void *data, size_t size, uint64_t start) {
    static char text[2048];
    text[0] = '\0';

    FILE *tmp = tmpfile();
    if (!tmp) return text;

    kit_hex_dump(tmp, data, size, start);
    rewind(tmp);
    size_t n = fread(text, 1, sizeof text - 1, tmp);
    text[n] = '\0';
    fclose(tmp);
    return text;
}

TEST(hex_dump_lays_out_sixteen_bytes_a_line) {
    const char *line = "kit.h - a single-header toolkit";

    CHECK_STR(dumped(line, 16, 0),
              "00000000  6b 69 74 2e 68 20 2d 20  61 20 73 69 6e 67 6c 65  "
              "|kit.h - a single|\n");
}

/* The text column stops at the real end, and the hex column is padded so that
 * the bar opening it stays in the same place on every line, which is the only
 * reason the padding exists. */
TEST(hex_dump_pads_a_partial_last_line) {
    char full[128];
    snprintf(full, sizeof full, "%s", dumped("0123456789abcdef", 16, 0));
    const char *partial = dumped("abc", 3, 0);

    const char *bar_full    = strchr(full, '|');
    const char *bar_partial = strchr(partial, '|');
    if (!CHECK(bar_full && bar_partial)) return;

    CHECK_INT(bar_partial - partial, bar_full - full);
    CHECK(strstr(partial, "  61 62 63 ") != NULL);
    CHECK(strstr(partial, "|abc|\n") != NULL);
}

TEST(hex_dump_labels_the_lines_with_the_offset_it_was_given) {
    const char *text = dumped("0123456789abcdefghij", 20, 0x1000);
    CHECK(strstr(text, "00001000 ") != NULL);
    CHECK(strstr(text, "00001010 ") != NULL);
    CHECK(strstr(text, "|ghij|") != NULL);
}

TEST(hex_dump_shows_a_dot_for_what_cannot_be_printed) {
    const unsigned char raw[] = { 0x00, 0x1f, 'A', 0x7f, 0x80, 0xff };
    CHECK(strstr(dumped(raw, sizeof raw, 0), "|..A...|") != NULL);
}

TEST(hex_dump_writes_nothing_when_there_is_nothing_to_write) {
    CHECK_STR(dumped("abc", 0, 0), "");
    CHECK_STR(dumped(NULL, 16, 0), "");
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("format");
    RUN(size_shows_exact_bytes_below_a_kibibyte);
    RUN(size_switches_unit_at_each_multiple_of_1024);
    RUN(size_carries_a_value_that_rounding_would_fill);
    RUN(size_tops_out_at_exbibytes);
    RUN(size_respects_a_tiny_buffer);
    RUN(duration_has_no_way_to_fail);
    RUN(duration_keeps_the_unit_a_reader_expects);
    RUN(duration_carries_a_value_that_rounding_would_fill);
    RUN(duration_pads_the_smaller_unit);
    RUN(hex_dump_lays_out_sixteen_bytes_a_line);
    RUN(hex_dump_pads_a_partial_last_line);
    RUN(hex_dump_labels_the_lines_with_the_offset_it_was_given);
    RUN(hex_dump_shows_a_dot_for_what_cannot_be_printed);
    RUN(hex_dump_writes_nothing_when_there_is_nothing_to_write);
    return utest_report();
}
