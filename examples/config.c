/*
 * Reading a configuration file, which is where structured errors earn their
 * keep: a parse failure has to say what was wrong and on which line, and the
 * caller adds the file name on the way out.
 *
 * Shows: KitStr slicing and parsing, KitMap for the key-value store, KitArena
 * to own the copies, and KitError carrying a context chain.
 *
 *   ./examples/config examples/demo/app.conf
 *   ./examples/config examples/demo/broken.conf   # fails, and says why
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"

/* Keys and values are slices of the file, which is freed before they are read,
 * so both are copied into an arena the config owns. */
typedef struct {
    KitArena arena;
    KitMap   entries;      /* "section.key" -> char * */
    char    *source;       /* the file name, owned by the arena */
} Config;

static void config_free(Config *cfg) {
    kit_map_free(&cfg->entries);
    kit_arena_free(&cfg->arena);
}

/* "  key = value  # trailing" -> key and value, comment and spaces removed. */
static bool split_assignment(KitStr line, KitStr *key, KitStr *value) {
    size_t at = kit_str_find_char(line, '=');
    if (at == KIT_NPOS) return false;

    /* Trimming one side at a time, because the key keeps its left edge for
     * the error message and the value does not. */
    *key   = kit_str_trim_right(kit_str_trim_left(kit_str_take(&line, at)));
    kit_str_take(&line, 1);                       /* the '=' itself */

    /* A trailing comment ends the value, unless it is inside quotes. */
    KitStr rest = kit_str_trim(line);
    if (!kit_str_starts_with(rest, KIT_STR_LIT("\""))) {
        size_t comment = kit_str_find(rest, KIT_STR_LIT(" #"));
        if (comment != KIT_NPOS) rest = kit_str_take(&rest, comment);
    }
    *value = kit_str_trim_right(rest);
    return key->count > 0;
}

/* "1h30" and "45s" as seconds, which a plain number cannot express. */
static bool parse_duration(KitStr text, uint64_t *seconds) {
    uint64_t total = 0;
    while (text.count > 0) {
        size_t digits = 0;
        while (digits < text.count && isdigit((unsigned char)text.data[digits])) digits++;
        if (digits == 0) return false;

        uint64_t value = 0;
        if (!kit_str_to_u64(kit_str_take(&text, digits), &value)) return false;

        if      (kit_str_starts_with_cstr(text, "h")) total += value * 3600;
        else if (kit_str_starts_with_cstr(text, "m")) total += value * 60;
        else if (kit_str_starts_with_cstr(text, "s")) total += value;
        else return false;
        kit_str_take(&text, 1);
    }
    *seconds = total;
    return true;
}

static bool parse(Config *cfg, const char *path, KitError *err) {
    char *text = kit_fs_read(path, err);
    if (!text) return false;

    /* The arena outlives the parse, so the name can be kept there too, and a
     * small scratch buffer for the section name comes from the same place. */
    cfg->source = kit_arena_strdup(&cfg->arena, path);
    char *seen_sections = (char *)kit_arena_alloc(&cfg->arena, 256);
    seen_sections[0] = '\0';

    KitStr rest    = kit_str_from(text);
    KitStr section = KIT_STR_LIT("");
    KitStr line;
    int    number  = 0;
    bool   result  = true;

    while (kit_str_next(&rest, '\n', &line)) {
        number++;
        line = kit_str_trim(line);

        /* Comments and blank lines carry nothing. */
        if (line.count == 0) continue;
        if (kit_str_starts_with_cstr(line, "#") || kit_str_starts_with_cstr(line, ";")) continue;

        if (kit_str_starts_with_cstr(line, "[")) {
            if (!kit_str_ends_with_cstr(line, "]")) {
                kit_error_set(err, KIT_ERR_INVALID, "line %d: unterminated section header", number);
                KIT_BAIL(false);
            }
            /* The two tests above found a '[' and a ']', and they cannot be
             * the same character, so there are at least two to cut. Reorder
             * those tests and the second cut underflows a size_t instead of
             * shortening anything. */
            KIT_ASSERT(line.count >= 2);

            kit_str_take(&line, 1);               /* '[' */
            kit_str_take_right(&line, 1);         /* ']' */
            section = kit_str_trim(line);
            if (kit_str_eq(section, KIT_STR_LIT("default"))) KIT_DEBUG("the default section");
            if (section.count == 0) {
                kit_error_set(err, KIT_ERR_INVALID, "line %d: empty section name", number);
                KIT_BAIL(false);
            }
            continue;
        }

        KitStr key, value;
        if (!split_assignment(line, &key, &value)) {
            kit_error_set(err, KIT_ERR_INVALID,
                          "line %d: expected 'key = value', got '" KIT_STR_FMT "'",
                          number, KIT_STR_ARG(line));
            KIT_BAIL(false);
        }

        /* The arena owns every string the map points at, so one free at the
         * end releases the lot. */
        char *full = section.count
            ? kit_arena_printf(&cfg->arena, KIT_STR_FMT "." KIT_STR_FMT,
                               KIT_STR_ARG(section), KIT_STR_ARG(key))
            : kit_arena_strndup(&cfg->arena, key.data, key.count);

        if (!kit_map_set(&cfg->entries, full, kit_arena_strndup(&cfg->arena, value.data, value.count)))
            KIT_WARN("%s is set twice, the last one wins", full);
    }

cleanup:
    free(text);
    return result;
}

/* Typed reads. A missing key is not a failure here: the caller passes the
 * default it wants, which is what a configuration is for. */
static const char *get_text(const Config *cfg, const char *key, const char *fallback) {
    const char *found = (const char *)kit_map_get(&cfg->entries, key);
    return found ? found : fallback;
}

