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
    kit_fs_write(path, text, strlen(text), NULL);
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

    kit_fs_remove(file, NULL);
}

/* --- kit_fs_mkdir -------------------------------------------------------------- */

TEST(fs_mkdir_creates_the_whole_chain) {
    char deep[512];
    kit_path_join(deep, sizeof(deep), SANDBOX, "a/b/c/d");

    CHECK(kit_fs_mkdir(deep, NULL));
    CHECK(kit_fs_is_dir(deep));
    CHECK(kit_fs_is_dir(join(SANDBOX, "a/b")));

    CHECK(kit_fs_mkdir(deep, NULL));          /* idempotent */
    CHECK(kit_fs_mkdir(SANDBOX, NULL));       /* already there */
}

TEST(fs_mkdir_handles_odd_paths) {
    CHECK(kit_fs_mkdir(join(SANDBOX, "trailing/"), NULL));
    CHECK(kit_fs_is_dir(join(SANDBOX, "trailing")));

    CHECK(kit_fs_mkdir(join(SANDBOX, "double//separator"), NULL));
    CHECK(kit_fs_is_dir(join(SANDBOX, "double/separator")));

    CHECK(!kit_fs_mkdir("", NULL));           /* empty path is an error, not a no-op */
    CHECK(!kit_fs_mkdir(NULL, NULL));

    /* A path whose parent is a regular file cannot be created. */
    char blocker[512];
    kit_path_join(blocker, sizeof(blocker), SANDBOX, "blocker");
    write_text(blocker, "x");
    CHECK(!kit_fs_mkdir(join(SANDBOX, "blocker/child"), NULL));
    kit_fs_remove(blocker, NULL);
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
    CHECK(kit_fs_write(src, payload, N, NULL));

    CHECK(kit_fs_copy(src, dst, NULL));
    size_t n = 0;
    char *back = kit_fs_read_sized(dst, &n, NULL);
    if (CHECK(back != NULL)) {
        CHECK_INT(n, N);
        CHECK(memcmp(back, payload, N) == 0);
        free(back);
    }

    CHECK(kit_fs_copy(src, dst, NULL));    /* overwrites an existing destination */
    CHECK(!kit_fs_copy(join(SANDBOX, "absent"), dst, NULL));

    free(payload);
    kit_fs_remove(src, NULL);
    kit_fs_remove(dst, NULL);
}

TEST(fs_copy_carries_the_permission_bits) {
#ifndef _WIN32
    char src[512], dst[512];
    kit_path_join(src, sizeof(src), SANDBOX, "exec.sh");
    kit_path_join(dst, sizeof(dst), SANDBOX, "exec-copy.sh");
    write_text(src, "#!/bin/sh\nexit 0\n");
    CHECK_INT(chmod(src, 0755), 0);

    CHECK(kit_fs_copy(src, dst, NULL));
    struct stat st;
    if (CHECK(stat(dst, &st) == 0))
        CHECK_INT(st.st_mode & 0777, 0755);

    kit_fs_remove(src, NULL);
    kit_fs_remove(dst, NULL);
#endif
}

/* kit_fs_remove must refuse a directory and kit_fs_rmdir must refuse a file, on
 * every platform. POSIX remove() blurs that line, Windows remove() draws it
 * elsewhere again, which is why neither is used. */
