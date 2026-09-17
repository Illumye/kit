/*
 * Structured errors: recording, the context chain, truncation, and the rule
 * that a failure is either reported or logged, never both.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

#define TMP_LOG "utest-tmp-error.log"

/* --- recording ------------------------------------------------------------ */

TEST(error_starts_clear) {
    KitError err = KIT_ZEROED;
    CHECK_INT(err.code, KIT_OK);
    CHECK_INT(err.length, 0);
    CHECK_STR(err.message, "");
}

TEST(error_set_formats_and_returns_false) {
    KitError err = KIT_ZEROED;
    CHECK(!kit_error_set(&err, KIT_ERR_INVALID, "expected %s, got '%s'", "an integer", "abc"));
    CHECK_INT(err.code, KIT_ERR_INVALID);
    CHECK_INT(err.native, 0);
    CHECK_STR(err.message, "expected an integer, got 'abc'");
    CHECK_INT(err.length, strlen(err.message));
    CHECK(!err.truncated);
}

TEST(error_set_replaces_a_previous_failure) {
    KitError err = KIT_ZEROED;
    kit_error_set(&err, KIT_ERR_IO, "first");
    kit_error_context(&err, "outer");
    kit_error_set(&err, KIT_ERR_BUSY, "second");
    CHECK_INT(err.code, KIT_ERR_BUSY);
    CHECK_STR(err.message, "second");       /* the old context does not leak */
}

TEST(error_errno_maps_the_code_and_appends_the_cause) {
    KitError err = KIT_ZEROED;
    kit_error_errno(&err, ENOENT, "cannot open '%s'", "config.ini");
    CHECK_INT(err.code, KIT_ERR_NOT_FOUND);
    CHECK_INT(err.native, ENOENT);

    char expected[256];
    snprintf(expected, sizeof(expected), "cannot open 'config.ini': %s", strerror(ENOENT));
    CHECK_STR(err.message, expected);
}

TEST(error_code_from_errno) {
    CHECK_INT(kit_error_code_from_errno(0),       KIT_OK);
    CHECK_INT(kit_error_code_from_errno(ENOENT),  KIT_ERR_NOT_FOUND);
    CHECK_INT(kit_error_code_from_errno(EEXIST),  KIT_ERR_EXISTS);
    CHECK_INT(kit_error_code_from_errno(EISDIR),  KIT_ERR_WRONG_KIND);
    CHECK_INT(kit_error_code_from_errno(ENOTDIR), KIT_ERR_WRONG_KIND);
    CHECK_INT(kit_error_code_from_errno(EACCES),  KIT_ERR_PERMISSION);
    CHECK_INT(kit_error_code_from_errno(EPERM),   KIT_ERR_PERMISSION);
    CHECK_INT(kit_error_code_from_errno(EINVAL),  KIT_ERR_INVALID);
    CHECK_INT(kit_error_code_from_errno(ERANGE),  KIT_ERR_RANGE);
    CHECK_INT(kit_error_code_from_errno(ENOSPC),  KIT_ERR_NO_SPACE);
    CHECK_INT(kit_error_code_from_errno(ENOMEM),  KIT_ERR_NO_MEMORY);
    CHECK_INT(kit_error_code_from_errno(EBUSY),   KIT_ERR_BUSY);
    CHECK_INT(kit_error_code_from_errno(EINTR),   KIT_ERR_INTERRUPTED);
    CHECK_INT(kit_error_code_from_errno(EIO),     KIT_ERR_IO);
    CHECK_INT(kit_error_code_from_errno(EXDEV),   KIT_ERR_UNSUPPORTED);
#ifdef ENOTEMPTY
    CHECK_INT(kit_error_code_from_errno(ENOTEMPTY), KIT_ERR_NOT_EMPTY);
#endif
    CHECK_INT(kit_error_code_from_errno(-12345),  KIT_ERR_OTHER);
}

