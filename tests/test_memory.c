/*
 * KitArena allocator and hash map.
 * Includes the regression test for the tombstone-blind load factor.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

/* --- arena ---------------------------------------------------------------- */

TEST(arena_allocates_aligned_zeroed_blocks) {
    KitArena a = kit_arena_make(4096);
    CHECK(a.first != NULL);
    CHECK_INT(kit_arena_used(&a), 0);
    CHECK_INT(kit_arena_capacity(&a), 4096);

    char *one = kit_arena_alloc(&a, 1);
    if (!CHECK(one != NULL)) { kit_arena_free(&a); return; }
    CHECK_INT(one[0], 0);                       /* freshly zeroed */

    /* A one-byte block must not leave the next one misaligned. */
    void **ptr = kit_arena_alloc(&a, sizeof(void *));
    CHECK_INT((uintptr_t)ptr % KIT_MAX_ALIGN, 0);
    CHECK(*ptr == NULL);

    int *nums = kit_arena_alloc_array(&a, int, 16);
    if (!CHECK(nums != NULL)) { kit_arena_free(&a); return; }
    for (int i = 0; i < 16; i++) CHECK_INT(nums[i], 0);
    CHECK(kit_arena_used(&a) >= 1 + sizeof(void *) + 16 * sizeof(int));

    kit_arena_free(&a);
    CHECK(a.first == NULL);
    CHECK_INT(kit_arena_capacity(&a), 0);
}

TEST(arena_honours_over_alignment) {
    KitArena a = {0};
    for (size_t align = 1; align <= 256; align *= 2) {
        kit_arena_alloc(&a, 1);                     /* knock the offset askew */
        void *p = kit_arena_alloc_aligned(&a, 32, align);
        if (!CHECK(p != NULL)) break;
        if (!CHECK((uintptr_t)p % align == 0))
            printf("      align=%lu gave %p\n", (unsigned long)align, p);
    }
    kit_arena_free(&a);
}

/* A zero-initialised arena must work with no preparation at all. */
TEST(arena_zero_initialised_grows_on_demand) {
    KitArena a = {0};
    CHECK_INT(kit_arena_capacity(&a), 0);

    char *p = kit_arena_alloc(&a, 10);
    CHECK(p != NULL);
    CHECK(kit_arena_capacity(&a) >= KIT_ARENA_REGION_SIZE);
    kit_arena_free(&a);
}

/* The old arena aborted when full. It must now chain another region, and a
 * single allocation larger than the region size must still be served. */
TEST(arena_chains_regions_instead_of_failing) {
    KitArena a = kit_arena_make(1024);
    CHECK_INT(kit_arena_capacity(&a), 1024);

    char *blocks[64];
    for (int i = 0; i < 64; i++) {
        blocks[i] = kit_arena_alloc(&a, 100);       /* 6400 bytes into 1024 */
        if (!CHECK(blocks[i] != NULL)) break;
        memset(blocks[i], 'a' + (i % 26), 100);
    }
    CHECK(kit_arena_capacity(&a) > 1024);

    /* Earlier blocks must survive the chain growing. */
    int corrupted = 0;
    for (int i = 0; i < 64; i++)
        for (int j = 0; j < 100; j++)
            if (blocks[i][j] != 'a' + (i % 26)) corrupted++;
    CHECK_INT(corrupted, 0);

    char *huge = kit_arena_alloc(&a, 100000);       /* bigger than the hint */
    if (CHECK(huge != NULL)) {
        memset(huge, 1, 100000);
        CHECK_INT(huge[99999], 1);
    }
    kit_arena_free(&a);
}

TEST(arena_reset_reuses_the_regions) {
    KitArena a = kit_arena_make(1024);
    char *first = kit_arena_alloc(&a, 64);
    memset(first, 'x', 64);
    for (int i = 0; i < 100; i++) kit_arena_alloc(&a, 100);   /* force a chain */
    size_t capacity = kit_arena_capacity(&a);

    kit_arena_reset(&a);
    CHECK_INT(kit_arena_used(&a), 0);
    CHECK_INT(kit_arena_capacity(&a), capacity);    /* regions kept, not freed */

    char *again = kit_arena_alloc(&a, 64);
    CHECK(again == first);                      /* same block handed out */
    CHECK_INT(again[0], 0);                     /* and re-zeroed */
    kit_arena_free(&a);
}