TEST(fs_remove_and_rmdir_do_not_overlap) {
    char dir[512], file[512];
    kit_path_join(dir, sizeof(dir), SANDBOX, "removable");
    CHECK(kit_fs_mkdir(dir, NULL));
    kit_path_join(file, sizeof(file), SANDBOX, "removable.txt");
    write_text(file, "x");

    CHECK(!kit_fs_remove(dir, NULL));        /* a directory is not a file */
    CHECK(kit_fs_is_dir(dir));
    CHECK(!kit_fs_rmdir(file, NULL));        /* and a file is not a directory */
    CHECK(kit_fs_is_file(file));

    CHECK(kit_fs_rmdir(dir, NULL));          /* empty, so it goes */
    CHECK(!kit_fs_is_dir(dir));
    CHECK(kit_fs_remove(file, NULL));
    CHECK(!kit_fs_is_file(file));

    CHECK(!kit_fs_rmdir(dir, NULL));         /* already gone */

    /* A directory with something in it stays put. */
    char nested[512];
    kit_path_join(nested, sizeof(nested), SANDBOX, "full");
    CHECK(kit_fs_mkdir(nested, NULL));
    write_text(join(nested, "child"), "x");
    CHECK(!kit_fs_rmdir(nested, NULL));
    CHECK(kit_fs_is_dir(nested));
}

TEST(fs_remove_and_rename) {
    char a[512], b[512];
    kit_path_join(a, sizeof(a), SANDBOX, "a.txt");
    kit_path_join(b, sizeof(b), SANDBOX, "b.txt");

    write_text(a, "content");
    CHECK(kit_fs_rename(a, b, NULL));
    CHECK(!kit_fs_is_file(a));
    CHECK(kit_fs_is_file(b));

    /* Renaming over an existing file must replace it, not fail. */
    write_text(a, "newer");
    CHECK(kit_fs_rename(a, b, NULL));
    char *text = kit_fs_read(b, NULL);
    if (CHECK(text != NULL)) { CHECK_STR(text, "newer"); free(text); }

    CHECK(kit_fs_remove(b, NULL));
    CHECK(!kit_fs_is_file(b));
    CHECK(!kit_fs_remove(b, NULL));        /* already gone */
    CHECK(!kit_fs_rename(join(SANDBOX, "absent"), b, NULL));
}

/* --- kit_fs_list ------------------------------------------------------------- */

TEST(fs_list_lists_sorted_entries) {
    char dir[512];
    kit_path_join(dir, sizeof(dir), SANDBOX, "listing");
    CHECK(kit_fs_mkdir(dir, NULL));

    write_text(join(dir, "zebra"), "z");
    write_text(join(dir, "alpha"), "a");
    write_text(join(dir, "middle"), "m");
    CHECK(kit_fs_mkdir(join(dir, "subdir"), NULL));

    KitFileList list = {0};
    if (!CHECK(kit_fs_list(dir, &list, NULL))) return;

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

    CHECK(kit_fs_list(SANDBOX, &list, NULL));
    CHECK(list.count > 1);
    CHECK_STR(list.items[0], "sentinel");     /* appended, not replaced */
    size_t after_success = list.count;

    CHECK(!kit_fs_list(join(SANDBOX, "absent"), &list, NULL));
    CHECK_INT(list.count, after_success);     /* untouched on failure */

    char plain[512];
    kit_path_join(plain, sizeof(plain), SANDBOX, "notadir.txt");
    write_text(plain, "x");
    CHECK(!kit_fs_list(plain, &list, NULL));           /* a regular file is not a dir */
    kit_fs_remove(plain, NULL);

    kit_file_list_free(&list);
}

TEST(fs_list_handles_an_empty_directory) {
    char dir[512];
    kit_path_join(dir, sizeof(dir), SANDBOX, "empty");
    CHECK(kit_fs_mkdir(dir, NULL));

    KitFileList list = {0};
    CHECK(kit_fs_list(dir, &list, NULL));
    CHECK_INT(list.count, 0);
    kit_file_list_free(&list);
}

/* --- timestamps and rebuild detection ------------------------------------- */

