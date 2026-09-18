/*
 * What is this file? The question comes up before every other one: a download
 * that will not open, a configuration file with an invisible byte in it, a
 * "text" file that starts with a zip header.
 *
 * Shows: the option parser in all the forms it accepts, the filesystem
 * answering what something is rather than failing, sizes a person can read,
 * and the hex dump.
 *
 *   ./examples/peek examples/demo/app.conf
 *   ./examples/peek -n 32 -o 16 examples/demo/prose.txt
 *   ./examples/peek --all --quiet examples/demo/greet.h
 *
 * It reads the whole file to show part of it, because kit_fs_read does: this
 * is a tool for configuration files, manifests and sources, not for disk
 * images. A reader who needs those wants a seek, which is a different API and
 * not this one.
 */

#define KIT_NO_VEC_MATH
#define KIT_IMPLEMENTATION
#include "../kit.h"

static const char *kind_name(KitFileKind kind) {
    switch (kind) {
        case KIT_FILE_KIND_REGULAR:   return "regular file";
        case KIT_FILE_KIND_DIRECTORY: return "directory";
        case KIT_FILE_KIND_OTHER:     return "device, socket or fifo";
        case KIT_FILE_KIND_NONE:      break;
    }
    return "nothing";
}

/* Modification time, as a date rather than as a count of seconds. */
static const char *modified_at(int64_t seconds) {
    static char stamp[32];
    time_t      when = (time_t)seconds;
    struct tm   parts;
#ifdef _WIN32
    localtime_s(&parts, &when);
#else
    localtime_r(&when, &parts);
#endif
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", &parts);
    return stamp;
}

/* The first bytes say more about a file than its extension does. */
static const char *looks_like(const unsigned char *data, size_t size) {
    if (size >= 4 && memcmp(data, "\x7f" "ELF", 4) == 0)  return "an ELF binary";
    if (size >= 4 && memcmp(data, "PK\x03\x04", 4) == 0)  return "a zip archive";
    if (size >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) return "a PNG image";
    if (size >= 2 && data[0] == '#' && data[1] == '!')    return "a script";
    if (size >= 3 && memcmp(data, "\xef\xbb\xbf", 3) == 0) return "text with a byte order mark";

    for (size_t i = 0; i < size; i++)
        if (data[i] == 0) return "binary: it has a NUL byte in it";

    return "text";
}

int main(int argc, char **argv) {
    const char *prog = kit_cli_shift(&argc, &argv);

    int  bytes = 64, offset = 0;
    bool all = false, quiet = false, help = false;
    KitCliOpt opts[] = {
        KIT_CLI_INT ('n', "bytes",  "N", "How many bytes to show", &bytes),
        KIT_CLI_INT ('o', "offset", "N", "Where to start",         &offset),
        KIT_CLI_FLAG('a', "all",    "Show the whole file",         &all),
        KIT_CLI_FLAG('q', "quiet",  "The dump on its own",         &quiet),
        KIT_CLI_FLAG('h', "help",   "Show this help",              &help),
    };

    /* NULL, so a command line nobody can act on is logged and the usage
     * printed: whoever typed it is the one who can fix it. */
    if (!kit_cli_parse_arr(opts, &argc, &argv, NULL)) {
        kit_cli_usage_arr(stderr, prog, opts);
        return 1;
    }
    if (help || argc != 1) {
        kit_cli_usage_arr(help ? stdout : stderr, prog, opts);
        return help ? 0 : 1;
    }

    kit_log_set_fields(KIT_LOG_FIELD_LEVEL);

    const char *path = argv[0];
    KitFileKind kind = kit_fs_kind(path);
    if (kind == KIT_FILE_KIND_NONE) {
        KIT_ERROR("there is nothing at %s", path);
        return 1;
    }
    if (kind != KIT_FILE_KIND_REGULAR) {
        KIT_ERROR("%s is a %s, and only a regular file can be read",
                  path, kind_name(kind));
        return 1;
    }

    KitError err  = KIT_ZEROED;
    size_t   size = 0;
    char    *data = kit_fs_read_sized(path, &size, &err);
    if (!data) {
        kit_error_context(&err, "peeking at %s", path);
        KIT_ERROR("%s (%s)", err.message, kit_error_code_name(err.code));
        return 1;
    }

    const unsigned char *raw = (const unsigned char *)data;

    if (!quiet) {
        char     readable[KIT_FMT_CAPACITY];
        int64_t  when = kit_fs_mtime(path, NULL);

        printf("%s\n", path);
        printf("  %s, %s, %s\n", kind_name(kind),
               kit_fmt_size(readable, sizeof readable, (uint64_t)size),
               looks_like(raw, size));
        if (when > 0) printf("  last changed %s\n", modified_at(when));
    }

    /* A window into the file: where it starts and how far it runs, both
     * clamped to what is actually there rather than trusted. */
    size_t from = (size_t)(offset < 0 ? 0 : offset);
    if (from > size) from = size;

    size_t run = all ? size - from : (size_t)(bytes < 0 ? 0 : bytes);
    if (run > size - from) run = size - from;

    if (run == 0) {
        if (!quiet) printf("  nothing to show from that offset\n");
    } else {
        if (!quiet) printf("\n");
        kit_hex_dump(stdout, raw + from, run, from);
        if (!quiet && from + run < size) {
            char rest[KIT_FMT_CAPACITY];
            printf("  ... %s more, --all shows it\n",
                   kit_fmt_size(rest, sizeof rest, (uint64_t)(size - from - run)));
        }
    }

    free(data);
    return 0;
}
