/*
 * Arena allocator and hash map.
 * Includes the regression test for the tombstone-blind load factor.
 */

#define UTILS_IMPLEMENTATION
#include "../utils.h"
#include "utest.h"

/* --- arena ---------------------------------------------------------------- */

TEST(arena_allocates_aligned_zeroed_blocks) {
    Arena a = arena_make(4096);
    CHECK(a.first != NULL);
    CHECK_INT(arena_used(&a), 0);
    CHECK_INT(arena_capacity(&a), 4096);

    char *one = arena_alloc(&a, 1);
    if (!CHECK(one != NULL)) { arena_free(&a); return; }
    CHECK_INT(one[0], 0);                       /* freshly zeroed */

    /* A one-byte block must not leave the next one misaligned. */
    void **ptr = arena_alloc(&a, sizeof(void *));
    CHECK_INT((uintptr_t)ptr % sizeof(max_align_t), 0);
    CHECK(*ptr == NULL);

    int *nums = arena_alloc_array(&a, int, 16);
    if (!CHECK(nums != NULL)) { arena_free(&a); return; }
    for (int i = 0; i < 16; i++) CHECK_INT(nums[i], 0);
    CHECK(arena_used(&a) >= 1 + sizeof(void *) + 16 * sizeof(int));

    arena_free(&a);
    CHECK(a.first == NULL);
    CHECK_INT(arena_capacity(&a), 0);
}

TEST(arena_honours_over_alignment) {
    Arena a = {0};
    for (size_t align = 1; align <= 256; align *= 2) {
        arena_alloc(&a, 1);                     /* knock the offset askew */
        void *p = arena_alloc_aligned(&a, 32, align);
        if (!CHECK(p != NULL)) break;
        if (!CHECK((uintptr_t)p % align == 0))
            printf("      align=%zu gave %p\n", align, p);
    }
    arena_free(&a);
}

/* A zero-initialised arena must work with no preparation at all. */
TEST(arena_zero_initialised_grows_on_demand) {
    Arena a = {0};
    CHECK_INT(arena_capacity(&a), 0);

    char *p = arena_alloc(&a, 10);
    CHECK(p != NULL);
    CHECK(arena_capacity(&a) >= ARENA_REGION_SIZE);
    arena_free(&a);
}

/* The old arena aborted when full. It must now chain another region, and a
 * single allocation larger than the region size must still be served. */
TEST(arena_chains_regions_instead_of_failing) {
    Arena a = arena_make(1024);
    CHECK_INT(arena_capacity(&a), 1024);

    char *blocks[64];
    for (int i = 0; i < 64; i++) {
        blocks[i] = arena_alloc(&a, 100);       /* 6400 bytes into 1024 */
        if (!CHECK(blocks[i] != NULL)) break;
        memset(blocks[i], 'a' + (i % 26), 100);
    }
    CHECK(arena_capacity(&a) > 1024);

    /* Earlier blocks must survive the chain growing. */
    int corrupted = 0;
    for (int i = 0; i < 64; i++)
        for (int j = 0; j < 100; j++)
            if (blocks[i][j] != 'a' + (i % 26)) corrupted++;
    CHECK_INT(corrupted, 0);

    char *huge = arena_alloc(&a, 100000);       /* bigger than the hint */
    if (CHECK(huge != NULL)) {
        memset(huge, 1, 100000);
        CHECK_INT(huge[99999], 1);
    }
    arena_free(&a);
}

TEST(arena_reset_reuses_the_regions) {
    Arena a = arena_make(1024);
    char *first = arena_alloc(&a, 64);
    memset(first, 'x', 64);
    for (int i = 0; i < 100; i++) arena_alloc(&a, 100);   /* force a chain */
    size_t capacity = arena_capacity(&a);

    arena_reset(&a);
    CHECK_INT(arena_used(&a), 0);
    CHECK_INT(arena_capacity(&a), capacity);    /* regions kept, not freed */

    char *again = arena_alloc(&a, 64);
    CHECK(again == first);                      /* same block handed out */
    CHECK_INT(again[0], 0);                     /* and re-zeroed */
    arena_free(&a);
}

TEST(arena_mark_and_rewind) {
    Arena a = arena_make(256);

    char *keep = arena_strdup(&a, "kept");
    Arena_Mark mark = arena_mark(&a);
    size_t used_at_mark = arena_used(&a);

    for (int i = 0; i < 50; i++) arena_sprintf(&a, "scratch %d", i);
    CHECK(arena_used(&a) > used_at_mark);

    arena_rewind(&a, mark);
    CHECK_INT(arena_used(&a), used_at_mark);
    CHECK_STR(keep, "kept");                    /* untouched by the rewind */

    /* The reclaimed space is handed out again. */
    char *reused = arena_alloc(&a, 8);
    CHECK(arena_used(&a) <= used_at_mark + 8 + sizeof(max_align_t));
    CHECK(reused != NULL);

    /* A mark taken from an empty arena rewinds everything. */
    Arena b = {0};
    Arena_Mark empty = arena_mark(&b);
    arena_alloc(&b, 100);
    arena_rewind(&b, empty);
    CHECK_INT(arena_used(&b), 0);

    arena_free(&a);
    arena_free(&b);
}

TEST(arena_string_helpers) {
    Arena a = {0};
    CHECK_STR(arena_strdup(&a, "hello"), "hello");
    CHECK_STR(arena_strdup(&a, ""), "");
    CHECK_STR(arena_strdup_n(&a, "truncated", 4), "trun");
    CHECK_STR(arena_sprintf(&a, "%s-%d-%.2f", "x", 42, 1.5), "x-42-1.50");

    /* Longer than a region, to exercise the growth path. */
    char *big = arena_sprintf(&a, "%0*d", 100000, 7);
    CHECK_INT(strlen(big), 100000);
    arena_free(&a);
}

