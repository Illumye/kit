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
    CHECK(a.buffer != NULL);
    CHECK_INT(a.length, 4096);
    CHECK_INT(a.offset, 0);

    char *one = arena_alloc(&a, 1);
    if (!CHECK(one != NULL)) { arena_free(&a); return; }
    CHECK_INT(one[0], 0);                       /* freshly zeroed */

    /* A one-byte block must not leave the next pointer misaligned. */
    void **ptr = arena_alloc(&a, sizeof(void *));
    CHECK_INT((uintptr_t)ptr % sizeof(void *), 0);
    CHECK(*ptr == NULL);

    int *nums = arena_alloc_array(&a, int, 16);
    if (!CHECK(nums != NULL)) { arena_free(&a); return; }
    for (int i = 0; i < 16; i++) CHECK_INT(nums[i], 0);
    CHECK(a.offset >= 1 + sizeof(void *) + 16 * sizeof(int));

    arena_free(&a);
    CHECK(a.buffer == NULL);
    CHECK_INT(a.length, 0);
}

TEST(arena_reset_reuses_the_block) {
    Arena a = arena_make(1024);
    char *first = arena_alloc(&a, 64);
    memset(first, 'x', 64);

    arena_reset(&a);
    CHECK_INT(a.offset, 0);

    char *again = arena_alloc(&a, 64);
    CHECK(again == first);        /* same block handed out again */
    CHECK_INT(again[0], 0);       /* and re-zeroed */
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
    RUN(arena_reset_reuses_the_block);
    RUN(hm_set_get_delete);
    RUN(hm_on_an_empty_map_is_safe);
    RUN(hm_stores_null_values);
    RUN(hm_grows_and_keeps_every_entry);
    RUN(hm_reset_keeps_the_allocation);
    RUN(hm_sliding_window_does_not_degrade);
    return utest_report();
}
