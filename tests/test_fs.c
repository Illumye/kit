/*
 * Filesystem: directories, copying, timestamps and rebuild detection.
 *
 * Every test works inside SANDBOX, created fresh and torn down at the end, so
 * the suite never touches anything it did not make.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

#define SANDBOX "utest-tmp-fs"

static void write_text(const char *path, const char *text) {
    kit_fs_write(path, text, strlen(text));
}

static char *join(const char *a, const char *b) {
    static char buf[512];
    return kit_path_join(buf, sizeof(buf), a, b);
}

/* --- kind and existence --------------------------------------------------- */

TEST(fs_kind_distinguishes_files_from_directories) {
    char file[512];
    kit_path_join(file, sizeof(file), SANDBOX, "plain.txt");
    write_text(file, "x");

    CHECK_INT(kit_fs_kind(file), KIT_FILE_KIND_REGULAR);
    CHECK_INT(kit_fs_kind(SANDBOX), KIT_FILE_KIND_DIRECTORY);
    CHECK_INT(kit_fs_kind(join(SANDBOX, "absent")), KIT_FILE_KIND_NONE);

    CHECK(kit_fs_is_dir(SANDBOX));
    CHECK(!kit_fs_is_dir(file));
    CHECK(!kit_fs_is_dir(join(SANDBOX, "absent")));

    /* kit_fs_is_file is regular-only, and must not be fooled by a directory. */
    CHECK(kit_fs_is_file(file));
    CHECK(!kit_fs_is_file(SANDBOX));

    kit_fs_remove(file);
}

/* --- kit_fs_mkdir -------------------------------------------------------------- */

TEST(fs_mkdir_creates_the_whole_chain) {
    char deep[512];
    kit_path_join(deep, sizeof(deep), SANDBOX, "a/b/c/d");

    CHECK(kit_fs_mkdir(deep));
    CHECK(kit_fs_is_dir(deep));
    CHECK(kit_fs_is_dir(join(SANDBOX, "a/b")));

    CHECK(kit_fs_mkdir(deep));          /* idempotent */
    CHECK(kit_fs_mkdir(SANDBOX));       /* already there */
}

TEST(fs_mkdir_handles_odd_paths) {
    CHECK(kit_fs_mkdir(join(SANDBOX, "trailing/")));
    CHECK(kit_fs_is_dir(join(SANDBOX, "trailing")));

    CHECK(kit_fs_mkdir(join(SANDBOX, "double//separator")));
    CHECK(kit_fs_is_dir(join(SANDBOX, "double/separator")));

    CHECK(!kit_fs_mkdir(""));           /* empty path is an error, not a no-op */
    CHECK(!kit_fs_mkdir(NULL));

    /* A path whose parent is a regular file cannot be created. */
    char blocker[512];
    kit_path_join(blocker, sizeof(blocker), SANDBOX, "blocker");
    write_text(blocker, "x");
    CHECK(!kit_fs_mkdir(join(SANDBOX, "blocker/child")));
    kit_fs_remove(blocker);
}

/* --- copy, remove, rename ------------------------------------------------- */

TEST(fs_copy_reproduces_the_content) {
    char src[512], dst[512];
    kit_path_join(src, sizeof(src), SANDBOX, "src.bin");
    kit_path_join(dst, sizeof(dst), SANDBOX, "dst.bin");

    /* Larger than the copy buffer, and with embedded NUL bytes. */
    enum { N = 200000 };
    char *payload = malloc(N);
    if (!CHECK(payload != NULL)) return;
    for (int i = 0; i < N; i++) payload[i] = (char)(i % 251);
    CHECK(kit_fs_write(src, payload, N));

    CHECK(kit_fs_copy(src, dst));
    size_t n = 0;
    char *back = kit_fs_read_sized(dst, &n);
    if (CHECK(back != NULL)) {
        CHECK_INT(n, N);
        CHECK(memcmp(back, payload, N) == 0);
        free(back);
    }

    CHECK(kit_fs_copy(src, dst));    /* overwrites an existing destination */
    CHECK(!kit_fs_copy(join(SANDBOX, "absent"), dst));

    free(payload);
    kit_fs_remove(src);
    kit_fs_remove(dst);
}

