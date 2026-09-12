/*
 * Filesystem: directories, copying, timestamps and rebuild detection.
 *
 * Every test works inside SANDBOX, created fresh and torn down at the end, so
 * the suite never touches anything it did not make.
 */

#define UTILS_IMPLEMENTATION
#include "../utils.h"
#include "utest.h"

#define SANDBOX "utest-tmp-fs"

static void write_text(const char *path, const char *text) {
    write_file(path, text, strlen(text));
}

static char *join(const char *a, const char *b) {
    static char buf[512];
    return path_join(buf, sizeof(buf), a, b);
}

/* --- kind and existence --------------------------------------------------- */

TEST(file_kind_distinguishes_files_from_directories) {
    char file[512];
    path_join(file, sizeof(file), SANDBOX, "plain.txt");
    write_text(file, "x");

    CHECK_INT(file_kind(file), FILE_KIND_REGULAR);
    CHECK_INT(file_kind(SANDBOX), FILE_KIND_DIRECTORY);
    CHECK_INT(file_kind(join(SANDBOX, "absent")), FILE_KIND_NONE);

    CHECK(dir_exists(SANDBOX));
    CHECK(!dir_exists(file));
    CHECK(!dir_exists(join(SANDBOX, "absent")));

    /* file_exists is regular-only, and must not be fooled by a directory. */
    CHECK(file_exists(file));
    CHECK(!file_exists(SANDBOX));

    remove_file(file);
}

/* --- mkdir_p -------------------------------------------------------------- */

TEST(mkdir_p_creates_the_whole_chain) {
    char deep[512];
    path_join(deep, sizeof(deep), SANDBOX, "a/b/c/d");

    CHECK(mkdir_p(deep));
    CHECK(dir_exists(deep));
    CHECK(dir_exists(join(SANDBOX, "a/b")));

    CHECK(mkdir_p(deep));          /* idempotent */
    CHECK(mkdir_p(SANDBOX));       /* already there */
}

TEST(mkdir_p_handles_odd_paths) {
    CHECK(mkdir_p(join(SANDBOX, "trailing/")));
    CHECK(dir_exists(join(SANDBOX, "trailing")));

    CHECK(mkdir_p(join(SANDBOX, "double//separator")));
    CHECK(dir_exists(join(SANDBOX, "double/separator")));

    CHECK(!mkdir_p(""));           /* empty path is an error, not a no-op */
    CHECK(!mkdir_p(NULL));

    /* A path whose parent is a regular file cannot be created. */
    char blocker[512];
    path_join(blocker, sizeof(blocker), SANDBOX, "blocker");
    write_text(blocker, "x");
    CHECK(!mkdir_p(join(SANDBOX, "blocker/child")));
    remove_file(blocker);
}

/* --- copy, remove, rename ------------------------------------------------- */

TEST(copy_file_reproduces_the_content) {
    char src[512], dst[512];
    path_join(src, sizeof(src), SANDBOX, "src.bin");
    path_join(dst, sizeof(dst), SANDBOX, "dst.bin");

    /* Larger than the copy buffer, and with embedded NUL bytes. */
    enum { N = 200000 };
    char *payload = malloc(N);
    if (!CHECK(payload != NULL)) return;
    for (int i = 0; i < N; i++) payload[i] = (char)(i % 251);
    CHECK(write_file(src, payload, N));

    CHECK(copy_file(src, dst));
    size_t n = 0;
    char *back = read_file_ex(dst, &n);
    if (CHECK(back != NULL)) {
        CHECK_INT(n, N);
        CHECK(memcmp(back, payload, N) == 0);
        free(back);
    }

    CHECK(copy_file(src, dst));    /* overwrites an existing destination */
    CHECK(!copy_file(join(SANDBOX, "absent"), dst));

    free(payload);
    remove_file(src);
    remove_file(dst);
}

