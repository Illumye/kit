/*
 * Rotating a log file: when the current one grows past a limit, it is moved
 * aside, the older ones shift down, and the oldest is dropped. Every step is a
 * filesystem call that can fail, which is what makes it a good example.
 *
 * Shows: the writing half of the filesystem, the logger pointed at a file and
 * reconfigured, and failures reported with the system's own reason attached.
 *
 *   ./examples/journal --dir build/journal --limit 2048 --lines 200
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"

typedef struct {
    const char *directory;
    const char *name;
    int64_t     limit;        /* bytes before the file is rotated */
    int         keep;         /* how many old files to keep */
} Journal;

static char *path_of(const Journal *j, int index) {
    /* kit_path_join puts the separator in, whatever it is on this platform. */
    char *buffer = (char *)kit_scratch_alloc(512);
    const char *leaf = index == 0 ? j->name
                                  : kit_scratch_printf("%s.%d", j->name, index);
    return kit_path_join(buffer, 512, j->directory, leaf);
}

/* What a caller can do about each category, which is what the code is for. */
static const char *advice(KitErrorCode code) {
    switch (code) {
        case KIT_OK:              return "nothing went wrong";
        case KIT_ERR_NOT_FOUND:   return "create it, or skip the step";
        case KIT_ERR_EXISTS:      return "pick another name, or replace it";
        case KIT_ERR_NOT_EMPTY:   return "empty the directory first";
        case KIT_ERR_WRONG_KIND:  return "a file is where a directory should be, or the reverse";
        case KIT_ERR_PERMISSION:  return "run somewhere writable";
        case KIT_ERR_INVALID:     return "fix what was passed in";
        case KIT_ERR_RANGE:       return "the name or the value is too large";
        case KIT_ERR_NO_SPACE:    return "free some space and retry";
        case KIT_ERR_NO_MEMORY:   return "ask for less at a time";
        case KIT_ERR_BUSY:        return "something holds it: retry later";
        case KIT_ERR_INTERRUPTED: return "a signal cut it short: retry";
        case KIT_ERR_IO:          return "the device failed, which is rarely ours to fix";
        case KIT_ERR_PROCESS:     return "the child failed: read what it printed";
        case KIT_ERR_UNSUPPORTED: return "not possible here, across devices for instance";
        case KIT_ERR_OTHER:       return "read the message";
    }
    return "unknown category";
}

/* journal.log.2 -> dropped, .1 -> .2, journal.log -> .1 */
static bool rotate(const Journal *j, KitError *err) {
    bool result = true;
    KitArenaMark mark = kit_scratch_mark();

    const char *oldest = path_of(j, j->keep);
    if (kit_fs_is_file(oldest) && !kit_fs_remove(oldest, err)) KIT_BAIL(false);

    for (int i = j->keep - 1; i >= 0; i--) {
        const char *from = path_of(j, i);
        if (!kit_fs_is_file(from)) continue;
        if (!kit_fs_rename(from, path_of(j, i + 1), err)) KIT_BAIL(false);
    }

    KIT_INFO("rotated %s", j->name);

cleanup:
    kit_scratch_rewind(mark);
    return result;
}

static bool append_line(const Journal *j, const char *line, KitError *err) {
    bool result = true;
    KitArenaMark mark = kit_scratch_mark();
    const char  *path = path_of(j, 0);

    int64_t size = kit_fs_is_file(path) ? kit_fs_size(path, err) : 0;
    if (size < 0) KIT_BAIL(false);
    if (size >= j->limit) {
        if (!rotate(j, err)) KIT_BAIL(false);
        /* rotate() reported success, so the current file has been renamed out
         * of the way. If it is still there the rename did nothing, every
         * later line appends to a file that never rotates, and the only
         * symptom is a disk filling up days later. */
        KIT_ASSERT_MSG(!kit_fs_is_file(path), "%s survived rotation at %lld bytes",
                       path, (long long)size);
    }

    /* Read, append, write: a real journal would keep the file open, but this
     * is the shape that shows the calls. */
    KitBuf buffer = KIT_ZEROED;
    if (size > 0 && kit_fs_is_file(path)) {
        size_t n = 0;
        char  *existing = kit_fs_read_sized(path, &n, err);
        if (!existing) { kit_buf_free(&buffer); KIT_BAIL(false); }
        kit_buf_append_n(&buffer, existing, n);
        free(existing);
    }
    kit_buf_append(&buffer, line);
    kit_buf_append_char(&buffer, '\n');

    if (!kit_fs_write(path, buffer.items, buffer.count, err)) {
        kit_buf_free(&buffer);
        KIT_BAIL(false);
    }
    kit_buf_free(&buffer);

cleanup:
    kit_scratch_rewind(mark);
    return result;
}