TEST(arena_mark_and_rewind) {
    KitArena a = kit_arena_make(256);

    char *keep = kit_arena_strdup(&a, "kept");
    KitArenaMark mark = kit_arena_mark(&a);
    size_t used_at_mark = kit_arena_used(&a);

    for (int i = 0; i < 50; i++) kit_arena_printf(&a, "scratch %d", i);
    CHECK(kit_arena_used(&a) > used_at_mark);

    kit_arena_rewind(&a, mark);
    CHECK_INT(kit_arena_used(&a), used_at_mark);
    CHECK_STR(keep, "kept");                    /* untouched by the rewind */

    /* The reclaimed space is handed out again. */
    char *reused = kit_arena_alloc(&a, 8);
    CHECK(kit_arena_used(&a) <= used_at_mark + 8 + KIT_MAX_ALIGN);
    CHECK(reused != NULL);

    /* A mark taken from an empty arena rewinds everything. */
    KitArena b = {0};
    KitArenaMark empty = kit_arena_mark(&b);
    kit_arena_alloc(&b, 100);
    kit_arena_rewind(&b, empty);
    CHECK_INT(kit_arena_used(&b), 0);

    kit_arena_free(&a);
    kit_arena_free(&b);
}

TEST(arena_string_helpers) {
    KitArena a = {0};
    CHECK_STR(kit_arena_strdup(&a, "hello"), "hello");
    CHECK_STR(kit_arena_strdup(&a, ""), "");
    CHECK_STR(kit_arena_strndup(&a, "truncated", 4), "trun");
    CHECK_STR(kit_arena_printf(&a, "%s-%d-%.2f", "x", 42, 1.5), "x-42-1.50");

    /* Longer than a region, to exercise the growth path. */
    char *big = kit_arena_printf(&a, "%0*d", 100000, 7);
    CHECK_INT(strlen(big), 100000);
    kit_arena_free(&a);
}

/* --- temporary allocator -------------------------------------------------- */

TEST(scratch_allocator_basics) {
    kit_scratch_reset();

    char *a = kit_scratch_printf("%s/%s.o", "build", "main");
    char *b = kit_scratch_strdup("literal");
    int  *n = kit_scratch_alloc(sizeof(int) * 4);

    CHECK_STR(a, "build/main.o");
    CHECK_STR(b, "literal");
    if (CHECK(n != NULL)) for (int i = 0; i < 4; i++) CHECK_INT(n[i], 0);

    /* Distinct calls must not overlap. */
    CHECK(a != b);
    CHECK_STR(a, "build/main.o");
    kit_scratch_reset();
}

TEST(scratch_reset_reclaims_everything) {
    kit_scratch_reset();
    for (int i = 0; i < 10000; i++) kit_scratch_printf("path/to/file-%d.c", i);

    KitArenaMark before = kit_scratch_mark();
    CHECK(before.region != NULL);

    kit_scratch_reset();
    char *after = kit_scratch_strdup("x");
    CHECK(after != NULL);
    CHECK_STR(after, "x");
    kit_scratch_reset();
}

TEST(scratch_mark_and_rewind_nest) {
    kit_scratch_reset();
    char *outer = kit_scratch_strdup("outer");

    KitArenaMark m1 = kit_scratch_mark();
    char *inner = kit_scratch_printf("inner %d", 1);
    CHECK_STR(inner, "inner 1");

    KitArenaMark m2 = kit_scratch_mark();
    for (int i = 0; i < 1000; i++) kit_scratch_printf("deep %d", i);
    kit_scratch_rewind(m2);
    CHECK_STR(inner, "inner 1");                /* the inner scope survives */
    CHECK_STR(outer, "outer");

    kit_scratch_rewind(m1);
    CHECK_STR(outer, "outer");                  /* and so does the outer one */

    kit_scratch_reset();
    kit_scratch_free();                                /* releasing must be safe */
    CHECK_STR(kit_scratch_strdup("after free"), "after free");
    kit_scratch_reset();
}

/* Each thread must get its own regions: no pointer handed out to one thread
 * may land inside another's, and one thread's allocations must survive all the
 * others hammering theirs.
 *
 * The comparison has to happen while every range is still live. Once a thread
 * calls kit_scratch_free its addresses go back to malloc, which will hand the same
 * ones to the next thread, and an overlap then proves nothing. Hence the
 * barrier: allocate, wait for everyone, compare, only then release. */
#ifndef _WIN32
#include <pthread.h>

#define TL_THREADS 8
#define TL_STRINGS 64

/* A gate rather than a barrier: the workers announce they are done allocating
 * and then hold, the main thread compares while nothing has been released,
 * and only then lets them go. pthread_barrier_t would do, were it not absent
 * on macOS. */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  cv;
    int             arrived;
    bool            released;
} TlGate;

typedef struct {
    TlGate   *gate;
    uintptr_t low, high;     /* the address range this thread was handed */
    int       id;
    bool      content_ok;
} TlResult;

