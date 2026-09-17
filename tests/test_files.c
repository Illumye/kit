/*
 * Files, logging and dynamic arrays.
 * Includes the regression tests for the kit_fs_read and kit_fs_write defects.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

#define TMP_TXT "utest-tmp-file.txt"
#define TMP_BIN "utest-tmp-file.bin"

/* --- logging -------------------------------------------------------------- */

TEST(log_level_filters_lower_levels) {
    FILE *fp = fopen(TMP_TXT, "w+");
    if (!CHECK(fp != NULL)) return;

    kit_log_set_output(fp);
    kit_log_set_level(KIT_LOG_WARN);
    KIT_DEBUG("invisible");
    KIT_INFO("invisible");
    KIT_ERROR("visible");
    fflush(fp);

    long size = ftell(fp);
    fclose(fp);
    kit_log_set_output(NULL);
    kit_log_set_level(KIT_LOG_CRITICAL);

    char *content = kit_fs_read(TMP_TXT, NULL);
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

    kit_log_set_output(fp);
    kit_log_set_level(KIT_LOG_ERROR);
    kit_log_set_color(KIT_LOG_COLOR_AUTO);
    KIT_ERROR("plain text please");
    fclose(fp);
    kit_log_set_output(NULL);
    kit_log_set_level(KIT_LOG_CRITICAL);

    char *content = kit_fs_read(TMP_TXT, NULL);
    if (!CHECK(content != NULL)) return;
    CHECK(strchr(content, '\x1b') == NULL);
    CHECK(strstr(content, "plain text please") != NULL);
    free(content);

    /* ALWAYS still forces colour on a non-tty, for pagers that render it. */
    fp = fopen(TMP_TXT, "w");
    if (!CHECK(fp != NULL)) return;
    kit_log_set_output(fp);
    kit_log_set_level(KIT_LOG_ERROR);
    kit_log_set_color(KIT_LOG_COLOR_ALWAYS);
    KIT_ERROR("coloured");
    fclose(fp);
    kit_log_set_output(NULL);
    kit_log_set_level(KIT_LOG_CRITICAL);

    content = kit_fs_read(TMP_TXT, NULL);
    if (!CHECK(content != NULL)) return;
    CHECK(strchr(content, '\x1b') != NULL);
    free(content);

    kit_log_set_color(KIT_LOG_COLOR_AUTO);
    remove(TMP_TXT);
}

/* Reads back what one KIT_LOG call wrote, with colour off. Binary mode, because
 * Windows text mode would turn every \n into \r\n and the comparisons below
 * are byte exact. */
static char *log_once(unsigned fields, KitLogLevel level, const char *message) {
    FILE *fp = fopen(TMP_TXT, "wb");
    if (!fp) return NULL;

    kit_log_set_output(fp);
    kit_log_set_level(KIT_LOG_DEBUG);
    kit_log_set_color(KIT_LOG_COLOR_NEVER);
    kit_log_set_fields(fields);
    kit__log(level, "src.c", 7, "%s", message);
    fclose(fp);

    kit_log_set_output(NULL);
    kit_log_set_level(KIT_LOG_CRITICAL);
    kit_log_set_fields(KIT_LOG_FIELDS_DEFAULT);
    return kit_fs_read(TMP_TXT, NULL);
}

TEST(log_fields_select_the_prefix) {
    char *line = log_once(KIT_LOG_FIELDS_NONE, KIT_LOG_INFO, "bare");
    if (CHECK(line != NULL)) {
        CHECK_STR(line, "bare\n");          /* nothing but the message */
        free(line);
    }

    line = log_once(KIT_LOG_FIELD_LEVEL, KIT_LOG_WARN, "msg");
    if (CHECK(line != NULL)) {
        CHECK_STR(line, "[WARN] msg\n");
        free(line);
    }

    line = log_once(KIT_LOG_FIELD_LOCATION, KIT_LOG_INFO, "msg");
    if (CHECK(line != NULL)) {
        CHECK_STR(line, "[src.c:7] msg\n");
        free(line);
    }

    line = log_once(KIT_LOG_FIELD_USER, KIT_LOG_INFO, "msg");
    if (CHECK(line != NULL)) {
        CHECK(strstr(line, "msg") != NULL);
        CHECK_INT(line[0], '[');
        CHECK(strstr(line, "[]") == NULL);  /* never an empty user field */
        free(line);
    }

    line = log_once(KIT_LOG_FIELD_DATE, KIT_LOG_INFO, "msg");
    if (CHECK(line != NULL)) {
        CHECK_INT(strlen(line), strlen("[2026-09-12 (Sat)] msg\n"));
        CHECK(strstr(line, "-") != NULL);
        free(line);
    }

    /* The default must stay exactly what it has always been. */
    line = log_once(KIT_LOG_FIELDS_DEFAULT, KIT_LOG_ERROR, "msg");
    if (CHECK(line != NULL)) {
        CHECK_INT(strlen(line), strlen("[14:05:42] [ERROR] [src.c:7] msg\n"));
        CHECK(strstr(line, "[ERROR] [src.c:7] msg") != NULL);
        free(line);
    }
    remove(TMP_TXT);
}

