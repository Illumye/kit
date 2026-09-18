/*
 * Walking a directory, which is the filesystem layer doing what a build tool
 * or an installer needs: list, classify, measure, and report where it failed.
 *
 * Shows: kit_fs_list and the rest of the filesystem, the path helpers, the
 * scratch allocator for the paths built along the way, and the timer.
 *
 *   ./examples/tree --depth 2 examples
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"

typedef struct {
    size_t files, directories, others;
    int64_t bytes;
    int64_t newest;                 /* seconds since the epoch */
} Totals;

static const char *human(int64_t bytes) {
    static const char *const unit[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double  value = (double)bytes;
    size_t  u     = 0;
    while (value >= 1024.0 && u + 1 < KIT_COUNTOF(unit)) { value /= 1024.0; u++; }
    /* Scratch memory: freed in one go by the caller, never one string at a
     * time, which is exactly what a printing loop wants. */
    return u == 0 ? kit_scratch_printf("%lld %s", (long long)bytes, unit[u])
                  : kit_scratch_printf("%.1f %s", value, unit[u]);
}

static bool walk(const char *path, int depth, int max_depth, Totals *totals, KitError *err) {
    /* The recursion only goes down while depth + 1 < max_depth, so a call
     * that arrives past the limit means the guard below was changed and the
     * walk no longer stops where it was told to. */
    KIT_ASSERT_CMP(depth, <=, max_depth);

    KitFileList entries = KIT_ZEROED;
    if (!kit_fs_list(path, &entries, err)) return false;

    bool result = true;
    for (size_t i = 0; i < entries.count; ++i) {
        /* Everything allocated in this iteration goes away at its end. */
        KitArenaMark mark  = kit_scratch_mark();
        const char  *name  = entries.items[i];
        /* kit_path_join adds the separator only when it is missing, so a root
         * ending in one does not end up with two. */
        char        *joined = (char *)kit_scratch_alloc(1024);
        const char  *child  = kit_path_join(joined, 1024, path, name);

        KitFileKind kind = kit_fs_kind(child);
        switch (kind) {
        case KIT_FILE_KIND_DIRECTORY: {
            totals->directories++;
            printf("%*s%s/\n", depth * 2, "", name);
            if (depth + 1 < max_depth && !walk(child, depth + 1, max_depth, totals, err)) {
                kit_error_context(err, "below %s", path);
                result = false;
            }
            break;
        }
        case KIT_FILE_KIND_REGULAR: {
            int64_t size = kit_fs_size(child, err);
            int64_t when = kit_fs_mtime(child, err);
            if (size < 0 || when < 0) { result = false; break; }

            totals->files++;
            /* The one sum in this program whose terms come from outside it.
             * Refusing to wrap is what keeps the reported total the last one
             * that was true, rather than a small number that looks right. */
            if (!kit_num_add(totals->bytes, size, &totals->bytes)) {
                kit_error_set(err, KIT_ERR_RANGE,
                              "%s takes the total past what a signed 64-bit count holds",
                              child);
                result = false;
                break;
            }
            if (when > totals->newest) totals->newest = when;

            const char *ext = kit_path_ext(name);
            printf("%*s%-28s %10s%s%s\n", depth * 2, "", name, human(size),
                   *ext ? "  " : "", ext);
            break;
        }
        case KIT_FILE_KIND_NONE:
            /* It was listed a moment ago, so it has just been removed. */
            KIT_WARN("%s vanished while walking", child);
            break;
        case KIT_FILE_KIND_OTHER:
        default:
            /* A socket, a fifo, a device: counted, not measured. */
            totals->others++;
            printf("%*s%-28s %10s\n", depth * 2, "", name, "(special)");
            break;
        }

        kit_scratch_rewind(mark);
        if (!result) break;
    }

    kit_file_list_free(&entries);
    return result;
}

int main(int argc, char **argv) {
    const char *prog = kit_cli_shift(&argc, &argv);

    int  depth = 2;
    bool help  = false;
    KitCliOpt opts[] = {
        KIT_CLI_INT ('d', "depth", "N", "How deep to go", &depth),
        KIT_CLI_FLAG('h', "help",  "Show this help",      &help),
    };
    if (!kit_cli_parse_arr(opts, &argc, &argv, NULL)) return 1;
    if (help) { kit_cli_usage_arr(stdout, prog, opts); return 0; }

    kit_log_set_fields(KIT_LOG_FIELD_LEVEL);

    const char *root = argc > 0 ? argv[0] : ".";
    if (!kit_fs_is_dir(root)) {
        KIT_ERROR("%s is not a directory", root);
        return 1;
    }
    char parent[512];
    printf("%s%s, under %s\n", root,
           kit_path_is_absolute(root) ? "  (absolute)" : "",
           kit_path_dirname(root, parent, sizeof(parent)));

    Totals   totals = KIT_ZEROED;
    KitError err    = KIT_ZEROED;
    KitTimer clock  = kit_timer_start();

    if (!walk(root, 1, depth < 1 ? 1 : depth, &totals, &err)) {
        kit_error_context(&err, "walking %s", root);
        KIT_ERROR("%s", err.message);
        kit_scratch_free();
        return 1;
    }
    double elapsed = kit_timer_ms(clock);

    printf("\n%zu files, %zu directories", totals.files, totals.directories);
    if (totals.others) printf(", %zu other entries", totals.others);
    printf(", %s in all\n", human(totals.bytes));

    if (totals.newest > 0) {
        time_t when = (time_t)totals.newest;
        char   stamp[32];
        struct tm parts;
#ifdef _WIN32
        localtime_s(&parts, &when);
#else
        localtime_r(&when, &parts);
#endif
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", &parts);
        printf("most recent change %s\n", stamp);
    }
    printf("walked in %.1f ms (%.3f s)\n", elapsed, kit_timer_s(clock));

    kit_scratch_free();
    return 0;
}
