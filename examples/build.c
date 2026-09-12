/*
 * A build tool in one file, using nothing but utils.h.
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

#define UTILS_IMPLEMENTATION
#include "../utils.h"

#define SRC_DIR   "examples/demo"
#define BUILD_DIR "build/demo"
#define TARGET    BUILD_DIR "/demo"

/* Recompiling one object also depends on the headers it includes. Tracking
 * that properly means parsing the compiler's dependency output; treating any
 * header in the directory as an input to every object is cruder, and right. */
static bool collect_sources(FileList *sources, FileList *headers) {
    FileList entries = {0};
    if (!read_dir(SRC_DIR, &entries)) return false;

    bool ok = true;
    for (size_t i = 0; i < entries.count; ++i) {
        const char *name = entries.items[i];
        char       *path = temp_sprintf("%s/%s", SRC_DIR, name);

        if (sv_ends_with_cstr(SV(name), ".c"))      da_append(sources, path);
        else if (sv_ends_with_cstr(SV(name), ".h")) da_append(headers, path);
    }

    if (sources->count == 0) {
        LOG(LOG_ERROR, "no source file in %s", SRC_DIR);
        ok = false;
    }
    file_list_free(&entries);
    return ok;
}

/* The object path for a source: examples/demo/greet.c -> build/demo/greet.o */
static char *object_for(const char *source) {
    String_View stem = SV(path_basename(source));
    stem.count -= 2;                       /* drop the ".c" */
    return temp_sprintf("%s/%.*s.o", BUILD_DIR, SV_Arg(stem));
}

static bool compile(char *source, const char *object,
                    const FileList *headers, size_t *compiled) {
    /* The object depends on its source and on every header. */
    FileList inputs = {0};
    da_append(&inputs, source);
    da_append_many(&inputs, headers->items, headers->count);

    int stale = needs_rebuild_list(object, &inputs);
    da_free(&inputs);

    if (stale < 0) return false;
    if (stale == 0) {
        LOG(LOG_DEBUG, "up to date: %s", object);
        return true;
    }

    Cmd cmd = {0};
    cmd_extend(&cmd, "cc", "-std=c11", "-Wall", "-Wextra", "-I", SRC_DIR,
               "-c", source, "-o", object, NULL);
    bool ok = cmd_run(&cmd);
    cmd_free(&cmd);

    if (ok) (*compiled)++;
    return ok;
}

static bool link_target(const FileList *objects) {
    int stale = needs_rebuild_list(TARGET, objects);
    if (stale < 0) return false;
    if (stale == 0) {
        LOG(LOG_INFO, "%s is up to date", TARGET);
        return true;
    }

    Cmd cmd = {0};
    cmd_extend(&cmd, "cc", "-o", TARGET, NULL);
    for (size_t i = 0; i < objects->count; ++i) cmd_append(&cmd, objects->items[i]);
    bool ok = cmd_run(&cmd);
    cmd_free(&cmd);
    return ok;
}

static bool clean(void) {
    FileList entries = {0};
    if (!dir_exists(BUILD_DIR)) {
        LOG(LOG_INFO, "nothing to clean");
        return true;
    }
    if (!read_dir(BUILD_DIR, &entries)) return false;

    for (size_t i = 0; i < entries.count; ++i)
        remove_file(temp_sprintf("%s/%s", BUILD_DIR, entries.items[i]));

    file_list_free(&entries);
    LOG(LOG_INFO, "cleaned %s", BUILD_DIR);
    return true;
}

int main(int argc, char **argv) {
    const char *prog = args_shift(&argc, &argv);

    bool verbose = false, do_clean = false, run = false, help = false;
    Opt opts[] = {
        OPT_FLAG('v', "verbose", "Echo every command",        &verbose),
        OPT_FLAG('c', "clean",   "Remove the build directory", &do_clean),
        OPT_FLAG('r', "run",     "Run the target once built",  &run),
        OPT_FLAG('h', "help",    "Show this help",             &help),
    };

    if (!opts_parse_arr(opts, &argc, &argv)) {
        opts_usage_arr(stderr, prog, opts);
        return 1;
    }
    if (help) {
        opts_usage_arr(stdout, prog, opts);
        return 0;
    }

    /* Quiet by default: only what the user needs, and the commands on -v.
     * The location field is noise for a build log. */
    log_set_level(verbose ? LOG_DEBUG : LOG_INFO);
    log_set_fields(LOG_FIELD_TIME | LOG_FIELD_LEVEL);

    if (do_clean) return clean() ? 0 : 1;

    if (!mkdir_p(BUILD_DIR)) return 1;

    FileList sources = {0}, headers = {0}, objects = {0};
    int      status  = 1;

    if (!collect_sources(&sources, &headers)) goto done;
    LOG(LOG_DEBUG, "%zu source(s), %zu header(s)", sources.count, headers.count);

    size_t compiled = 0;
    for (size_t i = 0; i < sources.count; ++i) {
        char *object = object_for(sources.items[i]);
        if (!compile(sources.items[i], object, &headers, &compiled)) goto done;
        da_append(&objects, object);
    }

    if (!link_target(&objects)) goto done;
    LOG(LOG_INFO, "%s ready (%zu file(s) compiled)", TARGET, compiled);

    if (run && !cmd_run_args(TARGET, NULL)) goto done;
    status = 0;

done:
    /* The paths live in the temporary arena, so the lists own no memory of
     * their own and only the arrays are released. */
    da_free(&sources);
    da_free(&headers);
    da_free(&objects);
    temp_reset();
    return status;
}
