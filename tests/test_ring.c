/*
 * The ring buffer. What matters is the wrap: the entries that survive it, the
 * order they come back in, and the fact that a ring that has wrapped many
 * times is indistinguishable from one that has just filled.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

typedef struct {
    int    items[4];
    size_t capacity, head, count;
} Ring;

typedef struct {
    const char *items[3];
    size_t      capacity, head, count;
} Names;

TEST(a_ring_fills_before_it_wraps) {
    Ring r = KIT_ZEROED;
    kit_ring_init(&r);

    CHECK_INT(r.capacity, 4);
    CHECK_INT(r.count, 0);

    kit_ring_push(&r, 1);
    kit_ring_push(&r, 2);
    CHECK_INT(r.count, 2);
    CHECK_INT(kit_ring_at(&r, 0), 1);
    CHECK_INT(kit_ring_at(&r, 1), 2);

    kit_ring_push(&r, 3);
    kit_ring_push(&r, 4);
    CHECK_INT(r.count, 4);
    CHECK_INT(kit_ring_at(&r, 3), 4);
}

TEST(pushing_into_a_full_ring_drops_the_oldest) {
    Ring r = KIT_ZEROED;
    kit_ring_init(&r);

    for (int i = 1; i <= 6; i++) kit_ring_push(&r, i);

    /* Six went in, the last four are what is left. */
    CHECK_INT(r.count, 4);
    CHECK_INT(kit_ring_at(&r, 0), 3);
    CHECK_INT(kit_ring_at(&r, 1), 4);
    CHECK_INT(kit_ring_at(&r, 2), 5);
    CHECK_INT(kit_ring_at(&r, 3), 6);
}

/* The state after many wraps has to be the state after one, or the modulo is
 * drifting somewhere. */
TEST(a_ring_that_has_wrapped_often_is_still_just_a_ring) {
    Ring r = KIT_ZEROED;
    kit_ring_init(&r);

    for (int i = 0; i < 1000; i++) kit_ring_push(&r, i);

    CHECK_INT(r.count, 4);
    CHECK(r.head < r.capacity);
    for (size_t i = 0; i < r.count; i++)
        CHECK_INT(kit_ring_at(&r, i), 996 + (int)i);
}

TEST(each_walks_from_the_oldest_to_the_newest) {
    Ring r = KIT_ZEROED;
    kit_ring_init(&r);
    for (int i = 1; i <= 7; i++) kit_ring_push(&r, i);

    int  seen[8] = KIT_ZEROED;
    size_t n = 0;
    kit_ring_each(&r, i) seen[n++] = kit_ring_at(&r, i);

    CHECK_INT(n, 4);
    CHECK_INT(seen[0], 4);
    CHECK_INT(seen[3], 7);

    /* Two rings walked at once, each with its own index. */
    Names names = KIT_ZEROED;
    kit_ring_init(&names);
    kit_ring_push(&names, "a");
    kit_ring_push(&names, "b");

    size_t pairs = 0;
    kit_ring_each(&r, i) {
        kit_ring_each(&names, j) {
            if (kit_ring_at(&r, i) > 0 && kit_ring_at(&names, j) != NULL) pairs++;
        }
    }
    CHECK_INT(pairs, 8);
}

TEST(pop_takes_the_oldest_and_leaves_the_rest) {
    Ring r = KIT_ZEROED;
    kit_ring_init(&r);
    for (int i = 1; i <= 5; i++) kit_ring_push(&r, i);   /* 2 3 4 5 */

    CHECK_INT(kit_ring_pop(&r), 2);
    CHECK_INT(r.count, 3);
    CHECK_INT(kit_ring_at(&r, 0), 3);

    CHECK_INT(kit_ring_pop(&r), 3);
    CHECK_INT(kit_ring_pop(&r), 4);
    CHECK_INT(kit_ring_pop(&r), 5);
    CHECK_INT(r.count, 0);

    /* Empty, and usable again from there. */
    kit_ring_push(&r, 99);
    CHECK_INT(r.count, 1);
    CHECK_INT(kit_ring_pop(&r), 99);
}

TEST(popping_and_pushing_can_alternate_for_ever) {
    Ring r = KIT_ZEROED;
    kit_ring_init(&r);
    kit_ring_push(&r, 1);
    kit_ring_push(&r, 2);

    for (int i = 3; i < 200; i++) {
        CHECK_INT(kit_ring_pop(&r), i - 2);
        kit_ring_push(&r, i);
        if (r.count != 2) { CHECK_INT(r.count, 2); return; }
    }
    CHECK_INT(r.count, 2);
}

/* Storage the caller owns, with the capacity set by hand: the same macros,
 * with items a pointer rather than an array. */
TEST(a_ring_over_borrowed_storage_behaves_the_same) {
    int slots[3];
    struct { int *items; size_t capacity, head, count; } r = KIT_ZEROED;
    r.items    = slots;
    r.capacity = KIT_COUNTOF(slots);

    for (int i = 1; i <= 4; i++) kit_ring_push(&r, i);

    CHECK_INT(r.count, 3);
    CHECK_INT(kit_ring_at(&r, 0), 2);
    CHECK_INT(kit_ring_at(&r, 2), 4);
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("ring");
    RUN(a_ring_fills_before_it_wraps);
    RUN(pushing_into_a_full_ring_drops_the_oldest);
    RUN(a_ring_that_has_wrapped_often_is_still_just_a_ring);
    RUN(each_walks_from_the_oldest_to_the_newest);
    RUN(pop_takes_the_oldest_and_leaves_the_rest);
    RUN(popping_and_pushing_can_alternate_for_ever);
    RUN(a_ring_over_borrowed_storage_behaves_the_same);
    return utest_report();
}
