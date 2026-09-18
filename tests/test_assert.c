/*
 * Assertions. The half that matters is the one that fails, and it ends the
 * process, so it cannot be checked from inside it: the suite re-runs its own
 * binary with a case name, the child asserts and dies, and the parent reads
 * what the child printed on its way out.
 *
 * That also makes the message itself the thing under test. An assertion whose
 * output does not name the values is an assertion that sends its reader to a
 * debugger, which is the whole point of the typed printing.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

static const char *self = NULL;   /* this binary, to re-run as the child */

/* --- the child ------------------------------------------------------------ */

static void die(const char *which) {
    if (strcmp(which, "plain") == 0) {
        int n = 3;
        KIT_ASSERT(n > 5);
    } else if (strcmp(which, "msg") == 0) {
        size_t writes = 42;
        bool   intact = false;
        KIT_ASSERT_MSG(intact, "ring corrupted after %zu writes", writes);
    } else if (strcmp(which, "cmp_size") == 0) {
        size_t used = 5000, capacity = 4096;
        KIT_ASSERT_CMP(used, <=, capacity);
    } else if (strcmp(which, "cmp_negative") == 0) {
        int balance = -7;
        KIT_ASSERT_CMP(balance, >=, 0);
    } else if (strcmp(which, "cmp_double") == 0) {
        double ratio = 1.5, limit = 1.0;
        KIT_ASSERT_CMP(ratio, <, limit);
    } else if (strcmp(which, "cmp_char") == 0) {
        char sep = 'a', want = '/';
        KIT_ASSERT_CMP(sep, ==, want);
    } else if (strcmp(which, "cmp_char_literal") == 0) {
        char sep = 'a';
        KIT_ASSERT_CMP(sep, ==, '/');
    } else if (strcmp(which, "cmp_pointer") == 0) {
        int  storage = 0;
        int *slot    = &storage;
        KIT_ASSERT_CMP(slot, ==, (int *)NULL);
    } else if (strcmp(which, "str") == 0) {
        const char *chosen = "config.ini";
        KIT_ASSERT_STR_EQ(chosen, "config.toml");
    } else if (strcmp(which, "str_null") == 0) {
        const char *missing = NULL;
        KIT_ASSERT_STR_EQ(missing, "expected");
    } else if (strcmp(which, "str_long") == 0) {
        const char *line = "a value far longer than the room an assertion "
                           "keeps for printing one of them";
        KIT_ASSERT_STR_EQ(line, "short");
    }
    printf("the assertion did not fire\n");
}

/* --- the parent ----------------------------------------------------------- */

/* Runs one case in a child and returns everything it wrote to stderr. The
 * child always dies, so the failure the capture reports is the expected one
 * and the output is what is being examined. */
static bool crash(const char *which, KitBuf *out) {
    KitCommand cmd = KIT_ZEROED;
    kit_command_push_all(&cmd, self, "--die", which, NULL);
    bool ran = kit_command_capture_split(&cmd, NULL, out, NULL);
    kit_command_free(&cmd);
    return !ran && out->count > 0;
}

static bool says(KitBuf *out, const char *fragment) {
    return out->items && strstr(kit_buf_cstr(out), fragment) != NULL;
}

/* The two values a comparison printed. Used where what matters is that both
 * sides were rendered and are not the same, rather than how: "%p" is spelled
 * "0x7ffd..." by glibc and "000000000014FE44" by msvcrt, and neither is the
 * header's business. */
static bool sides_differ(KitBuf *out) {
    const char *text  = out->items ? kit_buf_cstr(out) : "";
    const char *left  = strstr(text, "left:  ");
    const char *right = strstr(text, "right: ");
    if (!left || !right) return false;

    left  += 7;
    right += 7;
    size_t ln = strcspn(left, "\r\n"), rn = strcspn(right, "\r\n");
    return ln > 0 && rn > 0 && (ln != rn || strncmp(left, right, ln) != 0);
}

/* Every case asserts on the same buffer, so each one clears it first. */
#define CRASHES(which)                                       \
    (kit_buf_reset(&out), CHECK(crash((which), &out)))

#define SAYS(fragment) \
    CHECK(says(&out, (fragment)) || utest__fail(__FILE__, __LINE__, \
          "missing \"%s\" in:\n%s", (fragment), kit_buf_cstr(&out)))

TEST(a_true_assertion_does_nothing) {
    int    n     = 7;
    size_t used  = 3, capacity = 8;
    const char *name = "kit";

    KIT_ASSERT(n > 5);
    KIT_ASSERT_MSG(n > 5, "n was %d", n);
    KIT_ASSERT_CMP(used, <=, capacity);
    KIT_ASSERT_STR_EQ(name, "kit");
    KIT_ASSERT_STR_EQ(NULL, NULL);

    CHECK(true);   /* reaching here is the result */
}

TEST(a_failing_assertion_aborts_and_names_the_expression) {
    KitBuf out = KIT_ZEROED;

    CRASHES("plain");
    SAYS("assertion failed: n > 5");
    SAYS("Aborting...");

    kit_buf_free(&out);
}

TEST(the_message_form_adds_the_state_behind_the_failure) {
    KitBuf out = KIT_ZEROED;

    CRASHES("msg");
    SAYS("assertion failed: intact: ring corrupted after 42 writes");

    kit_buf_free(&out);
}

TEST(a_comparison_prints_both_sides_with_their_own_type) {
    KitBuf out = KIT_ZEROED;

    CRASHES("cmp_size");
    SAYS("assertion failed: used <= capacity");
    SAYS("left:  5000");
    SAYS("right: 4096");

    CRASHES("cmp_negative");
    SAYS("left:  -7");            /* signed, not a huge unsigned */

    CRASHES("cmp_double");
    SAYS("left:  1.5");           /* not truncated to an integer */

    CRASHES("cmp_char");
    SAYS("left:  'a'");           /* the character, not its code */
    SAYS("right: '/'");

    /* A character constant has type int in C, so it prints as the number it
     * is. Nothing can tell the two apart from inside the macro, and the
     * header says so rather than pretending otherwise. */
    CRASHES("cmp_char_literal");
    SAYS("right: 47");

    CRASHES("cmp_pointer");
    SAYS("assertion failed: slot == (int *)NULL");
    CHECK(sides_differ(&out));    /* both rendered as addresses */

    kit_buf_free(&out);
}

TEST(string_equality_prints_the_strings) {
    KitBuf out = KIT_ZEROED;

    CRASHES("str");
    SAYS("assertion failed: chosen equals \"config.toml\"");
    SAYS("left:  \"config.ini\"");
    SAYS("right: \"config.toml\"");

    CRASHES("str_null");
    SAYS("left:  NULL");          /* named, not dereferenced */

    CRASHES("str_long");
    SAYS("...");                  /* cut, and visibly so */

    kit_buf_free(&out);
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--die") == 0) {
        die(argv[2]);
        return 0;
    }

    self = argv[0];
    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("assert");
    RUN(a_true_assertion_does_nothing);
    RUN(a_failing_assertion_aborts_and_names_the_expression);
    RUN(the_message_form_adds_the_state_behind_the_failure);
    RUN(a_comparison_prints_both_sides_with_their_own_type);
    RUN(string_equality_prints_the_strings);
    return utest_report();
}