TEST(fs_copy_carries_the_permission_bits) {
#ifndef _WIN32
    char src[512], dst[512];
    kit_path_join(src, sizeof(src), SANDBOX, "exec.sh");
    kit_path_join(dst, sizeof(dst), SANDBOX, "exec-copy.sh");
    write_text(src, "#!/bin/sh\nexit 0\n");
    CHECK_INT(chmod(src, 0755), 0);

    CHECK(kit_fs_copy(src, dst));
    struct stat st;
    if (CHECK(stat(dst, &st) == 0))
        CHECK_INT(st.st_mode & 0777, 0755);

    kit_fs_remove(src);
    kit_fs_remove(dst);
#endif
}

/* kit_fs_remove must refuse a directory and kit_fs_rmdir must refuse a file, on
 * every platform. POSIX remove() blurs that line, Windows remove() draws it
 * elsewhere again, which is why neither is used. */
TEST(fs_remove_and_rmdir_do_not_overlap) {
    char dir[512], file[512];
    kit_path_join(dir, sizeof(dir), SANDBOX, "removable");
    CHECK(kit_fs_mkdir(dir));
    kit_path_join(file, sizeof(file), SANDBOX, "removable.txt");
    write_text(file, "x");

    CHECK(!kit_fs_remove(dir));        /* a directory is not a file */
    CHECK(kit_fs_is_dir(dir));
    CHECK(!kit_fs_rmdir(file));        /* and a file is not a directory */
    CHECK(kit_fs_is_file(file));

    CHECK(kit_fs_rmdir(dir));          /* empty, so it goes */
    CHECK(!kit_fs_is_dir(dir));
    CHECK(kit_fs_remove(file));
    CHECK(!kit_fs_is_file(file));

    CHECK(!kit_fs_rmdir(dir));         /* already gone */

    /* A directory with something in it stays put. */
    char nested[512];
    kit_path_join(nested, sizeof(nested), SANDBOX, "full");
    CHECK(kit_fs_mkdir(nested));
    write_text(join(nested, "child"), "x");
    CHECK(!kit_fs_rmdir(nested));
    CHECK(kit_fs_is_dir(nested));
}

TEST(fs_remove_and_rename) {
    char a[512], b[512];
    kit_path_join(a, sizeof(a), SANDBOX, "a.txt");
    kit_path_join(b, sizeof(b), SANDBOX, "b.txt");

    write_text(a, "content");
    CHECK(kit_fs_rename(a, b));
    CHECK(!kit_fs_is_file(a));
    CHECK(kit_fs_is_file(b));

    /* Renaming over an existing file must replace it, not fail. */
    write_text(a, "newer");
    CHECK(kit_fs_rename(a, b));
    char *text = kit_fs_read(b);
    if (CHECK(text != NULL)) { CHECK_STR(text, "newer"); free(text); }

    CHECK(kit_fs_remove(b));
    CHECK(!kit_fs_is_file(b));
    CHECK(!kit_fs_remove(b));        /* already gone */
    CHECK(!kit_fs_rename(join(SANDBOX, "absent"), b));
}

/* --- kit_fs_list ------------------------------------------------------------- */

TEST(fs_list_lists_sorted_entries) {
    char dir[512];
    kit_path_join(dir, sizeof(dir), SANDBOX, "listing");
    CHECK(kit_fs_mkdir(dir));

    write_text(join(dir, "zebra"), "z");
    write_text(join(dir, "alpha"), "a");
    write_text(join(dir, "middle"), "m");
    CHECK(kit_fs_mkdir(join(dir, "subdir")));

    KitFileList list = {0};
    if (!CHECK(kit_fs_list(dir, &list))) return;

    CHECK_INT(list.count, 4);
    if (list.count == 4) {
        CHECK_STR(list.items[0], "alpha");    /* sorted, and no "." or ".." */
        CHECK_STR(list.items[1], "middle");
        CHECK_STR(list.items[2], "subdir");
        CHECK_STR(list.items[3], "zebra");
    }
    kit_file_list_free(&list);
    CHECK(list.items == NULL);
}

TEST(fs_list_appends_and_leaves_the_list_alone_on_error) {
    KitFileList list = {0};
    kit_array_push(&list, kit__strdup("sentinel"));

    CHECK(kit_fs_list(SANDBOX, &list));
    CHECK(list.count > 1);
    CHECK_STR(list.items[0], "sentinel");     /* appended, not replaced */
    size_t after_success = list.count;

    CHECK(!kit_fs_list(join(SANDBOX, "absent"), &list));
    CHECK_INT(list.count, after_success);     /* untouched on failure */

    char plain[512];
    kit_path_join(plain, sizeof(plain), SANDBOX, "notadir.txt");
    write_text(plain, "x");
    CHECK(!kit_fs_list(plain, &list));           /* a regular file is not a dir */
    kit_fs_remove(plain);

    kit_file_list_free(&list);
}

