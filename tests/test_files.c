/*
 * Files, logging and dynamic arrays.
 * Includes the regression tests for the read_file and write_file defects.
 */

#define UTILS_IMPLEMENTATION
#include "../utils.h"
#include "utest.h"

#define TMP_TXT "utest-tmp-file.txt"
#define TMP_BIN "utest-tmp-file.bin"

/* --- logging -------------------------------------------------------------- */

TEST(log_level_filters_lower_levels) {
    FILE *fp = fopen(TMP_TXT, "w+");
    if (!CHECK(fp != NULL)) return;

    log_set_output(fp);
    log_set_level(LOG_WARNING);
    LOG(LOG_DEBUG, "invisible");
    LOG(LOG_INFO,  "invisible");
    LOG(LOG_ERROR, "visible");
    fflush(fp);

    long size = ftell(fp);
    fclose(fp);
    log_set_output(NULL);
    log_set_level(LOG_CRITICAL);

    char *content = read_file(TMP_TXT);
    if (!CHECK(content != NULL)) return;
    CHECK(strstr(content, "visible")   != NULL);
    CHECK(strstr(content, "invisible") == NULL);
    CHECK(size > 0);
    free(content);
    remove(TMP_TXT);
}

/* Regression: escape sequences used to be written unconditionally, so every
 * redirected log file was littered with them. */
TEST(log_omits_ansi_codes_when_not_a_tty) {
    FILE *fp = fopen(TMP_TXT, "w");
    if (!CHECK(fp != NULL)) return;

    log_set_output(fp);
    log_set_level(LOG_ERROR);
    log_set_color(LOG_COLOR_AUTO);
    LOG(LOG_ERROR, "plain text please");
    fclose(fp);
    log_set_output(NULL);
    log_set_level(LOG_CRITICAL);

    char *content = read_file(TMP_TXT);
    if (!CHECK(content != NULL)) return;
    CHECK(strchr(content, '\x1b') == NULL);
    CHECK(strstr(content, "plain text please") != NULL);
    free(content);

    /* ALWAYS still forces colour on a non-tty, for pagers that render it. */
    fp = fopen(TMP_TXT, "w");
    if (!CHECK(fp != NULL)) return;
    log_set_output(fp);
    log_set_level(LOG_ERROR);
    log_set_color(LOG_COLOR_ALWAYS);
    LOG(LOG_ERROR, "coloured");
    fclose(fp);
    log_set_output(NULL);
    log_set_level(LOG_CRITICAL);

    content = read_file(TMP_TXT);
    if (!CHECK(content != NULL)) return;
    CHECK(strchr(content, '\x1b') != NULL);
    free(content);

    log_set_color(LOG_COLOR_AUTO);
    remove(TMP_TXT);
}

/* --- files ---------------------------------------------------------------- */

TEST(write_then_read_roundtrip) {
    const char *payload = "hello\nworld\n";
    CHECK(write_file(TMP_TXT, payload, strlen(payload)));
    CHECK_INT(file_size(TMP_TXT), (long)strlen(payload));
    CHECK(file_exists(TMP_TXT));

    char *back = read_file(TMP_TXT);
    if (!CHECK(back != NULL)) return;
    CHECK_STR(back, payload);
    free(back);
    remove(TMP_TXT);
}

TEST(read_file_reports_size_and_keeps_embedded_nuls) {
    const char blob[] = { 'a', '\0', 'b', '\0', '\0', 'c' };
    CHECK(write_file(TMP_BIN, blob, sizeof(blob)));

    size_t n = 0;
    char *back = read_file_ex(TMP_BIN, &n);
    if (!CHECK(back != NULL)) return;
    CHECK_INT(n, sizeof(blob));
    CHECK(memcmp(back, blob, sizeof(blob)) == 0);
    CHECK_INT(back[sizeof(blob)], 0);   /* terminator past the payload */
    free(back);
    remove(TMP_BIN);
}