TEST(fs_mtime_is_a_plausible_epoch_time) {
    char f[512];
    kit_path_join(f, sizeof(f), SANDBOX, "stamped.txt");
    write_text(f, "x");

    int64_t t = kit_fs_mtime(f, NULL);
    CHECK(t > 1700000000);                    /* after November 2023 */
    CHECK(t < (int64_t)time(NULL) + 5);
    CHECK_INT(kit_fs_mtime(join(SANDBOX, "absent"), NULL), -1);

    kit_fs_remove(f, NULL);
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
    CHECK_INT(kit_fs_stale(out, inputs, 2, NULL), 1);

    /* Built after its inputs: up to date. Sub-second resolution is exactly
     * what this checks, so nothing sleeps here. */
    write_text(out, "built");
    CHECK_INT(kit_fs_stale(out, inputs, 2, NULL), 0);
    CHECK_INT(kit_fs_stale1(out, in1, NULL), 0);

    /* Touching one input makes it stale again. */
    write_text(in2, "two modified");
    CHECK_INT(kit_fs_stale(out, inputs, 2, NULL), 1);
    CHECK_INT(kit_fs_stale1(out, in2, NULL), 1);
    CHECK_INT(kit_fs_stale1(out, in1, NULL), 0);

    /* An unreadable input is an error, not "up to date". */
    const char *missing[] = { in1, "no/such/input.c" };
    CHECK_INT(kit_fs_stale(out, missing, 2, NULL), -1);

    /* No inputs at all: an existing output is never stale. */
    CHECK_INT(kit_fs_stale(out, NULL, 0, NULL), 0);

    kit_fs_remove(in1, NULL);
    kit_fs_remove(in2, NULL);
    kit_fs_remove(out, NULL);
}

/* --- failures ------------------------------------------------------------ */

/* The contract of the error module, applied: every failure carries a code the
 * caller can act on, and a message naming what was being done to which path.
 * The codes are the same on every platform even where the systems disagree. */
static bool failed_with(const KitError *err, KitErrorCode code, const char *fragment) {
    bool ok = (err->code == code) && strstr(err->message, fragment) != NULL;
    if (!ok)
        printf("      expected %s mentioning \"%s\", got %s: \"%s\"\n",
               kit_error_code_name(code), fragment,
               kit_error_code_name(err->code), err->message);
    return ok;
}

TEST(fs_missing_paths_are_not_found) {
    KitError err = KIT_ZEROED;
    /* A copy: join() hands out one static buffer, reused by the calls below. */
    char missing[512];
    kit_path_join(missing, sizeof(missing), SANDBOX, "absent.txt");

    CHECK(kit_fs_read(missing, &err) == NULL);
    CHECK(failed_with(&err, KIT_ERR_NOT_FOUND, "absent.txt"));
    CHECK(err.native != 0);

    kit_error_clear(&err);
    CHECK_INT(kit_fs_size(missing, &err), -1);
    CHECK(failed_with(&err, KIT_ERR_NOT_FOUND, "absent.txt"));

    kit_error_clear(&err);
    CHECK_INT(kit_fs_mtime(missing, &err), -1);
    CHECK(failed_with(&err, KIT_ERR_NOT_FOUND, "absent.txt"));

    kit_error_clear(&err);
    CHECK(!kit_fs_remove(missing, &err));
    CHECK(failed_with(&err, KIT_ERR_NOT_FOUND, "absent.txt"));

    kit_error_clear(&err);
    CHECK(!kit_fs_copy(missing, join(SANDBOX, "copy.txt"), &err));
    CHECK(failed_with(&err, KIT_ERR_NOT_FOUND, "absent.txt"));

    kit_error_clear(&err);
    CHECK(!kit_fs_rename(missing, join(SANDBOX, "renamed.txt"), &err));
    CHECK(failed_with(&err, KIT_ERR_NOT_FOUND, "absent.txt"));

    kit_error_clear(&err);
    CHECK(!kit_fs_write(join(SANDBOX, "no/such/dir/file.txt"), "x", 1, &err));
    CHECK(failed_with(&err, KIT_ERR_NOT_FOUND, "file.txt"));

    kit_error_clear(&err);
    KitFileList list = KIT_ZEROED;
    CHECK(!kit_fs_list(join(SANDBOX, "absent-dir"), &list, &err));
    CHECK(failed_with(&err, KIT_ERR_NOT_FOUND, "absent-dir"));
    CHECK_INT(list.count, 0);
}