static void *tl_worker(void *arg) {
    TlResult *r = arg;
    r->content_ok = true;
    r->low  = UINTPTR_MAX;
    r->high = 0;

    for (int i = 0; i < TL_STRINGS; i++) {
        char     *p = kit_scratch_printf("thread-%d-item-%d", r->id, i);
        uintptr_t a = (uintptr_t)p;
        if (a < r->low)              r->low  = a;
        if (a + strlen(p) > r->high) r->high = a + strlen(p);

        char expected[64];
        snprintf(expected, sizeof(expected), "thread-%d-item-%d", r->id, i);
        if (strcmp(p, expected) != 0) r->content_ok = false;
    }

    pthread_mutex_lock(&r->gate->lock);
    r->gate->arrived++;
    pthread_cond_broadcast(&r->gate->cv);
    while (!r->gate->released) pthread_cond_wait(&r->gate->cv, &r->gate->lock);
    pthread_mutex_unlock(&r->gate->lock);

    kit_scratch_free();   /* a worker releases its own regions */
    return NULL;
}

TEST(scratch_allocator_is_per_thread) {
    static TlResult results[TL_THREADS];
    pthread_t       threads[TL_THREADS];
    TlGate          gate = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER,
                             0, false };

    for (int i = 0; i < TL_THREADS; i++) {
        results[i].id   = i;
        results[i].gate = &gate;
        CHECK_INT(pthread_create(&threads[i], NULL, tl_worker, &results[i]), 0);
    }

    /* Every range is live and none is freed while this holds. */
    pthread_mutex_lock(&gate.lock);
    while (gate.arrived < TL_THREADS) pthread_cond_wait(&gate.cv, &gate.lock);

    int overlaps = 0;
    for (int i = 0; i < TL_THREADS; i++)
        for (int j = i + 1; j < TL_THREADS; j++)
            if (results[i].low <= results[j].high && results[j].low <= results[i].high)
                overlaps++;

    gate.released = true;
    pthread_cond_broadcast(&gate.cv);
    pthread_mutex_unlock(&gate.lock);

    for (int i = 0; i < TL_THREADS; i++) pthread_join(threads[i], NULL);

    if (!CHECK_INT(overlaps, 0))
        printf("      two threads were handed the same scratch memory\n");
    for (int i = 0; i < TL_THREADS; i++) CHECK(results[i].content_ok);
}
#endif /* !_WIN32 */

/* --- hash map ------------------------------------------------------------- */

TEST(map_set_get_delete) {
    KitMap hm = {0};
    int a = 1, b = 2;

    CHECK(kit_map_set(&hm, "alpha", &a));      /* true: new key */
    CHECK(kit_map_set(&hm, "beta", &b));
    CHECK(!kit_map_set(&hm, "alpha", &b));     /* false: update */
    CHECK_INT(hm.count, 2);

    CHECK(kit_map_get(&hm, "alpha") == &b);
    CHECK(kit_map_get(&hm, "beta") == &b);
    CHECK(kit_map_get(&hm, "absent") == NULL);
    CHECK(kit_map_has(&hm, "alpha"));
    CHECK(!kit_map_has(&hm, "absent"));

    CHECK(kit_map_delete(&hm, "alpha"));
    CHECK(!kit_map_delete(&hm, "alpha"));      /* already gone */
    CHECK_INT(hm.count, 1);
    CHECK(!kit_map_has(&hm, "alpha"));
    CHECK(kit_map_has(&hm, "beta"));           /* the tombstone must not hide it */

    kit_map_free(&hm);
    CHECK_INT(hm.capacity, 0);
}

TEST(map_on_an_empty_map_is_safe) {
    KitMap hm = {0};
    CHECK(kit_map_get(&hm, "x") == NULL);
    CHECK(!kit_map_has(&hm, "x"));
    CHECK(!kit_map_delete(&hm, "x"));
    kit_map_free(&hm);
}

TEST(map_stores_null_values) {
    KitMap hm = {0};
    kit_map_set(&hm, "key", NULL);
    CHECK(kit_map_get(&hm, "key") == NULL);
    CHECK(kit_map_has(&hm, "key"));            /* presence is not value != NULL */
    kit_map_free(&hm);
}

TEST(map_grows_and_keeps_every_entry) {
    enum { N = 10000 };
    static char keys[N][16];
    KitMap hm = {0};

    for (int i = 0; i < N; i++) {
        snprintf(keys[i], sizeof(keys[i]), "key%d", i);
        kit_map_set(&hm, keys[i], (void *)(intptr_t)(i + 1));
    }
    CHECK_INT(hm.count, N);

    int wrong = 0;
    for (int i = 0; i < N; i++)
        if (kit_map_get(&hm, keys[i]) != (void *)(intptr_t)(i + 1)) wrong++;
    CHECK_INT(wrong, 0);

    int visited = 0;
    kit_map_each(&hm, e) { CHECK(e->key != NULL); visited++; }
    CHECK_INT(visited, N);

    kit_map_free(&hm);
}