TEST(copy_file_carries_the_permission_bits) {
#ifndef _WIN32
    char src[512], dst[512];
    path_join(src, sizeof(src), SANDBOX, "exec.sh");
    path_join(dst, sizeof(dst), SANDBOX, "exec-copy.sh");
    write_text(src, "#!/bin/sh\nexit 0\n");
    CHECK_INT(chmod(src, 0755), 0);

    CHECK(copy_file(src, dst));
    struct stat st;
    if (CHECK(stat(dst, &st) == 0))
        CHECK_INT(st.st_mode & 0777, 0755);

    remove_file(src);
    remove_file(dst);
#endif
}

/* remove_file must refuse a directory and remove_dir must refuse a file, on
 * every platform. POSIX remove() blurs that line, Windows remove() draws it
 * elsewhere again, which is why neither is used. */
TEST(remove_file_and_remove_dir_do_not_overlap) {
    char dir[512], file[512];
    path_join(dir, sizeof(dir), SANDBOX, "removable");
    CHECK(mkdir_p(dir));
    path_join(file, sizeof(file), SANDBOX, "removable.txt");
    write_text(file, "x");

    CHECK(!remove_file(dir));        /* a directory is not a file */
    CHECK(dir_exists(dir));
    CHECK(!remove_dir(file));        /* and a file is not a directory */
    CHECK(file_exists(file));

    CHECK(remove_dir(dir));          /* empty, so it goes */
    CHECK(!dir_exists(dir));
    CHECK(remove_file(file));
    CHECK(!file_exists(file));

    CHECK(!remove_dir(dir));         /* already gone */

    /* A directory with something in it stays put. */
    char nested[512];
    path_join(nested, sizeof(nested), SANDBOX, "full");
    CHECK(mkdir_p(nested));
    write_text(join(nested, "child"), "x");
    CHECK(!remove_dir(nested));
    CHECK(dir_exists(nested));
}

TEST(remove_and_rename) {
    char a[512], b[512];
    path_join(a, sizeof(a), SANDBOX, "a.txt");
    path_join(b, sizeof(b), SANDBOX, "b.txt");

    write_text(a, "content");
    CHECK(rename_file(a, b));
    CHECK(!file_exists(a));
    CHECK(file_exists(b));

    /* Renaming over an existing file must replace it, not fail. */
    write_text(a, "newer");
    CHECK(rename_file(a, b));
    char *text = read_file(b);
    if (CHECK(text != NULL)) { CHECK_STR(text, "newer"); free(text); }

    CHECK(remove_file(b));
    CHECK(!file_exists(b));
    CHECK(!remove_file(b));        /* already gone */
    CHECK(!rename_file(join(SANDBOX, "absent"), b));
}

/* --- read_dir ------------------------------------------------------------- */

TEST(read_dir_lists_sorted_entries) {
    char dir[512];
    path_join(dir, sizeof(dir), SANDBOX, "listing");
    CHECK(mkdir_p(dir));

    write_text(join(dir, "zebra"), "z");
    write_text(join(dir, "alpha"), "a");
    write_text(join(dir, "middle"), "m");
    CHECK(mkdir_p(join(dir, "subdir")));

    FileList list = {0};
    if (!CHECK(read_dir(dir, &list))) return;

    CHECK_INT(list.count, 4);
    if (list.count == 4) {
        CHECK_STR(list.items[0], "alpha");    /* sorted, and no "." or ".." */
        CHECK_STR(list.items[1], "middle");
        CHECK_STR(list.items[2], "subdir");
        CHECK_STR(list.items[3], "zebra");
    }
    file_list_free(&list);
    CHECK(list.items == NULL);
}

TEST(read_dir_appends_and_leaves_the_list_alone_on_error) {
    FileList list = {0};
    da_append(&list, utils__strdup("sentinel"));

    CHECK(read_dir(SANDBOX, &list));
    CHECK(list.count > 1);
    CHECK_STR(list.items[0], "sentinel");     /* appended, not replaced */
    size_t after_success = list.count;

    CHECK(!read_dir(join(SANDBOX, "absent"), &list));
    CHECK_INT(list.count, after_success);     /* untouched on failure */

    char plain[512];
    path_join(plain, sizeof(plain), SANDBOX, "notadir.txt");
    write_text(plain, "x");
    CHECK(!read_dir(plain, &list));           /* a regular file is not a dir */
    remove_file(plain);

    file_list_free(&list);
}

