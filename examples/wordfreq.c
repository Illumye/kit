/*
 * Counting words in a text, which is what the string views, the map and the
 * growable containers are for: not one byte of the input is copied to be
 * looked at, only the words worth keeping are.
 *
 * Shows: KitStr slicing and comparison, KitMap as a counter, KitBuf to build
 * output, the kit_array_* macros over a struct of your own, and the hashes.
 *
 *   ./examples/wordfreq --top 5 examples/demo/prose.txt
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"

typedef struct {
    const char *word;
    size_t      count;
} Entry;

typedef struct {            /* any struct with these three fields works */
    Entry *items;
    size_t count;
    size_t capacity;
} Entries;

/* Words that say nothing about a text. Kept as views over a literal, so the
 * list costs nothing at run time. */
typedef struct { KitStr *items; size_t count, capacity; } Words;

/* An array of pointers, which the search macros can compare. */
typedef struct { const char **items; size_t count, capacity; } Names;

static void add_stop_words(Words *stop) {
    static const char *const list[] = {
        "the", "a", "an", "and", "or", "of", "to", "in", "is", "it", "that", "this"
    };
    kit_array_reserve(stop, KIT_COUNTOF(list));
    for (size_t i = 0; i < KIT_COUNTOF(list); i++)
        kit_array_push(stop, kit_str_from(list[i]));
}

static bool is_stop_word(const Words *stop, KitStr word) {
    kit_array_each(KitStr, it, stop)
        if (kit_str_eq_nocase(*it, word)) return true;
    return false;
}

/* What the kit_map_each macro hands out, named rather than left implicit. */
static void print_entry(const KitMapEntry *entry, size_t rank) {
    printf("  %2zu. %-16s %08x\n", rank, entry->key, kit_hash_str(entry->key));
}

/* Letters and apostrophes make a word; everything else separates them. */
static bool is_word_byte(char c) {
    unsigned char u = (unsigned char)c;
    return isalpha(u) || c == '\'';
}

static KitStr next_word(KitStr *rest) {
    while (rest->count > 0 && !is_word_byte(rest->data[0])) kit_str_take(rest, 1);
    size_t n = 0;
    while (n < rest->count && is_word_byte(rest->data[n])) n++;
    return kit_str_take(rest, n);
}

/* Everything one pass over the text needs, and what it found. A struct rather
 * than a handful of locals because that is the shape a benchmark can call
 * again: the harness hands a single pointer back to the body it times. */
typedef struct {
    KitStr    text;
    Words    *stop;
    KitArena *arena;
    KitMap   *counts;
    size_t    total, skipped;
} Pass;

/* Each word, lowercased into the arena and counted in the map. Running this
 * twice must leave no trace of the first time, and since the keys live in the
 * arena, emptying one means emptying the other in the same breath. */
static void count_words(void *context) {
    Pass *pass = (Pass *)context;

    kit_arena_reset(pass->arena);
    kit_map_reset(pass->counts);
    pass->total = pass->skipped = 0;

    KitStr rest = pass->text;
    for (KitStr word = next_word(&rest); word.count > 0; word = next_word(&rest)) {
        pass->total++;
        if (is_stop_word(pass->stop, word)) { pass->skipped++; continue; }

        char *key = kit_arena_strndup(pass->arena, word.data, word.count);
        for (char *c = key; *c; c++) *c = (char)tolower((unsigned char)*c);

        size_t *counter = (size_t *)kit_map_get(pass->counts, key);
        if (counter) { (*counter)++; continue; }

        counter  = kit_arena_alloc_array(pass->arena, size_t, 1);
        *counter = 1;
        kit_map_set(pass->counts, key, counter);
    }
}

static int by_count_then_word(const void *a, const void *b) {
    const Entry *x = (const Entry *)a, *y = (const Entry *)b;
    if (x->count != y->count) return x->count < y->count ? 1 : -1;
    return strcmp(x->word, y->word);
}

