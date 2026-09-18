/*
 * The heap. The property under test is the only one it has: whatever the
 * order things went in, the one a sort would put first is the one that comes
 * out, every time, until there is nothing left.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

typedef struct { int *items; size_t count, capacity; } Ints;

typedef struct {
    const char *name;
    int         size;
} File;

typedef struct { File *items; size_t count, capacity; } Files;

static int ascending(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static int descending(const void *a, const void *b) {
    return -ascending(a, b);
}

static int by_size(const void *a, const void *b) {
    const File *x = (const File *)a, *y = (const File *)b;
    return x->size < y->size ? -1 : x->size > y->size ? 1 : 0;
}

TEST(what_goes_in_comes_out_in_order) {
    Ints heap  = KIT_ZEROED;
    int  input[] = { 9, 4, 7, 1, 8, 2, 2, 6 };

    for (size_t i = 0; i < KIT_COUNTOF(input); i++)
        kit_heap_push(&heap, input[i], ascending);

    CHECK_INT(heap.count, KIT_COUNTOF(input));

    int previous = INT_MIN;
    for (size_t i = 0; i < KIT_COUNTOF(input); i++) {
        int next = kit_heap_pop(&heap, ascending);
        if (!CHECK(next >= previous)) break;
        previous = next;
    }
    CHECK_INT(heap.count, 0);

    kit_array_free(&heap);
}

/* The comparator is the only thing that decides the order, so the same
 * numbers with the opposite one come out the other way round. */
TEST(the_comparator_decides_which_end_comes_first) {
    Ints heap = KIT_ZEROED;
    for (int i = 1; i <= 20; i++) kit_heap_push(&heap, i, descending);

    CHECK_INT(kit_heap_pop(&heap, descending), 20);
    CHECK_INT(kit_heap_pop(&heap, descending), 19);
    CHECK_INT(kit_heap_pop(&heap, descending), 18);

    kit_array_free(&heap);
}

TEST(the_next_element_can_be_read_without_taking_it) {
    Ints heap = KIT_ZEROED;
    kit_heap_push(&heap, 5, ascending);
    kit_heap_push(&heap, 3, ascending);
    kit_heap_push(&heap, 8, ascending);

    CHECK_INT(heap.items[0], 3);
    CHECK_INT(heap.count, 3);          /* looking took nothing */
    CHECK_INT(kit_heap_pop(&heap, ascending), 3);
    CHECK_INT(heap.items[0], 5);

    kit_array_free(&heap);
}

TEST(one_element_and_two_elements_behave) {
    Ints heap = KIT_ZEROED;

    kit_heap_push(&heap, 42, ascending);
    CHECK_INT(kit_heap_pop(&heap, ascending), 42);
    CHECK_INT(heap.count, 0);

    kit_heap_push(&heap, 2, ascending);
    kit_heap_push(&heap, 1, ascending);
    CHECK_INT(kit_heap_pop(&heap, ascending), 1);
    CHECK_INT(kit_heap_pop(&heap, ascending), 2);
    CHECK_INT(heap.count, 0);

    kit_array_free(&heap);
}

/* Elements wider than a machine word, to catch a swap that moves the wrong
 * number of bytes. */
TEST(elements_can_be_structs) {
    Files heap = KIT_ZEROED;
    File  input[] = { { "small", 10 }, { "huge", 9000 }, { "medium", 500 } };

    for (size_t i = 0; i < KIT_COUNTOF(input); i++)
        kit_heap_push(&heap, input[i], by_size);

    File first = kit_heap_pop(&heap, by_size);
    CHECK_STR(first.name, "small");
    CHECK_INT(first.size, 10);

    File second = kit_heap_pop(&heap, by_size);
    CHECK_STR(second.name, "medium");
    CHECK_INT(second.size, 500);

    kit_array_free(&heap);
}

/* Pushing and popping in no particular order has to keep the property, which
 * a heap that only ever fills and then empties would not prove. */
TEST(interleaved_pushes_and_pops_keep_the_order) {
    Ints      heap = KIT_ZEROED;
    KitRandom rng  = kit_random_seed(11);
    int       live = 0;

    for (int round = 0; round < 2000; round++) {
        if (live == 0 || kit_random_double(&rng) < 0.6) {
            kit_heap_push(&heap, (int)kit_random_between(&rng, -1000, 1000), ascending);
            live++;
        } else {
            int smallest = heap.items[0];
            int taken    = kit_heap_pop(&heap, ascending);
            live--;
            if (!CHECK(taken == smallest)) break;

            /* And it really was the smallest of everything still there. */
            bool holds = true;
            for (size_t i = 0; i < heap.count; i++)
                if (heap.items[i] < taken) holds = false;
            if (!CHECK(holds)) break;
        }
    }
    CHECK_INT(heap.count, (size_t)live);

    kit_array_free(&heap);
}

/* Keeping the N biggest of a long run without sorting it: a heap ordered the
 * other way round, whose smallest is dropped whenever something bigger than
 * it turns up. */
TEST(a_heap_of_n_keeps_the_n_largest) {
    Ints      best = KIT_ZEROED;
    KitRandom rng  = kit_random_seed(3);
    const size_t want = 5;
    int       seen_max = INT_MIN;

    for (int i = 0; i < 5000; i++) {
        int value = (int)kit_random_between(&rng, 0, 1000000);
        if (value > seen_max) seen_max = value;

        if (best.count < want) {
            kit_heap_push(&best, value, ascending);
        } else if (value > best.items[0]) {
            kit_heap_pop(&best, ascending);
            kit_heap_push(&best, value, ascending);
        }
    }

    CHECK_INT(best.count, want);

    kit_array_sort(&best, descending);
    CHECK_INT(kit_array_first(&best), seen_max);
    for (size_t i = 1; i < best.count; i++)
        CHECK(best.items[i - 1] >= best.items[i]);

    kit_array_free(&best);
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("heap");
    RUN(what_goes_in_comes_out_in_order);
    RUN(the_comparator_decides_which_end_comes_first);
    RUN(the_next_element_can_be_read_without_taking_it);
    RUN(one_element_and_two_elements_behave);
    RUN(elements_can_be_structs);
    RUN(interleaved_pushes_and_pops_keep_the_order);
    RUN(a_heap_of_n_keeps_the_n_largest);
    return utest_report();
}