TEST(read_dir_handles_an_empty_directory) {
    char dir[512];
    path_join(dir, sizeof(dir), SANDBOX, "empty");
    CHECK(mkdir_p(dir));

    FileList list = {0};
    CHECK(read_dir(dir, &list));
    CHECK_INT(list.count, 0);
    file_list_free(&list);
}

/* --- timestamps and rebuild detection ------------------------------------- */

TEST(file_mtime_is_a_plausible_epoch_time) {
    char f[512];
    path_join(f, sizeof(f), SANDBOX, "stamped.txt");
    write_text(f, "x");

    int64_t t = file_mtime(f);
    CHECK(t > 1700000000);                    /* after November 2023 */
    CHECK(t < (int64_t)time(NULL) + 5);
    CHECK_INT(file_mtime(join(SANDBOX, "absent")), -1);

    remove_file(f);
}

TEST(needs_rebuild_tracks_staleness) {
    char in1[512], in2[512], out[512];
    path_join(in1, sizeof(in1), SANDBOX, "in1.c");
    path_join(in2, sizeof(in2), SANDBOX, "in2.c");
    path_join(out, sizeof(out), SANDBOX, "out.o");
    const char *inputs[] = { in1, in2 };

    write_text(in1, "one");
    write_text(in2, "two");

    /* A missing output always needs building. */
    remove(out);
    CHECK_INT(needs_rebuild(out, inputs, 2), 1);

    /* Built after its inputs: up to date. Sub-second resolution is exactly
     * what this checks, so nothing sleeps here. */
    write_text(out, "built");
    CHECK_INT(needs_rebuild(out, inputs, 2), 0);
    CHECK_INT(needs_rebuild1(out, in1), 0);

    /* Touching one input makes it stale again. */
    write_text(in2, "two modified");
    CHECK_INT(needs_rebuild(out, inputs, 2), 1);
    CHECK_INT(needs_rebuild1(out, in2), 1);
    CHECK_INT(needs_rebuild1(out, in1), 0);

    /* An unreadable input is an error, not "up to date". */
    const char *missing[] = { in1, "no/such/input.c" };
    CHECK_INT(needs_rebuild(out, missing, 2), -1);

    /* No inputs at all: an existing output is never stale. */
    CHECK_INT(needs_rebuild(out, NULL, 0), 0);

    remove_file(in1);
    remove_file(in2);
    remove_file(out);
}

/* --- teardown ------------------------------------------------------------- */

static void remove_tree(const char *path) {
    FileList list = {0};
    if (!read_dir(path, &list)) return;
    for (size_t i = 0; i < list.count; ++i) {
        char child[512];
        path_join(child, sizeof(child), path, list.items[i]);
        if (file_kind(child) == FILE_KIND_DIRECTORY) remove_tree(child);
        else                                         remove_file(child);
    }
    file_list_free(&list);
    remove_dir(path);
}

int main(void) {
    log_set_level(LOG_CRITICAL);

    remove_tree(SANDBOX);
    if (!mkdir_p(SANDBOX)) {
        fprintf(stderr, "cannot create the sandbox directory\n");
        return 1;
    }

    utest_begin("filesystem");
    RUN(file_kind_distinguishes_files_from_directories);
    RUN(mkdir_p_creates_the_whole_chain);
    RUN(mkdir_p_handles_odd_paths);
    RUN(copy_file_reproduces_the_content);
    RUN(copy_file_carries_the_permission_bits);
    RUN(remove_file_and_remove_dir_do_not_overlap);
    RUN(remove_and_rename);
    RUN(read_dir_lists_sorted_entries);
    RUN(read_dir_appends_and_leaves_the_list_alone_on_error);
    RUN(read_dir_handles_an_empty_directory);
    RUN(file_mtime_is_a_plausible_epoch_time);
    RUN(needs_rebuild_tracks_staleness);
    int rc = utest_report();

    remove_tree(SANDBOX);
    return rc;
}