int main(int argc, char **argv) {
    const char *prog = kit_cli_shift(&argc, &argv);

    int  top = 10, bench = 0;
    bool help = false, keep_stop_words = false;
    KitCliOpt opts[] = {
        KIT_CLI_INT ('n', "top", "N", "How many words to print", &top),
        KIT_CLI_INT ('b', "bench", "N", "Time the counting pass N times", &bench),
        KIT_CLI_FLAG('s', "stop-words", "Count the common words too", &keep_stop_words),
        KIT_CLI_FLAG('h', "help", "Show this help", &help),
    };
    if (!kit_cli_parse_arr(opts, &argc, &argv, NULL)) return 1;
    if (help || argc != 1) {
        kit_cli_usage_arr(stdout, prog, opts);
        return help ? 0 : 1;
    }

    kit_log_set_fields(KIT_LOG_FIELD_LEVEL);

    KitError err  = KIT_ZEROED;
    size_t   size = 0;
    char    *text = kit_fs_read_sized(argv[0], &size, &err);
    if (!text) {
        KIT_ERROR("%s", err.message);
        return 1;
    }

    /* A fingerprint of the input, mixed so that the low bits are usable. */
    uint32_t fingerprint = kit_hash_mix32(kit_hash_bytes(text, size));

    Words stop = KIT_ZEROED;
    if (!keep_stop_words) add_stop_words(&stop);

    /* The map stores counters that live in an arena, so nothing is freed one
     * by one and the keys stay valid as long as the arena does. */
    KitArena arena  = KIT_ZEROED;
    KitMap   counts = KIT_ZEROED;
    Pass     pass   = { kit_str_from_parts(text, size), &stop, &arena, &counts, 0, 0 };

    count_words(&pass);

    /* The same pass again, timed, when asked. Nothing is set up for it: the
     * work the program does anyway is the work worth measuring. */
    if (bench > 0) {
        KitBench timed = kit_bench_run("counting", (size_t)bench, count_words, &pass);
        kit_bench_report(stdout, &timed);
    }

    size_t total = pass.total, skipped = pass.skipped;
    free(text);                             /* the passes read straight from it */

    /* Ranking wants an array, which the map does not pretend to be. */
    /* A word that turned out to be noise after all can simply go. */
    if (kit_map_has(&counts, "dog") && kit_map_delete(&counts, "dog"))
        KIT_DEBUG("dropped a word that says nothing here");

    Entries ranked = KIT_ZEROED;
    kit_array_reserve(&ranked, counts.count);
    kit_map_each(&counts, e) {
        Entry entry = { e->key, *(const size_t *)e->value };
        kit_array_push(&ranked, entry);
    }
    if (ranked.count > 1)
        qsort(ranked.items, ranked.count, sizeof(*ranked.items), by_count_then_word);

    /* One buffer for the whole report, printed once. */
    KitBuf out = KIT_ZEROED;
    kit_buf_printf(&out, "%zu words, %zu distinct", total, ranked.count);
    if (skipped) kit_buf_printf(&out, ", %zu common ones skipped", skipped);
    kit_buf_printf(&out, ", fingerprint %08x\n", fingerprint);

    size_t shown = (size_t)(top < 0 ? 0 : top);
    if (shown > ranked.count) shown = ranked.count;
    for (size_t i = 0; i < shown; i++) {
        kit_buf_printf(&out, "%6zu  ", ranked.items[i].count);
        kit_buf_append(&out, ranked.items[i].word);
        kit_buf_append_char(&out, '\n');
    }
    if (ranked.count > 0) {
        kit_buf_append(&out, "most common: ");
        kit_buf_append_str(&out, kit_str_from(kit_array_first(&ranked).word));
        kit_buf_append(&out, ", rarest: ");
        kit_buf_append_str(&out, kit_str_from(kit_array_last(&ranked).word));
        kit_buf_append_n(&out, " (once or nearly)\n", 18);
    }
    fputs(kit_buf_cstr(&out), stdout);

    /* A second report, on the same buffer: emptied rather than freed. */
    kit_buf_reset(&out);
    kit_buf_printf(&out, "the ranking held %zu entries", ranked.count);
    char *saved = kit_buf_dup(&out);        /* a copy that outlives the buffer */

    /* Dropping entries: the last one costs nothing, and any other one costs
     * the ordering, which is why it is called a swap. */
    if (ranked.count > 2) {
        Entry dropped = kit_array_pop(&ranked);
        kit_array_swap_remove(&ranked, 0);
        printf("%s, now %zu after dropping %s and the leader\n",
               saved, ranked.count, dropped.word);
    } else {
        printf("%s\n", saved);
    }
    free(saved);

    /* Searching by value. The macros compare with ==, so they work on an
     * array of pointers and not on the array of structs above. */
    Names printed = KIT_ZEROED;
    for (size_t i = 0; i < shown && i < ranked.count; i++)
        kit_array_push(&printed, ranked.items[i].word);

    if (printed.count > 0) {
        size_t at = 0;
        kit_array_find(&printed, kit_array_last(&printed), at);
        printf("the last word printed sits at index %zu of %zu\n", at, printed.count);
#ifdef kit_array_contains        /* absent where GNU statement expressions are */
        if (!kit_array_contains(&printed, kit_array_first(&printed)))
            KIT_WARN("the list lost its head");
#endif
    }
    kit_array_free(&printed);

    /* The same walk the macro does, written out: a slot can be empty or hold
     * the trace of a deleted key, which kit_map_entry_live tells apart. */
    size_t live = 0, rank = 0;
    for (size_t i = 0; i < counts.capacity; i++)
        if (kit_map_entry_live(&counts.entries[i])) {
            live++;
            if (rank < 3) print_entry(&counts.entries[i], ++rank);
        }
    printf("%zu live slots of %zu\n", live, counts.capacity);

    kit_map_reset(&counts);                 /* keeps the allocation for reuse */

    kit_buf_free(&out);
    kit_array_free(&ranked);
    kit_array_free(&stop);
    kit_map_free(&counts);
    kit_arena_free(&arena);
    return 0;
}