TEST(map_reset_keeps_the_allocation) {
    KitMap hm = {0};
    int v = 1;
    kit_map_set(&hm, "a", &v);
    size_t cap = hm.capacity;

    kit_map_reset(&hm);
    CHECK_INT(hm.count, 0);
    CHECK_INT(hm.capacity, cap);
    CHECK(!kit_map_has(&hm, "a"));

    kit_map_set(&hm, "b", &v);
    CHECK(kit_map_has(&hm, "b"));
    kit_map_free(&hm);
}

/* The three states a slot can be in, told apart by the one predicate a caller
 * walking the table by hand has. kit_map_each is that walk, so the two must
 * agree on what is there: a deleted key leaves the slot occupied, and only
 * the predicate separates that trace from a key that is still live. */
TEST(map_entry_live_separates_the_slot_states) {
    KitMap hm = {0};
    int a = 1, b = 2;
    kit_map_set(&hm, "alpha", &a);
    kit_map_set(&hm, "beta", &b);
    CHECK(kit_map_delete(&hm, "alpha"));

    size_t live = 0, empty = 0, tombstones = 0;
    for (size_t i = 0; i < hm.capacity; i++) {
        const KitMapEntry *e = &hm.entries[i];
        if (kit_map_entry_live(e))    live++;
        else if (e->key == NULL)      empty++;
        else                          tombstones++;
    }
    CHECK_INT(live, 1);
    CHECK_INT(tombstones, 1);                  /* the delete still holds a slot */
    CHECK_INT(live, hm.count);
    CHECK_INT(live + tombstones, hm.used);     /* `used` counts both */
    CHECK_INT(live + tombstones + empty, hm.capacity);

    size_t visited = 0;
    kit_map_each(&hm, e) { CHECK_STR(e->key, "beta"); visited++; }
    CHECK_INT(visited, live);

    kit_map_free(&hm);
}

/* Regression: the load factor counted live entries only, so a set/delete
 * workload filled the table with tombstones that were never reclaimed. Probe
 * sequences then grew until every lookup scanned the whole table.
 *
 * A sliding window keeps a constant population, so the capacity must stay
 * bounded and the run must stay fast. Before the fix this took ~550 ms. */
TEST(map_sliding_window_does_not_degrade) {
    enum { LIVE = 20000, TOTAL = 200000 };
    static char keys[TOTAL][16];
    for (int i = 0; i < TOTAL; i++) snprintf(keys[i], sizeof(keys[i]), "key%d", i);

    KitMap hm = {0};
    for (int i = 0; i < LIVE; i++) kit_map_set(&hm, keys[i], (void *)(intptr_t)1);

    size_t cap_when_full = hm.capacity;
    KitTimer sw = kit_timer_start();
    for (int i = LIVE; i < TOTAL; i++) {
        kit_map_set(&hm, keys[i], (void *)(intptr_t)1);
        kit_map_delete(&hm, keys[i - LIVE]);
    }
    double ms = kit_timer_ms(sw);

    CHECK_INT(hm.count, LIVE);
    CHECK_INT(hm.capacity, cap_when_full);   /* rehashed in place, never grown */
    CHECK(hm.used <= hm.capacity);
    CHECK(kit_map_has(&hm, keys[TOTAL - 1]));
    CHECK(!kit_map_has(&hm, keys[0]));

    /* Generous bound: the point is the order of magnitude, not the machine.
     * The unfixed version needed ~12 s here, the fixed one ~80 ms. */
    double budget = UTEST_SANITIZED ? 3000.0 : 1000.0;
    if (!CHECK(ms < budget))
        printf("      sliding window took %.0f ms (budget %.0f)\n", ms, budget);

    kit_map_free(&hm);
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("memory");
    RUN(arena_allocates_aligned_zeroed_blocks);
    RUN(arena_honours_over_alignment);
    RUN(arena_zero_initialised_grows_on_demand);
    RUN(arena_chains_regions_instead_of_failing);
    RUN(arena_reset_reuses_the_regions);
    RUN(arena_mark_and_rewind);
    RUN(arena_string_helpers);
    RUN(scratch_allocator_basics);
    RUN(scratch_reset_reclaims_everything);
    RUN(scratch_mark_and_rewind_nest);
#ifndef _WIN32
    RUN(scratch_allocator_is_per_thread);
#endif
    RUN(map_set_get_delete);
    RUN(map_on_an_empty_map_is_safe);
    RUN(map_stores_null_values);
    RUN(map_grows_and_keeps_every_entry);
    RUN(map_reset_keeps_the_allocation);
    RUN(map_entry_live_separates_the_slot_states);
    RUN(map_sliding_window_does_not_degrade);
    return utest_report();
}
