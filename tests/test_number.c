/*
 * Checked arithmetic. Every case here is a boundary: the largest value that
 * still fits and the first one that does not, on both sides of zero.
 *
 * Built twice by the Makefile, once with the compiler's overflow builtins and
 * once with KIT_NO_OVERFLOW_BUILTINS, so the portable path is exercised on a
 * machine whose compiler would never take it.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

TEST(add_unsigned_stops_at_the_top) {
    unsigned out = 0;

    CHECK(kit_num_add(UINT_MAX - 1u, 1u, &out));
    CHECK_INT(out, UINT_MAX);

    out = 7u;
    CHECK(!kit_num_add(UINT_MAX, 1u, &out));
    CHECK_INT(out, 7u);                    /* untouched on refusal */

    CHECK(!kit_num_add(UINT_MAX, UINT_MAX, &out));
}

TEST(sub_unsigned_refuses_to_go_below_zero) {
    unsigned out = 0;

    CHECK(kit_num_sub(1u, 1u, &out));
    CHECK_INT(out, 0u);

    out = 7u;
    CHECK(!kit_num_sub(0u, 1u, &out));
    CHECK_INT(out, 7u);
}

TEST(mul_unsigned_stops_at_the_top) {
    unsigned out = 0;

    CHECK(kit_num_mul(0u, UINT_MAX, &out));
    CHECK_INT(out, 0u);
    CHECK(kit_num_mul(UINT_MAX / 2u, 2u, &out));
    CHECK_INT(out, UINT_MAX - 1u);

    CHECK(!kit_num_mul(UINT_MAX / 2u + 1u, 2u, &out));
    CHECK(!kit_num_mul(UINT_MAX, UINT_MAX, &out));
}

TEST(add_signed_stops_at_both_ends) {
    int out = 0;

    CHECK(kit_num_add(INT_MAX - 1, 1, &out));
    CHECK_INT(out, INT_MAX);
    CHECK(kit_num_add(INT_MIN + 1, -1, &out));
    CHECK_INT(out, INT_MIN);

    out = 7;
    CHECK(!kit_num_add(INT_MAX, 1, &out));
    CHECK(!kit_num_add(INT_MIN, -1, &out));
    CHECK(!kit_num_add(INT_MAX, INT_MAX, &out));
    CHECK(!kit_num_add(INT_MIN, INT_MIN, &out));
    CHECK_INT(out, 7);

    /* Opposite signs can never overflow, whatever their size. */
    CHECK(kit_num_add(INT_MAX, INT_MIN, &out));
    CHECK_INT(out, -1);
}

TEST(sub_signed_stops_at_both_ends) {
    int out = 0;

    CHECK(kit_num_sub(INT_MAX, -1 + 1, &out));
    CHECK_INT(out, INT_MAX);
    CHECK(kit_num_sub(INT_MIN + 1, 1, &out));
    CHECK_INT(out, INT_MIN);

    CHECK(!kit_num_sub(INT_MAX, -1, &out));
    CHECK(!kit_num_sub(INT_MIN, 1, &out));

    /* The one that looks harmless: negating the most negative value. */
    CHECK(!kit_num_sub(0, INT_MIN, &out));
}

TEST(mul_signed_covers_all_four_sign_pairs) {
    int out = 0;

    CHECK(kit_num_mul(0, INT_MIN, &out));
    CHECK_INT(out, 0);
    CHECK(kit_num_mul(INT_MIN, 0, &out));
    CHECK_INT(out, 0);

    CHECK(kit_num_mul(INT_MAX / 2, 2, &out));       /* + * + */
    CHECK_INT(out, INT_MAX - 1);
    CHECK(!kit_num_mul(INT_MAX / 2 + 1, 2, &out));

    CHECK(kit_num_mul(2, INT_MIN / 2, &out));       /* + * - */
    CHECK_INT(out, INT_MIN);
    CHECK(!kit_num_mul(2, INT_MIN / 2 - 1, &out));

    CHECK(kit_num_mul(INT_MIN / 2, 2, &out));       /* - * + */
    CHECK_INT(out, INT_MIN);
    CHECK(!kit_num_mul(INT_MIN / 2 - 1, 2, &out));

    CHECK(kit_num_mul(-2, -(INT_MAX / 2), &out));   /* - * - */
    CHECK_INT(out, (INT_MAX / 2) * 2);
    CHECK(!kit_num_mul(INT_MIN, -1, &out));         /* the classic one */
    CHECK(!kit_num_mul(-1, INT_MIN, &out));
}