/* Each level macro must reach its own level, and the source location must be
 * the caller's line rather than a line inside the macro. */
TEST(log_level_macros_map_to_their_levels) {
    FILE *fp = fopen(TMP_TXT, "wb");
    if (!CHECK(fp != NULL)) return;

    kit_log_set_output(fp);
    kit_log_set_level(KIT_LOG_DEBUG);
    kit_log_set_color(KIT_LOG_COLOR_NEVER);
    kit_log_set_fields(KIT_LOG_FIELD_LEVEL);

    KIT_DEBUG("a");
    KIT_INFO("b %d", 2);
    KIT_WARN("c");
    KIT_ERROR("d");
    KIT_CRITICAL("e");
    kit_log_set_fields(KIT_LOG_FIELD_LOCATION);
    int line = __LINE__; KIT_INFO("here");
    fclose(fp);

    kit_log_set_output(NULL);
    kit_log_set_level(KIT_LOG_CRITICAL);
    kit_log_set_fields(KIT_LOG_FIELDS_DEFAULT);

    char *text = kit_fs_read(TMP_TXT, NULL);
    if (!CHECK(text != NULL)) return;
    char expected[256];
    snprintf(expected, sizeof(expected),
             "[DEBUG] a\n[INFO] b 2\n[WARN] c\n[ERROR] d\n[CRIT] e\n[%s:%d] here\n",
             __FILE__, line);
    CHECK_STR(text, expected);
    free(text);
    kit_fs_remove(TMP_TXT, NULL);
}

TEST(log_field_count_numbers_the_records) {
    FILE *fp = fopen(TMP_TXT, "w");
    if (!CHECK(fp != NULL)) return;

    kit_log_set_output(fp);
    kit_log_set_level(KIT_LOG_DEBUG);
    kit_log_set_color(KIT_LOG_COLOR_NEVER);
    kit_log_set_fields(KIT_LOG_FIELD_COUNT);

    unsigned long long first = 0;
    for (int i = 0; i < 3; i++) KIT_INFO("tick");
    fclose(fp);
    kit_log_set_output(NULL);
    kit_log_set_level(KIT_LOG_CRITICAL);
    kit_log_set_fields(KIT_LOG_FIELDS_DEFAULT);

    char *text = kit_fs_read(TMP_TXT, NULL);
    if (!CHECK(text != NULL)) return;

    /* Consecutive, whatever the counter started at. */
    KitStr rest = KIT_STR(text), line;
    int seen = 0;
    while (kit_str_next(&rest, '\n', &line)) {
        if (line.count == 0) continue;
        uint64_t n = 0;
        KitStr digits = kit_str_cut(&line, ']');
        kit_str_take(&digits, 1);            /* drop the '[', in place */
        if (!CHECK(kit_str_to_u64(digits, &n))) break;
        if (seen == 0) first = n;
        else           CHECK_INT(n, first + (unsigned long long)seen);
        seen++;
    }
    CHECK_INT(seen, 3);

    free(text);
    remove(TMP_TXT);
}

/* --- files ---------------------------------------------------------------- */

TEST(fs_write_then_read_roundtrip) {
    const char *payload = "hello\nworld\n";
    CHECK(kit_fs_write(TMP_TXT, payload, strlen(payload), NULL));
    CHECK_INT(kit_fs_size(TMP_TXT, NULL), (int64_t)strlen(payload));
    CHECK(kit_fs_is_file(TMP_TXT));

    char *back = kit_fs_read(TMP_TXT, NULL);
    if (!CHECK(back != NULL)) return;
    CHECK_STR(back, payload);
    free(back);
    remove(TMP_TXT);
}

TEST(fs_read_reports_size_and_keeps_embedded_nuls) {
    const char blob[] = { 'a', '\0', 'b', '\0', '\0', 'c' };
    CHECK(kit_fs_write(TMP_BIN, blob, sizeof(blob), NULL));

    size_t n = 0;
    char *back = kit_fs_read_sized(TMP_BIN, &n, NULL);
    if (!CHECK(back != NULL)) return;
    CHECK_INT(n, sizeof(blob));
    CHECK(memcmp(back, blob, sizeof(blob)) == 0);
    CHECK_INT(back[sizeof(blob)], 0);   /* terminator past the payload */
    free(back);
    remove(TMP_BIN);
}