TEST(fs_list_handles_an_empty_directory) {
    char dir[512];
    kit_path_join(dir, sizeof(dir), SANDBOX, "empty");
    CHECK(kit_fs_mkdir(dir));

    KitFileList list = {0};
    CHECK(kit_fs_list(dir, &list));
    CHECK_INT(list.count, 0);
    kit_file_list_free(&list);
}

/* --- timestamps and rebuild detection ------------------------------------- */

TEST(fs_mtime_is_a_plausible_epoch_time) {
    char f[512];
    kit_path_join(f, sizeof(f), SANDBOX, "stamped.txt");
    write_text(f, "x");

    int64_t t = kit_fs_mtime(f);
    CHECK(t > 1700000000);                    /* after November 2023 */
    CHECK(t < (int64_t)time(NULL) + 5);
    CHECK_INT(kit_fs_mtime(join(SANDBOX, "absent")), -1);

    kit_fs_remove(f);
}

TEST(fs_stale_tracks_staleness) {
    char in1[512], in2[512], out[512];
    kit_path_join(in1, sizeof(in1), SANDBOX, "in1.c");
    kit_path_join(in2, sizeof(in2), SANDBOX, "in2.c");
    kit_path_join(out, sizeof(out), SANDBOX, "out.o");
    const char *inputs[] = { in1, in2 };

    write_text(in1, "one");
    write_text(in2, "two");

    /* A missing output always needs building. */
    remove(out);
    CHECK_INT(kit_fs_stale(out, inputs, 2), 1);

    /* Built after its inputs: up to date. Sub-second resolution is exactly
     * what this checks, so nothing sleeps here. */
    write_text(out, "built");
    CHECK_INT(kit_fs_stale(out, inputs, 2), 0);
    CHECK_INT(kit_fs_stale1(out, in1), 0);

    /* Touching one input makes it stale again. */
    write_text(in2, "two modified");
    CHECK_INT(kit_fs_stale(out, inputs, 2), 1);
    CHECK_INT(kit_fs_stale1(out, in2), 1);
    CHECK_INT(kit_fs_stale1(out, in1), 0);

    /* An unreadable input is an error, not "up to date". */
    const char *missing[] = { in1, "no/such/input.c" };
    CHECK_INT(kit_fs_stale(out, missing, 2), -1);

    /* No inputs at all: an existing output is never stale. */
    CHECK_INT(kit_fs_stale(out, NULL, 0), 0);

    kit_fs_remove(in1);
    kit_fs_remove(in2);
    kit_fs_remove(out);
}

/* --- teardown ------------------------------------------------------------- */

static void remove_tree(const char *path) {
    KitFileList list = {0};
    if (!kit_fs_list(path, &list)) return;
    for (size_t i = 0; i < list.count; ++i) {
        char child[512];
        kit_path_join(child, sizeof(child), path, list.items[i]);
        if (kit_fs_kind(child) == KIT_FILE_KIND_DIRECTORY) remove_tree(child);
        else                                         kit_fs_remove(child);
    }
    kit_file_list_free(&list);
    kit_fs_rmdir(path);
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);

    remove_tree(SANDBOX);
    if (!kit_fs_mkdir(SANDBOX)) {
        fprintf(stderr, "cannot create the sandbox directory\n");
        return 1;
    }

    utest_begin("filesystem");
    RUN(fs_kind_distinguishes_files_from_directories);
    RUN(fs_mkdir_creates_the_whole_chain);
    RUN(fs_mkdir_handles_odd_paths);
    RUN(fs_copy_reproduces_the_content);
    RUN(fs_copy_carries_the_permission_bits);
    RUN(fs_remove_and_rmdir_do_not_overlap);
    RUN(fs_remove_and_rename);
    RUN(fs_list_lists_sorted_entries);
    RUN(fs_list_appends_and_leaves_the_list_alone_on_error);
    RUN(fs_list_handles_an_empty_directory);
    RUN(fs_mtime_is_a_plausible_epoch_time);
    RUN(fs_stale_tracks_staleness);
    int rc = utest_report();

    remove_tree(SANDBOX);
    return rc;
}