TEST(error_code_names_are_complete_and_distinct) {
    /* A new code without a name would print "unknown" in every log. */
    const char *seen[KIT_ERR_OTHER + 1];
    for (int c = KIT_OK; c <= KIT_ERR_OTHER; c++) {
        seen[c] = kit_error_code_name((KitErrorCode)c);
        if (!CHECK(strcmp(seen[c], "unknown") != 0)) printf("      code %d has no name\n", c);
        for (int d = KIT_OK; d < c; d++)
            if (!CHECK(strcmp(seen[c], seen[d]) != 0)) printf("      %s is used twice\n", seen[c]);
    }
    CHECK_STR(kit_error_code_name(KIT_ERR_NOT_FOUND), "not_found");
    CHECK_STR(kit_error_code_name((KitErrorCode)9999), "unknown");
}

TEST(error_clear_resets_for_reuse) {
    KitError err = KIT_ZEROED;
    kit_error_errno(&err, EIO, "write");
    kit_error_clear(&err);
    CHECK_INT(err.code, KIT_OK);
    CHECK_INT(err.native, 0);
    CHECK_INT(err.length, 0);
    CHECK_STR(err.message, "");
    kit_error_clear(NULL);                  /* must not crash */
}

/* --- context -------------------------------------------------------------- */

TEST(error_context_builds_outermost_first) {
    KitError err = KIT_ZEROED;
    kit_error_set(&err, KIT_ERR_INVALID, "expected an integer");
    CHECK(!kit_error_context(&err, "line %d", 12));
    kit_error_context(&err, "reading %s", "config.ini");
    kit_error_context(&err, "starting");

    CHECK_STR(err.message, "starting: reading config.ini: line 12: expected an integer");
    CHECK_INT(err.length, strlen(err.message));
    CHECK_INT(err.code, KIT_ERR_INVALID);   /* context never changes the code */
    CHECK(!err.truncated);
}

TEST(error_context_is_a_no_op_without_a_failure) {
    KitError err = KIT_ZEROED;
    kit_error_context(&err, "should not appear");
    CHECK_STR(err.message, "");
    CHECK_INT(err.code, KIT_OK);
    kit_error_context(NULL, "nor crash");
}

/* --- truncation ----------------------------------------------------------- */

TEST(error_set_truncates_with_a_marker) {
    KitError err = KIT_ZEROED;
    char big[2 * KIT_ERROR_CAPACITY];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';

    kit_error_set(&err, KIT_ERR_OTHER, "%s", big);
    CHECK(err.truncated);
    CHECK(err.length < KIT_ERROR_CAPACITY);
    CHECK_INT(strlen(err.message), err.length);
    CHECK(kit_str_ends_with_cstr(KIT_STR(err.message), "..."));
}

/* When the chain outgrows the buffer, what survives must be the outermost
 * context and the root cause: the two things a reader needs. */
TEST(error_context_keeps_both_ends_when_it_overflows) {
    KitError err = KIT_ZEROED;
    kit_error_set(&err, KIT_ERR_NOT_FOUND, "ROOT-CAUSE");
    for (int i = 0; i < 100; i++) kit_error_context(&err, "intermediate step %03d", i);
    kit_error_context(&err, "OUTERMOST");

    CHECK(err.truncated);
    CHECK(err.length < KIT_ERROR_CAPACITY);
    CHECK_INT(strlen(err.message), err.length);
    CHECK(kit_str_starts_with_cstr(KIT_STR(err.message), "OUTERMOST: ..."));
    CHECK(kit_str_ends_with_cstr(KIT_STR(err.message), "ROOT-CAUSE"));
}