/* --- hash map ------------------------------------------------------------- */

TEST(hm_set_get_delete) {
    HashMap hm = {0};
    int a = 1, b = 2;

    CHECK(hm_set(&hm, "alpha", &a));      /* true: new key */
    CHECK(hm_set(&hm, "beta", &b));
    CHECK(!hm_set(&hm, "alpha", &b));     /* false: update */
    CHECK_INT(hm.count, 2);

    CHECK(hm_get(&hm, "alpha") == &b);
    CHECK(hm_get(&hm, "beta") == &b);
    CHECK(hm_get(&hm, "absent") == NULL);
    CHECK(hm_has(&hm, "alpha"));
    CHECK(!hm_has(&hm, "absent"));

    CHECK(hm_delete(&hm, "alpha"));
    CHECK(!hm_delete(&hm, "alpha"));      /* already gone */
    CHECK_INT(hm.count, 1);
    CHECK(!hm_has(&hm, "alpha"));
    CHECK(hm_has(&hm, "beta"));           /* the tombstone must not hide it */

    hm_free(&hm);
    CHECK_INT(hm.capacity, 0);
}

TEST(hm_on_an_empty_map_is_safe) {
    HashMap hm = {0};
    CHECK(hm_get(&hm, "x") == NULL);
    CHECK(!hm_has(&hm, "x"));
    CHECK(!hm_delete(&hm, "x"));
    hm_free(&hm);
}

TEST(hm_stores_null_values) {
    HashMap hm = {0};
    hm_set(&hm, "key", NULL);
    CHECK(hm_get(&hm, "key") == NULL);
    CHECK(hm_has(&hm, "key"));            /* presence is not value != NULL */
    hm_free(&hm);
}

TEST(hm_grows_and_keeps_every_entry) {
    enum { N = 10000 };
    static char keys[N][16];
    HashMap hm = {0};

    for (int i = 0; i < N; i++) {
        snprintf(keys[i], sizeof(keys[i]), "key%d", i);
        hm_set(&hm, keys[i], (void *)(intptr_t)(i + 1));
    }
    CHECK_INT(hm.count, N);

    int wrong = 0;
    for (int i = 0; i < N; i++)
        if (hm_get(&hm, keys[i]) != (void *)(intptr_t)(i + 1)) wrong++;
    CHECK_INT(wrong, 0);

    int visited = 0;
    hm_foreach(&hm, e) { CHECK(e->key != NULL); visited++; }
    CHECK_INT(visited, N);

    hm_free(&hm);
}

TEST(hm_reset_keeps_the_allocation) {
    HashMap hm = {0};
    int v = 1;
    hm_set(&hm, "a", &v);
    size_t cap = hm.capacity;

    hm_reset(&hm);
    CHECK_INT(hm.count, 0);
    CHECK_INT(hm.capacity, cap);
    CHECK(!hm_has(&hm, "a"));

    hm_set(&hm, "b", &v);
    CHECK(hm_has(&hm, "b"));
    hm_free(&hm);
}

/* Regression: the load factor counted live entries only, so a set/delete
 * workload filled the table with tombstones that were never reclaimed. Probe
 * sequences then grew until every lookup scanned the whole table.
 *
 * A sliding window keeps a constant population, so the capacity must stay
 * bounded and the run must stay fast. Before the fix this took ~550 ms. */
TEST(hm_sliding_window_does_not_degrade) {
    enum { LIVE = 20000, TOTAL = 200000 };
    static char keys[TOTAL][16];
    for (int i = 0; i < TOTAL; i++) snprintf(keys[i], sizeof(keys[i]), "key%d", i);

    HashMap hm = {0};
    for (int i = 0; i < LIVE; i++) hm_set(&hm, keys[i], (void *)(intptr_t)1);

    size_t cap_when_full = hm.capacity;
    Stopwatch sw = sw_start();
    for (int i = LIVE; i < TOTAL; i++) {
        hm_set(&hm, keys[i], (void *)(intptr_t)1);
        hm_delete(&hm, keys[i - LIVE]);
    }
    double ms = sw_elapsed_ms(sw);

    CHECK_INT(hm.count, LIVE);
    CHECK_INT(hm.capacity, cap_when_full);   /* rehashed in place, never grown */
    CHECK(hm.used <= hm.capacity);
    CHECK(hm_has(&hm, keys[TOTAL - 1]));
    CHECK(!hm_has(&hm, keys[0]));

    /* Generous bound: the point is the order of magnitude, not the machine.
     * The unfixed version needed ~12 s here, the fixed one ~80 ms. */
    double budget = UTEST_SANITIZED ? 3000.0 : 1000.0;
    if (!CHECK(ms < budget))
        printf("      sliding window took %.0f ms (budget %.0f)\n", ms, budget);

    hm_free(&hm);
}

int main(void) {
    log_set_level(LOG_CRITICAL);
    utest_begin("memory");
    RUN(arena_allocates_aligned_zeroed_blocks);
    RUN(arena_honours_over_alignment);
    RUN(arena_zero_initialised_grows_on_demand);
    RUN(arena_chains_regions_instead_of_failing);
    RUN(arena_reset_reuses_the_regions);
    RUN(arena_mark_and_rewind);
    RUN(arena_string_helpers);
    RUN(hm_set_get_delete);
    RUN(hm_on_an_empty_map_is_safe);
    RUN(hm_stores_null_values);
    RUN(hm_grows_and_keeps_every_entry);
    RUN(hm_reset_keeps_the_allocation);
    RUN(hm_sliding_window_does_not_degrade);
    return utest_report();
}
