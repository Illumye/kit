/*
 * A build tool in one file, using nothing but kit.h.
 *
 * It compiles examples/demo into an executable, skipping any step whose
 * output is already newer than its inputs, and rebuilding it when a header
 * changes. This is the integration test the unit suites cannot be: it puts
 * the option parser, the filesystem layer, the temporary allocator, the
 * command runner and the logger to work together.
 *
 *   cc -o build examples/build.c && ./build
 *   ./build -v            show every command
 *   ./build --clean       remove the build directory
 *   ./build -j            (not implemented, see below)
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"

#define SRC_DIR   "examples/demo"
#define BUILD_DIR "build/demo"
#define TARGET    BUILD_DIR "/demo"

/* Recompiling one object also depends on the headers it includes. Tracking
 * that properly means parsing the compiler's dependency output; treating any
 * header in the directory as an input to every object is cruder, and right. */
static bool collect_sources(KitFileList *sources, KitFileList *headers) {
    KitFileList entries = {0};
    if (!kit_fs_list(SRC_DIR, &entries)) return false;

    bool ok = true;
    for (size_t i = 0; i < entries.count; ++i) {
        const char *name = entries.items[i];
        char       *path = kit_scratch_printf("%s/%s", SRC_DIR, name);

        if (kit_str_ends_with_cstr(KIT_STR(name), ".c"))      kit_array_push(sources, path);
        else if (kit_str_ends_with_cstr(KIT_STR(name), ".h")) kit_array_push(headers, path);
    }

    if (sources->count == 0) {
        KIT_LOG(KIT_LOG_ERROR, "no source file in %s", SRC_DIR);
        ok = false;
    }
    kit_file_list_free(&entries);
    return ok;
}

/* The object path for a source: examples/demo/greet.c -> build/demo/greet.o */
static char *object_for(const char *source) {
    KitStr stem = KIT_STR(kit_path_basename(source));
    stem.count -= 2;                       /* drop the ".c" */
    return kit_scratch_printf("%s/%.*s.o", BUILD_DIR, KIT_STR_ARG(stem));
}

static bool compile(char *source, const char *object,
                    const KitFileList *headers, size_t *compiled) {
    /* The object depends on its source and on every header. */
    KitFileList inputs = {0};
    kit_array_push(&inputs, source);
    kit_array_push_many(&inputs, headers->items, headers->count);

    int stale = kit_fs_stale_list(object, &inputs);
    kit_array_free(&inputs);

    if (stale < 0) return false;
    if (stale == 0) {
        KIT_LOG(KIT_LOG_DEBUG, "up to date: %s", object);
        return true;
    }

    KitCommand cmd = {0};
    kit_command_push_all(&cmd, "cc", "-std=c11", "-Wall", "-Wextra", "-I", SRC_DIR,
               "-c", source, "-o", object, NULL);
    bool ok = kit_command_run(&cmd);
    kit_command_free(&cmd);

    if (ok) (*compiled)++;
    return ok;
}

static bool link_target(const KitFileList *objects) {
    int stale = kit_fs_stale_list(TARGET, objects);
    if (stale < 0) return false;
    if (stale == 0) {
        KIT_LOG(KIT_LOG_INFO, "%s is up to date", TARGET);
        return true;
    }

    KitCommand cmd = {0};
    kit_command_push_all(&cmd, "cc", "-o", TARGET, NULL);
    for (size_t i = 0; i < objects->count; ++i) kit_command_push(&cmd, objects->items[i]);
    bool ok = kit_command_run(&cmd);
    kit_command_free(&cmd);
    return ok;
}

static bool clean(void) {
    KitFileList entries = {0};
    if (!kit_fs_is_dir(BUILD_DIR)) {
        KIT_LOG(KIT_LOG_INFO, "nothing to clean");
        return true;
    }
    if (!kit_fs_list(BUILD_DIR, &entries)) return false;

    for (size_t i = 0; i < entries.count; ++i)
        kit_fs_remove(kit_scratch_printf("%s/%s", BUILD_DIR, entries.items[i]));

    kit_file_list_free(&entries);
    KIT_LOG(KIT_LOG_INFO, "cleaned %s", BUILD_DIR);
    return true;
}

int main(int argc, char **argv) {
    const char *prog = kit_cli_shift(&argc, &argv);

    bool verbose = false, do_clean = false, run = false, help = false;
    KitCliOpt opts[] = {
        KIT_CLI_FLAG('v', "verbose", "Echo every command",        &verbose),
        KIT_CLI_FLAG('c', "clean",   "Remove the build directory", &do_clean),
        KIT_CLI_FLAG('r', "run",     "Run the target once built",  &run),
        KIT_CLI_FLAG('h', "help",    "Show this help",             &help),
    };

    if (!kit_cli_parse_arr(opts, &argc, &argv)) {
        kit_cli_usage_arr(stderr, prog, opts);
        return 1;
    }
    if (help) {
        kit_cli_usage_arr(stdout, prog, opts);
        return 0;
    }

    /* Quiet by default: only what the user needs, and the commands on -v.
     * The location field is noise for a build log. */
    kit_log_set_level(verbose ? KIT_LOG_DEBUG : KIT_LOG_INFO);
    kit_log_set_fields(KIT_LOG_FIELD_TIME | KIT_LOG_FIELD_LEVEL);

    if (do_clean) return clean() ? 0 : 1;

    if (!kit_fs_mkdir(BUILD_DIR)) return 1;

    KitFileList sources = {0}, headers = {0}, objects = {0};
    int      status  = 1;

    if (!collect_sources(&sources, &headers)) goto done;
    KIT_LOG(KIT_LOG_DEBUG, "%zu source(s), %zu header(s)", sources.count, headers.count);

    size_t compiled = 0;
    for (size_t i = 0; i < sources.count; ++i) {
        char *object = object_for(sources.items[i]);
        if (!compile(sources.items[i], object, &headers, &compiled)) goto done;
        kit_array_push(&objects, object);
    }

    if (!link_target(&objects)) goto done;
    KIT_LOG(KIT_LOG_INFO, "%s ready (%zu file(s) compiled)", TARGET, compiled);

    if (run && !kit_command_run_args(TARGET, NULL)) goto done;
    status = 0;

done:
    /* The paths live in the temporary arena, so the lists own no memory of
     * their own and only the arrays are released. */
    kit_array_free(&sources);
    kit_array_free(&headers);
    kit_array_free(&objects);
    kit_scratch_reset();
    return status;
}