TEST(fs_wrong_kind_is_reported_the_same_everywhere) {
    char dir[512], file[512];
    kit_path_join(dir, sizeof(dir), SANDBOX, "kind-dir");
    kit_path_join(file, sizeof(file), SANDBOX, "kind-file.txt");
    CHECK(kit_fs_mkdir(dir, NULL));
    write_text(file, "x");

    KitError err = KIT_ZEROED;

    /* Linux says EISDIR here, macOS EPERM, Windows ERROR_ACCESS_DENIED. */
    CHECK(!kit_fs_remove(dir, &err));
    CHECK(failed_with(&err, KIT_ERR_WRONG_KIND, "is a directory"));

    kit_error_clear(&err);
    CHECK(!kit_fs_rmdir(file, &err));
    CHECK(failed_with(&err, KIT_ERR_WRONG_KIND, "not a directory"));

    kit_error_clear(&err);
    KitFileList list = KIT_ZEROED;
    CHECK(!kit_fs_list(file, &list, &err));
    CHECK(failed_with(&err, KIT_ERR_WRONG_KIND, "kind-file.txt"));

    kit_error_clear(&err);
    CHECK(!kit_fs_mkdir(join(SANDBOX, "kind-file.txt/child"), &err));
    CHECK(failed_with(&err, KIT_ERR_WRONG_KIND, "a file is in the way"));

    /* A path that runs through a file: POSIX says "not a directory", Windows
     * says "not found". Both must come out as the same code. */
    kit_error_clear(&err);
    CHECK(!kit_fs_write(join(SANDBOX, "kind-file.txt/nested.txt"), "x", 1, &err));
    CHECK(failed_with(&err, KIT_ERR_WRONG_KIND, "kind-file.txt"));

    kit_error_clear(&err);
    CHECK(kit_fs_read(join(SANDBOX, "kind-file.txt/nested.txt"), &err) == NULL);
    CHECK(failed_with(&err, KIT_ERR_WRONG_KIND, "kind-file.txt"));

    kit_error_clear(&err);
    CHECK_INT(kit_fs_size(join(SANDBOX, "kind-file.txt/nested.txt"), &err), -1);
    CHECK(failed_with(&err, KIT_ERR_WRONG_KIND, "kind-file.txt"));

    /* And the message must stay valid UTF-8 whatever the system's own text. */
    const unsigned char *m = (const unsigned char *)err.message;
    bool utf8 = true;
    while (*m && utf8) {
        int extra = (*m < 0x80) ? 0 : (*m >> 5) == 0x6 ? 1 : (*m >> 4) == 0xE ? 2
                  : (*m >> 3) == 0x1E ? 3 : -1;
        if (extra < 0) { utf8 = false; break; }
        m++;
        while (extra-- > 0) { if ((*m & 0xC0) != 0x80) { utf8 = false; break; } m++; }
    }
    CHECK(utf8);

    kit_fs_remove(file, NULL);
    kit_fs_rmdir(dir, NULL);
}

