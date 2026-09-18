/*
 * Walking a directory, which is the filesystem layer doing what a build tool
 * or an installer needs: list, classify, measure, and report where it failed.
 * It also answers the question a listing alone never does, which files are
 * the big ones, in one pass and without sorting the tree.
 *
 * Shows: kit_fs_list and the rest of the filesystem, the path helpers, the
 * scratch allocator for the paths built along the way, a heap for the N
 * largest, glob patterns for choosing among the names, and the timer.
 *
 *   ./examples/tree --depth 2 examples
 *   ./examples/tree --match '*.c' --largest 5 examples
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"

/* A file worth remembering. The path has to be copied: the listing it came
 * from is freed when the walk leaves the directory, and the scratch memory
 * the child path was built in is rewound at the end of every entry. */
typedef struct {
    const char *path;
    int64_t     size;
} Found;

typedef struct { Found *items; size_t count, capacity; } Biggest;

typedef struct {
    size_t files, directories, others;
    int64_t bytes;
    int64_t newest;                 /* seconds since the epoch */
    Biggest   biggest;              /* a heap of the largest seen so far */
    size_t    want;                 /* how many of them to keep */
    KitArena *names;                /* where the kept paths live */
    const char *match;              /* the pattern a file has to answer to */
} Totals;

/* Smallest first, so that the one to drop when something bigger arrives is
 * the one the heap offers. */
static int by_size(const void *a, const void *b) {
    const Found *x = (const Found *)a, *y = (const Found *)b;
    return x->size < y->size ? -1 : x->size > y->size ? 1 : 0;
}

static int by_size_desc(const void *a, const void *b) {
    return -by_size(a, b);
}

/* The N largest of a tree, without sorting the tree. The heap holds N
 * entries: anything smaller than its smallest cannot belong in the answer and
 * is forgotten as soon as it is seen. */
static void remember_if_big(Totals *totals, const char *path, int64_t size) {
    if (totals->want == 0) return;

    if (totals->biggest.count == totals->want) {
        if (size <= totals->biggest.items[0].size) return;
        kit_heap_pop(&totals->biggest, by_size);
    }

    Found found = { kit_arena_strdup(totals->names, path), size };
    kit_heap_push(&totals->biggest, found, by_size);
}

/* Scratch memory: freed in one go by the caller, never one string at a time,
 * which is exactly what a printing loop wants. The formatting itself is
 * kit_fmt_size, since every program that reports sizes writes this function
 * once and this one is no exception. */
static const char *human(int64_t bytes) {
    char text[KIT_FMT_CAPACITY];
    return kit_scratch_strdup(kit_fmt_size(text, sizeof text, (uint64_t)bytes));
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
            /* The filter applies to what is reported, never to where the walk
             * goes: a directory is always entered, or "*.o" would only ever
             * find the object files sitting at the top. */
            if (!kit_glob_match(totals->match, name)) break;

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
            remember_if_big(totals, child, size);

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

    int  depth = 2, largest = 3;
    bool help  = false;
    const char *match = "*";
    KitCliOpt opts[] = {
        KIT_CLI_INT ('d', "depth",   "N",    "How deep to go",             &depth),
        KIT_CLI_INT ('l', "largest", "N",    "How many big files to name", &largest),
        KIT_CLI_STR ('m', "match",   "GLOB", "Only files matching this",   &match),
        KIT_CLI_FLAG('h', "help",    "Show this help",                     &help),
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

    /* The names of the files worth keeping outlive the walk that found them,
     * so they go in an arena of their own rather than in the scratch memory
     * the walk rewinds after every entry. */
    KitArena names  = KIT_ZEROED;
    Totals   totals = KIT_ZEROED;
    totals.want     = (size_t)(largest < 0 ? 0 : largest);
    totals.names    = &names;
    totals.match    = match;

    KitError err   = KIT_ZEROED;
    KitTimer clock = kit_timer_start();

    if (!walk(root, 1, depth < 1 ? 1 : depth, &totals, &err)) {
        kit_error_context(&err, "walking %s", root);
        KIT_ERROR("%s", err.message);
        kit_array_free(&totals.biggest);
        kit_arena_free(&names);
        kit_scratch_free();
        return 1;
    }
    /* Read before printing anything, so the figure is the walk and not the
     * terminal it is reported to. */
    double elapsed = kit_timer_s(clock);

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
    /* The heap answered "which are the biggest", which is not the same
     * question as "in what order": only its first element was ever ordered,
     * so the few that came out of it are sorted now, at the end, on the
     * handful that survived rather than on everything walked. */
    if (totals.biggest.count > 0) {
        kit_array_sort(&totals.biggest, by_size_desc);
        printf("largest %zu:\n", totals.biggest.count);
        kit_array_each(Found, found, &totals.biggest)
            printf("  %-32s %10s\n", found->path, human(found->size));
    }

    char took[KIT_FMT_CAPACITY];
    printf("walked in %s\n", kit_fmt_duration(took, sizeof took, elapsed));

    kit_array_free(&totals.biggest);
    kit_arena_free(&names);
    kit_scratch_free();
    return 0;
}