TEST(fs_read_handles_empty_file) {
    CHECK(kit_fs_write(TMP_TXT, "", 0, NULL));
    size_t n = 123;
    char *back = kit_fs_read_sized(TMP_TXT, &n, NULL);
    if (!CHECK(back != NULL)) return;
    CHECK_INT(n, 0);
    CHECK_STR(back, "");
    free(back);
    remove(TMP_TXT);
}

TEST(fs_read_missing_path_returns_null) {
    /* The suite runs at KIT_LOG_CRITICAL, so the expected error stays quiet. */
    CHECK(kit_fs_read("no/such/file/here", NULL) == NULL);
    CHECK(!kit_fs_is_file("no/such/file/here"));
    CHECK_INT(kit_fs_size("no/such/file/here", NULL), -1);
}

/* Regression: the buffer used to be sized from ftell(), which reports 0 for
 * the synthetic /proc files, so the content came back empty. */
#ifdef __linux__
TEST(fs_read_reads_a_zero_sized_proc_file) {
    CHECK_INT(kit_fs_size("/proc/self/status", NULL), 0);
    size_t n = 0;
    char *content = kit_fs_read_sized("/proc/self/status", &n, NULL);
    if (!CHECK(content != NULL)) return;
    CHECK(n > 0);
    CHECK(strstr(content, "Name:") != NULL);
    free(content);
}

/* Regression: a buffered write only fails at fclose, whose result was ignored,
 * so a full filesystem was reported as a success. */
TEST(fs_write_detects_a_failing_flush) {
    if (!kit_fs_is_file("/dev/full")) return;   /* not available in every sandbox */
    char payload[8192];
    memset(payload, 'x', sizeof(payload));
    CHECK(!kit_fs_write("/dev/full", payload, sizeof(payload), NULL));
}
#endif

/* --- dynamic arrays ------------------------------------------------------- */

typedef struct { int *items; size_t count, capacity; } IntArray;

TEST(array_append_and_iterate) {
    IntArray a = {0};
    for (int i = 0; i < 1000; i++) kit_array_push(&a, i);
    CHECK_INT(a.count, 1000);
    CHECK(a.capacity >= 1000);

    long sum = 0;
    kit_array_each(int, it, &a) sum += *it;
    CHECK_INT(sum, 999L * 1000 / 2);

    CHECK_INT(kit_array_first(&a), 0);
    CHECK_INT(kit_array_last(&a), 999);
    CHECK_INT(kit_array_pop(&a), 999);
    CHECK_INT(a.count, 999);
    kit_array_free(&a);
    CHECK(a.items == NULL);
    CHECK_INT(a.capacity, 0);
}

TEST(array_append_many_and_remove) {
    int src[] = { 10, 20, 30, 40 };
    IntArray a = {0};
    kit_array_push_many(&a, src, 4);
    CHECK_INT(a.count, 4);

    const int *nothing = NULL;     /* must not reach memcpy with a NULL source */
    kit_array_push_many(&a, nothing, 0);
    CHECK_INT(a.count, 4);

    kit_array_swap_remove(&a, 0);    /* last element takes the hole */
    CHECK_INT(a.count, 3);
    CHECK_INT(a.items[0], 40);
#ifdef kit_array_contains          /* absent where GNU statement expressions are */
    CHECK(kit_array_contains(&a, 20));
    CHECK(!kit_array_contains(&a, 10));
#endif

    /* The portable counterpart reports the position, count when absent. */
    size_t at;
    kit_array_find(&a, 20, at);
    CHECK_INT(at, 1);
    kit_array_find(&a, 40, at);
    CHECK_INT(at, 0);              /* first match */
    kit_array_find(&a, 10, at);
    CHECK_INT(at, a.count);        /* absent */
    kit_array_free(&a);
}

TEST(array_reserve_is_idempotent) {
    IntArray a = {0};
    kit_array_reserve(&a, 10);
    size_t cap = a.capacity;
    int *items = a.items;
    kit_array_reserve(&a, 10);
    CHECK_INT(a.capacity, cap);
    CHECK(a.items == items);       /* no reallocation when it already fits */
    CHECK_INT(a.count, 0);
    kit_array_free(&a);
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("files");
    RUN(log_level_filters_lower_levels);
    RUN(log_omits_ansi_codes_when_not_a_tty);
    RUN(log_fields_select_the_prefix);
    RUN(log_level_macros_map_to_their_levels);
    RUN(log_field_count_numbers_the_records);
    RUN(fs_write_then_read_roundtrip);
    RUN(fs_read_reports_size_and_keeps_embedded_nuls);
    RUN(fs_read_handles_empty_file);
    RUN(fs_read_missing_path_returns_null);
#ifdef __linux__
    RUN(fs_read_reads_a_zero_sized_proc_file);
    RUN(fs_write_detects_a_failing_flush);
#endif
    RUN(array_append_and_iterate);
    RUN(array_append_many_and_remove);
    RUN(array_reserve_is_idempotent);
    return utest_report();
}