TEST(fs_other_failure_codes) {
    KitError err = KIT_ZEROED;

    CHECK(!kit_fs_mkdir("", &err));
    CHECK(failed_with(&err, KIT_ERR_INVALID, "empty path"));

    char full[512];
    kit_path_join(full, sizeof(full), SANDBOX, "not-empty");
    CHECK(kit_fs_mkdir(full, NULL));
    write_text(join(full, "child"), "x");
    kit_error_clear(&err);
    CHECK(!kit_fs_rmdir(full, &err));
    CHECK(failed_with(&err, KIT_ERR_NOT_EMPTY, "not-empty"));

    /* A missing input makes the answer unknowable, which is a failure rather
     * than "up to date", and the message says which input. */
    char out[512];
    kit_path_join(out, sizeof(out), SANDBOX, "built.o");
    write_text(out, "x");
    const char *inputs[] = { "definitely/not/here.c" };
    kit_error_clear(&err);
    CHECK_INT(kit_fs_stale(out, inputs, 1, &err), -1);
    CHECK(failed_with(&err, KIT_ERR_NOT_FOUND, "definitely/not/here.c"));

#ifdef __linux__
    if (kit_fs_is_file("/dev/full") || kit_fs_kind("/dev/full") == KIT_FILE_KIND_OTHER) {
        char payload[8192];
        memset(payload, 'x', sizeof(payload));
        kit_error_clear(&err);
        CHECK(!kit_fs_write("/dev/full", payload, sizeof(payload), &err));
        CHECK(failed_with(&err, KIT_ERR_NO_SPACE, "/dev/full"));
    }
#endif

#ifndef _WIN32
    /* Root reads through any permission bits, so the check would be moot. */
    if (geteuid() != 0) {
        char locked[512];
        kit_path_join(locked, sizeof(locked), SANDBOX, "locked");
        CHECK(kit_fs_mkdir(locked, NULL));
        CHECK_INT(chmod(locked, 0), 0);
        KitFileList list = KIT_ZEROED;
        kit_error_clear(&err);
        CHECK(!kit_fs_list(locked, &list, &err));
        CHECK(failed_with(&err, KIT_ERR_PERMISSION, "locked"));
        CHECK_INT(chmod(locked, 0700), 0);
    }
#endif
}

/* Given a KitError the failure is reported and nothing is logged; given NULL
 * it is logged. Checked here on a real call, not only on the error module. */
TEST(fs_reports_or_logs_but_never_both) {
    char missing[512];
    kit_path_join(missing, sizeof(missing), SANDBOX, "silent.txt");
    const char *log_path = "utest-tmp-fs.log";

    for (int with_error = 0; with_error <= 1; with_error++) {
        FILE *fp = fopen(log_path, "wb");
        if (!CHECK(fp != NULL)) return;
        kit_log_set_output(fp);
        kit_log_set_level(KIT_LOG_DEBUG);
        kit_log_set_color(KIT_LOG_COLOR_NEVER);
        kit_log_set_fields(KIT_LOG_FIELD_LEVEL);

        KitError err = KIT_ZEROED;
        CHECK(kit_fs_read(missing, with_error ? &err : NULL) == NULL);
        fclose(fp);
        kit_log_set_output(NULL);
        kit_log_set_level(KIT_LOG_CRITICAL);
        kit_log_set_fields(KIT_LOG_FIELDS_DEFAULT);

        char *log = kit_fs_read(log_path, NULL);
        if (!CHECK(log != NULL)) return;
        if (with_error) {
            CHECK_STR(log, "");
            CHECK_INT(err.code, KIT_ERR_NOT_FOUND);
        } else {
            CHECK(kit_str_starts_with_cstr(KIT_STR(log), "[ERROR] cannot open '"));
        }
        free(log);
    }
    kit_fs_remove(log_path, NULL);
}

/* --- teardown ------------------------------------------------------------- */

static void remove_tree(const char *path) {
    KitFileList list = {0};
    if (!kit_fs_list(path, &list, NULL)) return;
    for (size_t i = 0; i < list.count; ++i) {
        char child[512];
        kit_path_join(child, sizeof(child), path, list.items[i]);
        if (kit_fs_kind(child) == KIT_FILE_KIND_DIRECTORY) remove_tree(child);
        else                                         kit_fs_remove(child, NULL);
    }
    kit_file_list_free(&list);
    kit_fs_rmdir(path, NULL);
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);

    remove_tree(SANDBOX);
    if (!kit_fs_mkdir(SANDBOX, NULL)) {
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
    RUN(fs_missing_paths_are_not_found);
    RUN(fs_wrong_kind_is_reported_the_same_everywhere);
    RUN(fs_other_failure_codes);
    RUN(fs_reports_or_logs_but_never_both);
    RUN(fs_stale_tracks_staleness);
    int rc = utest_report();

    remove_tree(SANDBOX);
    return rc;
}
