/*
 * Counting words in a text, which is what the string views, the map and the
 * growable containers are for: not one byte of the input is copied to be
 * looked at, only the words worth keeping are.
 *
 * Shows: KitStr slicing, KitMap as a counter, KitBuf to build output, the
 * kit_array_* macros over a struct of your own, and UTF-8 decoding used to
 * decide where a word begins and ends.
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

/* Words that say nothing about a text. Dropped once the count is in rather
 * than tested on the way: twelve lookups at the end cost less than a walk of
 * the list for every word of the input, and the totals come out the same. */
static const char *const stop_words[] = {
    "the", "a", "an", "and", "or", "of", "to", "in", "is", "it", "that", "this"
};

/* Letters and apostrophes make a word; everything else separates them.
 *
 * Above ASCII it is a guess, since deciding properly needs the Unicode tables
 * and those are bigger than the library. Everything up there counts as a
 * letter except the two ranges that plainly are not: Latin-1 punctuation and
 * symbols, and general punctuation, which is where the dashes and the
 * typographic quotes live. Without that exclusion an em dash glues the words
 * on either side of it into one. */
static bool is_word_codepoint(uint32_t codepoint) {
    if (codepoint < 0x80) return isalpha((int)codepoint) || codepoint == '\'';
    if (codepoint >= 0x00a0 && codepoint <= 0x00bf) return false;
    if (codepoint >= 0x2000 && codepoint <= 0x206f) return false;
    return true;
}

/* A word is taken a character at a time, not a byte at a time: 'é' is two
 * bytes, and a scan that walked over them separately would cut it in half. */
static KitStr next_word(KitStr *rest) {
    uint32_t codepoint = 0;

    while (rest->count > 0) {
        size_t used = kit_utf8_decode(*rest, &codepoint);
        if (is_word_codepoint(codepoint)) break;
        kit_str_take(rest, used);
    }

    size_t taken = 0;
    while (taken < rest->count) {
        KitStr from = kit_str_from_parts(rest->data + taken, rest->count - taken);
        size_t used = kit_utf8_decode(from, &codepoint);
        if (!is_word_codepoint(codepoint)) break;
        taken += used;
    }
    return kit_str_take(rest, taken);
}

/* Everything one pass over the text needs, and what it found. A struct rather
 * than a handful of locals because that is the shape a benchmark can call
 * again: the harness hands a single pointer back to the body it times. */
typedef struct {
    KitStr    text;
    bool      keep_stop_words;
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

        /* Lowercased where that can be done without a table: folding case
         * above ASCII is a Unicode question, and this library does not carry
         * the answer. "Été" and "été" therefore count apart. */
        char *key = kit_arena_strndup(pass->arena, word.data, word.count);
        for (char *c = key; *c; c++)
            if ((unsigned char)*c < 0x80) *c = (char)tolower((unsigned char)*c);

        size_t *counter = (size_t *)kit_map_get(pass->counts, key);
        if (counter) { (*counter)++; continue; }

        counter  = kit_arena_alloc_array(pass->arena, size_t, 1);
        *counter = 1;
        kit_map_set(pass->counts, key, counter);
    }

    if (pass->keep_stop_words) return;
    for (size_t i = 0; i < KIT_COUNTOF(stop_words); i++) {
        const size_t *counter = (const size_t *)kit_map_get(pass->counts, stop_words[i]);
        if (!counter) continue;
        pass->skipped += *counter;
        kit_map_delete(pass->counts, stop_words[i]);
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

    /* A fingerprint of the input, mixed so that the low bits are usable: two
     * runs that report it are counting the same text or they are not. */
    uint32_t fingerprint = kit_hash_mix32(kit_hash_bytes(text, size));

    /* The map stores counters that live in an arena, so nothing is freed one
     * by one and the keys stay valid as long as the arena does. */
    KitArena arena  = KIT_ZEROED;
    KitMap   counts = KIT_ZEROED;
    Pass     pass   = { kit_str_from_parts(text, size), keep_stop_words,
                        &arena, &counts, 0, 0 };

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
    Entries ranked = KIT_ZEROED;
    kit_array_reserve(&ranked, counts.count);
    kit_map_each(&counts, e) {
        Entry entry = { e->key, *(const size_t *)e->value };
        kit_array_push(&ranked, entry);
    }
    kit_array_sort(&ranked, by_count_then_word);

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
    if (shown < ranked.count)
        kit_buf_printf(&out, "  ... and %zu more, --top %zu shows them all\n",
                       ranked.count - shown, ranked.count);

    fputs(kit_buf_cstr(&out), stdout);

    kit_buf_free(&out);
    kit_array_free(&ranked);
    kit_map_free(&counts);
    kit_arena_free(&arena);
    return 0;
}