TEST(error_context_caps_a_huge_prefix_at_half) {
    KitError err = KIT_ZEROED;
    kit_error_set(&err, KIT_ERR_IO, "ROOT");
    char huge[KIT_ERROR_CAPACITY];
    memset(huge, 'c', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';

    kit_error_context(&err, "%s", huge);
    CHECK(err.truncated);
    CHECK(kit_str_ends_with_cstr(KIT_STR(err.message), "ROOT"));   /* still there */
    CHECK(err.length < KIT_ERROR_CAPACITY);
}

/* Cutting inside a multi-byte character would hand invalid UTF-8 to whatever
 * prints the message next. */
static bool is_valid_utf8(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int extra = (*p < 0x80) ? 0 : (*p >> 5) == 0x6 ? 1 : (*p >> 4) == 0xE ? 2
                  : (*p >> 3) == 0x1E ? 3 : -1;
        if (extra < 0) return false;
        p++;
        while (extra-- > 0) {
            if ((*p & 0xC0) != 0x80) return false;
            p++;
        }
    }
    return true;
}

TEST(error_truncation_never_splits_a_character) {
    /* "é" is two bytes; try every alignment against the cut point. */
    for (int shift = 0; shift < 4; shift++) {
        char text[2 * KIT_ERROR_CAPACITY];
        size_t n = 0;
        for (int i = 0; i < shift; i++) text[n++] = 'a';
        while (n + 2 < sizeof(text)) { text[n++] = (char)0xC3; text[n++] = (char)0xA9; }
        text[n] = '\0';

        KitError err = KIT_ZEROED;
        kit_error_set(&err, KIT_ERR_OTHER, "%s", text);
        if (!CHECK(is_valid_utf8(err.message))) printf("      set, shift %d\n", shift);

        KitError chained = KIT_ZEROED;
        kit_error_set(&chained, KIT_ERR_OTHER, "%s", text);
        kit_error_context(&chained, "%s", text);
        if (!CHECK(is_valid_utf8(chained.message))) printf("      context, shift %d\n", shift);
    }
}

/* --- report or log, never both -------------------------------------------- */

static char *capture_log(void (*fn)(void)) {
    FILE *fp = fopen(TMP_LOG, "wb");
    if (!fp) return NULL;
    kit_log_set_output(fp);
    kit_log_set_level(KIT_LOG_DEBUG);
    kit_log_set_color(KIT_LOG_COLOR_NEVER);
    kit_log_set_fields(KIT_LOG_FIELD_LEVEL);
    fn();
    fclose(fp);
    kit_log_set_output(NULL);
    kit_log_set_level(KIT_LOG_CRITICAL);
    kit_log_set_fields(KIT_LOG_FIELDS_DEFAULT);
    char *text = kit_fs_read(TMP_LOG);
    kit_fs_remove(TMP_LOG);
    return text;
}

static void record_into_an_error(void) {
    KitError err = KIT_ZEROED;
    kit_error_errno(&err, ENOENT, "cannot open 'x'");
    kit_error_set(&err, KIT_ERR_INVALID, "bad input");
}

static void record_into_null(void) {
    kit_error_set(NULL, KIT_ERR_INVALID, "bad %s", "input");
}

TEST(error_given_an_error_logs_nothing) {
    char *log = capture_log(record_into_an_error);
    if (!CHECK(log != NULL)) return;
    CHECK_STR(log, "");
    free(log);
}

TEST(error_given_null_logs_instead) {
    char *log = capture_log(record_into_null);
    if (!CHECK(log != NULL)) return;
    CHECK_STR(log, "[ERROR] bad input\n");
    free(log);
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("errors");
    RUN(error_starts_clear);
    RUN(error_set_formats_and_returns_false);
    RUN(error_set_replaces_a_previous_failure);
    RUN(error_errno_maps_the_code_and_appends_the_cause);
    RUN(error_code_from_errno);
    RUN(error_code_names_are_complete_and_distinct);
    RUN(error_clear_resets_for_reuse);
    RUN(error_context_builds_outermost_first);
    RUN(error_context_is_a_no_op_without_a_failure);
    RUN(error_set_truncates_with_a_marker);
    RUN(error_context_keeps_both_ends_when_it_overflows);
    RUN(error_context_caps_a_huge_prefix_at_half);
    RUN(error_truncation_never_splits_a_character);
    RUN(error_given_an_error_logs_nothing);
    RUN(error_given_null_logs_instead);
    return utest_report();
}