/* The same operation on every width the dispatch covers, so that a type wired
 * to the wrong helper shows up here rather than in a caller. */
TEST(every_covered_type_is_dispatched) {
    long      l  = 0;
    long long ll = 0;
    unsigned long      ul  = 0;
    unsigned long long ull = 0;

    CHECK(kit_num_add(LONG_MAX - 1L, 1L, &l));
    CHECK(!kit_num_add(LONG_MAX, 1L, &l));
    CHECK(kit_num_add(LLONG_MAX - 1LL, 1LL, &ll));
    CHECK(!kit_num_add(LLONG_MAX, 1LL, &ll));

    CHECK(kit_num_add(ULONG_MAX - 1UL, 1UL, &ul));
    CHECK(!kit_num_add(ULONG_MAX, 1UL, &ul));
    CHECK(kit_num_add(ULLONG_MAX - 1ULL, 1ULL, &ull));
    CHECK(!kit_num_add(ULLONG_MAX, 1ULL, &ull));

    CHECK(!kit_num_mul(LLONG_MIN, -1LL, &ll));
    CHECK(!kit_num_sub(0UL, 1UL, &ul));
}

/* size_t and the fixed-width types are aliases of the six, whichever they
 * turn out to be here: the point is that a caller writes the natural type and
 * it resolves. */
TEST(size_t_and_fixed_width_types_resolve) {
    size_t   total = SIZE_MAX - 4u;
    uint64_t big   = 0;
    int64_t  wide  = 0;

    CHECK(kit_num_add(total, (size_t)4u, &total));
    CHECK(total == SIZE_MAX);
    CHECK(!kit_num_add(total, (size_t)1u, &total));
    CHECK(total == SIZE_MAX);

    CHECK(kit_num_mul((uint64_t)1u << 32, (uint64_t)1u << 31, &big));
    CHECK(!kit_num_mul((uint64_t)1u << 32, (uint64_t)1u << 32, &big));

    CHECK(kit_num_sub(INT64_MIN + 1, 1, &wide));
    CHECK(wide == INT64_MIN);
    CHECK(!kit_num_sub(INT64_MIN, 1, &wide));
}

/* A running total is the reason this module exists: it must keep the last
 * value that was true rather than wrap into a smaller one. */
TEST(a_running_total_that_overflows_keeps_its_last_good_value) {
    uint64_t sizes[] = { 1000, UINT64_MAX - 2000, 5000 };
    uint64_t total   = 0;
    size_t   counted = 0;

    for (size_t i = 0; i < KIT_COUNTOF(sizes); i++) {
        if (!kit_num_add(total, sizes[i], &total)) break;
        counted++;
    }

    CHECK_INT(counted, 2);
    CHECK(total == UINT64_MAX - 1000);
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("number");
    RUN(add_unsigned_stops_at_the_top);
    RUN(sub_unsigned_refuses_to_go_below_zero);
    RUN(mul_unsigned_stops_at_the_top);
    RUN(add_signed_stops_at_both_ends);
    RUN(sub_signed_stops_at_both_ends);
    RUN(mul_signed_covers_all_four_sign_pairs);
    RUN(every_covered_type_is_dispatched);
    RUN(size_t_and_fixed_width_types_resolve);
    RUN(a_running_total_that_overflows_keeps_its_last_good_value);
    return utest_report();
}