static bool get_int(const Config *cfg, const char *key, int64_t *out, KitError *err) {
    const char *raw = (const char *)kit_map_get(&cfg->entries, key);
    if (!raw) return true;                        /* absent: keep the default */
    if (kit_str_to_i64(kit_str_from(raw), out)) return true;
    return kit_error_set(err, KIT_ERR_INVALID, "%s: expected a whole number, got '%s'", key, raw);
}

static bool get_real(const Config *cfg, const char *key, double *out, KitError *err) {
    const char *raw = (const char *)kit_map_get(&cfg->entries, key);
    if (!raw) return true;
    if (kit_str_to_double(kit_str_from(raw), out)) return true;
    return kit_error_set(err, KIT_ERR_INVALID, "%s: expected a number, got '%s'", key, raw);
}

static bool get_flag(const Config *cfg, const char *key, bool *out) {
    const char *raw = (const char *)kit_map_get(&cfg->entries, key);
    if (!raw) return false;
    KitStr v = kit_str_from(raw);
    *out = kit_str_eq_nocase(v, KIT_STR_LIT("yes")) || kit_str_eq_nocase(v, KIT_STR_LIT("true"))
        || kit_str_eq_cstr(v, "1");
    return true;
}

int main(int argc, char **argv) {
    const char *prog = kit_cli_shift(&argc, &argv);

    bool list = false, help = false;
    KitCliOpt opts[] = {
        KIT_CLI_FLAG('l', "list", "Print every key that was read", &list),
        KIT_CLI_FLAG('h', "help", "Show this help",                &help),
    };
    if (!kit_cli_parse_arr(opts, &argc, &argv, NULL)) return 1;
    if (help || argc != 1) {
        kit_cli_usage_arr(stdout, prog, opts);
        fprintf(stdout, "\nOne argument: the configuration file to read.\n");
        return help ? 0 : 1;
    }

    kit_log_set_fields(KIT_LOG_FIELD_LEVEL);

    Config   cfg = KIT_ZEROED;
    KitError err = KIT_ZEROED;
    const char *path = argv[0];

    if (!parse(&cfg, path, &err)) {
        kit_error_context(&err, "reading %s", path);
        KIT_ERROR("%s (%s)", err.message, kit_error_code_name(err.code));
        config_free(&cfg);
        return 1;
    }

    /* Defaults first, then whatever the file overrides. */
    const char *host = get_text(&cfg, "server.host", "127.0.0.1");
    int64_t     port = 80, connections = 64;
    double      timeout = 1.0, rate = 1.0;
    bool        tls = false;

    if (!get_int(&cfg, "server.port", &port, &err)
     || !get_int(&cfg, "limits.max_connections", &connections, &err)
     || !get_real(&cfg, "server.timeout", &timeout, &err)
     || !get_real(&cfg, "limits.rate_per_second", &rate, &err)) {
        kit_error_context(&err, "reading %s", path);
        KIT_ERROR("%s", err.message);
        config_free(&cfg);
        return 1;
    }
    if (!get_flag(&cfg, "server.tls", &tls)) KIT_INFO("server.tls is unset, assuming no");

    /* A value with a shape of its own, parsed from the same views. */
    const char *raw_keepalive = get_text(&cfg, "server.keepalive", "1h30m");
    uint64_t    keepalive     = 0;
    if (!parse_duration(kit_str_from(raw_keepalive), &keepalive)) {
        KitErrorCode code = KIT_ERR_INVALID;
        KIT_WARN("server.keepalive: '%s' is not a duration (%s), using an hour",
                 raw_keepalive, kit_error_code_name(code));
        keepalive = 3600;
    }

    printf("host        %s\n", host);
    printf("port        %lld\n", (long long)port);
    printf("timeout     %.2f s\n", timeout);
    printf("tls         %s\n", tls ? "on" : "off");
    printf("connections %lld\n", (long long)connections);
    printf("rate        %.2f/s\n", rate);
    printf("keepalive   %llu s\n", (unsigned long long)keepalive);

    /* Paths are worth a second look: where they point, and what they end in. */
    const char *cache = get_text(&cfg, "paths.cache", "(none)");
    KitStr      log   = kit_str_from(get_text(&cfg, "paths.log", ""));
    printf("cache       %s%s\n", cache,
           kit_str_starts_with_cstr(kit_str_from(cache), "/") ? "  (absolute)" : "");
    if (kit_str_ends_with(log, KIT_STR_LIT(".log")) && kit_str_contains(log, KIT_STR_LIT("/app")))
        printf("log         " KIT_STR_FMT "  (a log file under an app directory)\n",
               KIT_STR_ARG(log));

    /* Sections, read back out of the keys themselves. */
    KitBuf sections = KIT_ZEROED;
    kit_map_each(&cfg.entries, e) {
        KitStr key  = kit_str_from(e->key);
        KitStr head = kit_str_cut_str(&key, KIT_STR_LIT("."));
        if (key.count == 0) continue;             /* no section in this key */

        KitStr seen = kit_str_from(kit_buf_cstr(&sections));
        if (!kit_str_contains(seen, head)) {
            kit_buf_append_str(&sections, head);
            kit_buf_append_char(&sections, ' ');
        }
    }
    printf("sections    %s\n", kit_buf_cstr(&sections));
    kit_buf_free(&sections);

    if (list) {
        printf("\n%zu keys, %zu bytes of arena for their storage\n",
               cfg.entries.count, kit_arena_used(&cfg.arena));
        kit_map_each(&cfg.entries, e)
            printf("  %-28s %s\n", e->key, (const char *)e->value);
    }

    config_free(&cfg);
    return 0;
}