int main(int argc, char **argv) {
    const char *prog = kit_cli_shift(&argc, &argv);

    const char *directory = "build/journal";
    int  limit = 1024, lines = 120, keep = 3;
    bool help = false, quiet = false;
    KitCliOpt opts[] = {
        KIT_CLI_STR ('d', "dir",   "PATH", "Where to write",        &directory),
        KIT_CLI_INT ('l', "limit", "N",    "Bytes before rotating", &limit),
        KIT_CLI_INT ('n', "lines", "N",    "How many lines",        &lines),
        KIT_CLI_INT ('k', "keep",  "N",    "Old files to keep",     &keep),
        KIT_CLI_FLAG('q', "quiet", "Only complain",                 &quiet),
        KIT_CLI_FLAG('h', "help",  "Show this help",                &help),
    };
    if (!kit_cli_parse_arr(opts, &argc, &argv, NULL)) return 1;
    if (help) { kit_cli_usage_arr(stdout, prog, opts); return 0; }

    /* The logger is configured, not accepted as it comes: the level decides
     * the noise, and colour is left to the AUTO rule, which keeps escape
     * sequences out of a redirected file. */
    KitLogLevel level  = quiet ? KIT_LOG_WARN : KIT_LOG_DEBUG;
    KitLogColor colour = KIT_LOG_COLOR_AUTO;
    KitLogField first  = KIT_LOG_FIELD_TIME;
    kit_log_set_level(level);
    kit_log_set_color(colour);
    kit_log_set_fields((unsigned)first | KIT_LOG_FIELD_LEVEL);

    KitError err = KIT_ZEROED;
    if (!kit_fs_mkdir(directory, &err)) {
        KIT_ERROR("%s", err.message);
        return 1;
    }

    /* A limit of zero or less would rotate on every single line. */
    Journal journal = { directory, "app.log", kit_clampi(limit, 1, INT_MAX),
                        kit_clampi(keep, 1, 9) };

    /* How much this run is about to write, which is a number typed on the
     * command line multiplied by a size. Left unchecked, --lines 100000000
     * does not report an absurd plan, it reports a plausible one: the product
     * wraps and comes back small. */
    const int line_bytes = 46;
    int       planned    = 0;
    if (!kit_num_mul(lines, line_bytes, &planned)) {
        KIT_ERROR("%d lines of %d bytes is more than this program can count",
                  lines, line_bytes);
        return 1;
    }
    KIT_INFO("about %d bytes to write, rotating every %d", planned, limit);

    for (int i = 0; i < lines; i++) {
        const char *line = kit_scratch_printf("%04d  the quick brown fox jumps over the lazy dog", i);
        if (!append_line(&journal, line, &err)) {
            kit_error_context(&err, "writing the journal in %s", directory);
            KIT_ERROR("%s (%s)", err.message, kit_error_code_name(err.code));
            kit_scratch_free();
            return 1;
        }
        kit_scratch_reset();
    }

    /* What is on disk now. */
    KitFileList files = KIT_ZEROED;
    if (!kit_fs_list(directory, &files, &err)) {
        KIT_ERROR("%s", err.message);
        kit_scratch_free();
        return 1;
    }

    printf("%d lines written, %zu file(s) kept:\n", lines, files.count);
    for (size_t i = 0; i < files.count; ++i) {
        const char *path = kit_scratch_printf("%s%c%s", directory, KIT_PATH_SEP, files.items[i]);
        printf("  %-14s %8lld bytes\n", files.items[i], (long long)kit_fs_size(path, NULL));
    }

    /* Room before the next rotation, counted in bytes, which is a size and so
     * unsigned. The file is over the limit as often as it is under it, and an
     * unsigned subtraction that wraps there does not report a full journal:
     * it reports one with sixteen exabytes to spare. */
    int64_t written = kit_fs_size(path_of(&journal, 0), NULL);
    if (written >= 0) {
        uint64_t room = 0;
        if (kit_num_sub((uint64_t)journal.limit, (uint64_t)written, &room))
            printf("  %llu bytes before the next rotation\n", (unsigned long long)room);
        else
            printf("  past the limit already: the next line rotates\n");
    }

    /* A copy of the current file, kept as a snapshot, and proof that the
     * error carries the system's own reason when a path is wrong. */
    const char *current  = kit_scratch_printf("%s%c%s", directory, KIT_PATH_SEP, journal.name);
    const char *snapshot = kit_scratch_printf("%s%c%s", directory, KIT_PATH_SEP, "snapshot.log");
    if (kit_fs_copy(current, snapshot, &err)) {
        KIT_DEBUG("snapshot taken");
        kit_fs_remove(snapshot, NULL);
    }

    kit_error_clear(&err);
    if (!kit_fs_copy(kit_scratch_printf("%s%cabsent.log", directory, KIT_PATH_SEP), snapshot, &err))
        printf("as expected: %s\n  errno %d, category %s, %s\n",
               err.message, err.native,
               kit_error_code_name(kit_error_code_from_errno(err.native)), advice(err.code));

    /* An error of our own, from a call the library knows nothing about. */
    kit_error_clear(&err);
    FILE *locked = fopen(kit_scratch_printf("%s%cno%cdir%clock", directory,
                                            KIT_PATH_SEP, KIT_PATH_SEP, KIT_PATH_SEP), "w");
    if (!locked) {
        kit_error_errno(&err, errno, "cannot take the journal lock");
        KIT_DEBUG("%s", err.message);
    } else {
        fclose(locked);
    }

    /* Is the newest file older than its predecessor? kit_fs_stale answers for
     * one output against several inputs, or against a single one. */
    const char *newest = path_of(&journal, 0);
    const char *older  = path_of(&journal, 1);
    if (kit_fs_is_file(older)) {
        const char *inputs[] = { older };
        printf("the current file is %s than the one before it\n",
               kit_fs_stale(newest, inputs, 1, NULL) == 0 ? "newer" : "not newer");
        printf("and %s by the same measure taken one input at a time\n",
               kit_fs_stale1(newest, older, NULL) == 0 ? "newer" : "not newer");
    }

    /* The whole record of this run, in one line, whatever the fields were. */
    kit_log_set_fields(KIT_LOG_FIELDS_ALL);
    KIT_LOG(level, "%s finished", kit_scratch_strdup(journal.name));
    kit_log_set_fields(KIT_LOG_FIELDS_NONE);
    KIT_LOG(KIT_LOG_INFO, "the same line without a single field");
    kit_log_set_fields(KIT_LOG_FIELDS_DEFAULT);

    /* Diagnostics can go to a file rather than to the terminal, with colour
     * forced on or off rather than guessed. */
    const char *diagnostics = path_of(&journal, 0);
    FILE *sink = fopen(kit_scratch_printf("%s%cdiagnostics.log", directory, KIT_PATH_SEP), "w");
    if (sink) {
        kit_log_set_output(sink);
        kit_log_set_color(KIT_LOG_COLOR_NEVER);
        kit_log_set_fields(KIT_LOG_FIELD_DATE | KIT_LOG_FIELD_TIME |
                           KIT_LOG_FIELD_USER | KIT_LOG_FIELD_COUNT |
                           KIT_LOG_FIELD_LOCATION | KIT_LOG_FIELD_LEVEL);
        KIT_LOG(KIT_LOG_ERROR, "a line with every field, written to %s", diagnostics);
        KIT_LOG(KIT_LOG_CRITICAL, "the loudest level, which does not end the program");
        KIT_CRITICAL("the same thing, written the short way");
        kit_log_set_output(NULL);
        kit_log_set_color(KIT_LOG_COLOR_ALWAYS);
        kit_log_set_color(KIT_LOG_COLOR_AUTO);
        kit_log_set_fields(KIT_LOG_FIELD_LEVEL);
        fclose(sink);
    }

    /* Tidying up: the snapshot, then the directory itself if it is empty. */
    kit_fs_remove(kit_scratch_printf("%s%cdiagnostics.log", directory, KIT_PATH_SEP), NULL);
    char parent[512];
    KIT_DEBUG("the journal lives under %s",
              kit_path_dirname(kit_scratch_printf("%s%c%s", directory, KIT_PATH_SEP, journal.name),
                               parent, sizeof(parent)));
    kit_error_clear(&err);
    if (!kit_fs_rmdir(directory, &err))
        printf("the directory stays: %s (%s)\n", err.message, advice(err.code));

    kit_file_list_free(&files);
    kit_scratch_free();
    return 0;
}
