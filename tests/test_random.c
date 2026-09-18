/*
 * The generator. What is being checked is what the header promises: the same
 * seed gives the same sequence anywhere, the range is uniform over both ends
 * included, and a zeroed generator produces numbers rather than zeros.
 *
 * Distribution is checked with counts rather than with a statistical test: a
 * suite that fails once a month on a fair generator is a suite nobody trusts,
 * so the tolerances are wide enough to catch a broken range and nothing else.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

TEST(the_same_seed_gives_the_same_sequence) {
    KitRandom a = kit_random_seed(1234);
    KitRandom b = kit_random_seed(1234);
    KitRandom c = kit_random_seed(1235);

    bool same = true, differs = false;
    for (int i = 0; i < 64; i++) {
        uint64_t x = kit_random_u64(&a);
        if (x != kit_random_u64(&b)) same = false;
        if (x != kit_random_u64(&c)) differs = true;
    }

    CHECK(same);
    CHECK(differs);
}

/* Seeding the four words from a counter would make neighbouring seeds start
 * with neighbouring output. This is what splitmix64 is there to prevent. */
TEST(neighbouring_seeds_do_not_start_alike) {
    KitRandom a = kit_random_seed(1);
    KitRandom b = kit_random_seed(2);

    uint64_t x = kit_random_u64(&a), y = kit_random_u64(&b);
    uint64_t diff = x ^ y;

    int bits = 0;
    for (int i = 0; i < 64; i++) bits += (int)((diff >> i) & 1u);

    /* Two unrelated values differ in about half their bits. */
    CHECK(bits > 12);
}

/* The header promises the sequence is the same on every platform, so it is
 * written down: a change to the generator has to be a deliberate one. */
TEST(the_sequence_is_the_one_it_has_always_been) {
    KitRandom r = kit_random_seed(42);

    CHECK(kit_random_u64(&r) == 1546998764402558742ull);
    CHECK(kit_random_u64(&r) == 6990951692964543102ull);
    CHECK(kit_random_u64(&r) == 12544586762248559009ull);
}

TEST(a_zeroed_generator_is_the_default_sequence) {
    KitRandom zeroed = KIT_ZEROED;

    uint64_t first  = kit_random_u64(&zeroed);
    uint64_t second = kit_random_u64(&zeroed);

    CHECK(first != 0);
    CHECK(second != 0);
    CHECK(first != second);

    /* And it is a sequence, not a one-off repair: a second zeroed generator
     * follows the same one. */
    KitRandom other = KIT_ZEROED;
    CHECK(kit_random_u64(&other) == first);
}

TEST(doubles_stay_inside_their_interval) {
    KitRandom r    = kit_random_seed(7);
    double    sum  = 0.0;
    double    low = 1.0, high = 0.0;

    for (int i = 0; i < 20000; i++) {
        double x = kit_random_double(&r);
        if (!CHECK(x >= 0.0 && x < 1.0)) return;
        if (x < low)  low  = x;
        if (x > high) high = x;
        sum += x;
    }

    CHECK_DBL(sum / 20000.0, 0.5, 0.02);
    CHECK(low < 0.01);            /* the whole interval is reachable */
    CHECK(high > 0.99);
}

TEST(between_includes_both_ends_and_nothing_else) {
    KitRandom r = kit_random_seed(99);
    bool saw_low = false, saw_high = false, inside = true;

    for (int i = 0; i < 5000; i++) {
        int64_t x = kit_random_between(&r, -3, 3);
        if (x < -3 || x > 3) inside = false;
        if (x == -3) saw_low  = true;
        if (x == 3)  saw_high = true;
    }

    CHECK(inside);
    CHECK(saw_low);
    CHECK(saw_high);
}

TEST(between_is_uniform_enough_to_roll_a_die) {
    KitRandom r = kit_random_seed(2024);
    int       face[7] = KIT_ZEROED;

    for (int i = 0; i < 60000; i++) {
        int64_t roll = kit_random_between(&r, 1, 6);
        if (!CHECK(roll >= 1 && roll <= 6)) return;
        face[roll]++;
    }

    /* 10000 each on average. A modulo taken straight off a 64-bit draw would
     * pass this too; the point is that a range that is plainly broken, or one
     * that drops an end, does not. */
    for (int f = 1; f <= 6; f++)
        CHECK(face[f] > 9000 && face[f] < 11000);
}

TEST(between_handles_the_edges_of_the_type) {
    KitRandom r = kit_random_seed(5);

    CHECK(kit_random_between(&r, 7, 7) == 7);
    CHECK(kit_random_between(&r, 4, 1) >= 1);      /* bounds either way round */
    CHECK(kit_random_between(&r, 4, 1) <= 4);

    bool in_range = true, varied = false;
    int64_t first = kit_random_between(&r, INT64_MIN, INT64_MIN + 10);
    for (int i = 0; i < 200; i++) {
        int64_t x = kit_random_between(&r, INT64_MIN, INT64_MIN + 10);
        if (x < INT64_MIN || x > INT64_MIN + 10) in_range = false;
        if (x != first) varied = true;
    }
    CHECK(in_range);
    CHECK(varied);

    /* The whole type: the span is one more than a uint64 can hold, which is
     * the case the code spells out. */
    bool spread = false;
    for (int i = 0; i < 200; i++) {
        int64_t x = kit_random_between(&r, INT64_MIN, INT64_MAX);
        if (x < 0) spread = true;
    }
    CHECK(spread);
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("random");
    RUN(the_same_seed_gives_the_same_sequence);
    RUN(neighbouring_seeds_do_not_start_alike);
    RUN(the_sequence_is_the_one_it_has_always_been);
    RUN(a_zeroed_generator_is_the_default_sequence);
    RUN(doubles_stay_inside_their_interval);
    RUN(between_includes_both_ends_and_nothing_else);
    RUN(between_is_uniform_enough_to_roll_a_die);
    RUN(between_handles_the_edges_of_the_type);
    return utest_report();
}