TEST(read_file_handles_empty_file) {
    CHECK(write_file(TMP_TXT, "", 0));
    size_t n = 123;
    char *back = read_file_ex(TMP_TXT, &n);
    if (!CHECK(back != NULL)) return;
    CHECK_INT(n, 0);
    CHECK_STR(back, "");
    free(back);
    remove(TMP_TXT);
}

TEST(read_file_missing_path_returns_null) {
    /* The suite runs at LOG_CRITICAL, so the expected error stays quiet. */
    CHECK(read_file("no/such/file/here") == NULL);
    CHECK(!file_exists("no/such/file/here"));
    CHECK_INT(file_size("no/such/file/here"), -1);
}

/* Regression: the buffer used to be sized from ftell(), which reports 0 for
 * the synthetic /proc files, so the content came back empty. */
#ifdef __linux__
TEST(read_file_reads_a_zero_sized_proc_file) {
    CHECK_INT(file_size("/proc/self/status"), 0);
    size_t n = 0;
    char *content = read_file_ex("/proc/self/status", &n);
    if (!CHECK(content != NULL)) return;
    CHECK(n > 0);
    CHECK(strstr(content, "Name:") != NULL);
    free(content);
}

/* Regression: a buffered write only fails at fclose, whose result was ignored,
 * so a full filesystem was reported as a success. */
TEST(write_file_detects_a_failing_flush) {
    if (!file_exists("/dev/full")) return;   /* not available in every sandbox */
    char payload[8192];
    memset(payload, 'x', sizeof(payload));
    CHECK(!write_file("/dev/full", payload, sizeof(payload)));
}
#endif

/* --- dynamic arrays ------------------------------------------------------- */

typedef struct { int *items; size_t count, capacity; } IntArray;

TEST(da_append_and_iterate) {
    IntArray a = {0};
    for (int i = 0; i < 1000; i++) da_append(&a, i);
    CHECK_INT(a.count, 1000);
    CHECK(a.capacity >= 1000);

    long sum = 0;
    da_foreach(int, it, &a) sum += *it;
    CHECK_INT(sum, 999L * 1000 / 2);

    CHECK_INT(da_first(&a), 0);
    CHECK_INT(da_last(&a), 999);
    CHECK_INT(da_pop(&a), 999);
    CHECK_INT(a.count, 999);
    da_free(&a);
    CHECK(a.items == NULL);
    CHECK_INT(a.capacity, 0);
}

TEST(da_append_many_and_remove) {
    int src[] = { 10, 20, 30, 40 };
    IntArray a = {0};
    da_append_many(&a, src, 4);
    CHECK_INT(a.count, 4);

    const int *nothing = NULL;     /* must not reach memcpy with a NULL source */
    da_append_many(&a, nothing, 0);
    CHECK_INT(a.count, 4);

    da_remove_unordered(&a, 0);    /* last element takes the hole */
    CHECK_INT(a.count, 3);
    CHECK_INT(a.items[0], 40);
    CHECK(da_contains(&a, 20));
    CHECK(!da_contains(&a, 10));
    da_free(&a);
}

TEST(da_reserve_is_idempotent) {
    IntArray a = {0};
    da_reserve(&a, 10);
    size_t cap = a.capacity;
    int *items = a.items;
    da_reserve(&a, 10);
    CHECK_INT(a.capacity, cap);
    CHECK(a.items == items);       /* no reallocation when it already fits */
    CHECK_INT(a.count, 0);
    da_free(&a);
}

int main(void) {
    log_set_level(LOG_CRITICAL);
    utest_begin("files");
    RUN(log_level_filters_lower_levels);
    RUN(log_omits_ansi_codes_when_not_a_tty);
    RUN(write_then_read_roundtrip);
    RUN(read_file_reports_size_and_keeps_embedded_nuls);
    RUN(read_file_handles_empty_file);
    RUN(read_file_missing_path_returns_null);
#ifdef __linux__
    RUN(read_file_reads_a_zero_sized_proc_file);
    RUN(write_file_detects_a_failing_flush);
#endif
    RUN(da_append_and_iterate);
    RUN(da_append_many_and_remove);
    RUN(da_reserve_is_idempotent);
    return utest_report();
}
