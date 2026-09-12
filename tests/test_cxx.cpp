/*
 * The header from C++. Not a second copy of the C suite: this checks that the
 * things C++ is stricter about actually work, since that is what breaks.
 *
 * Compiled as C++17 with the implementation included, so both halves of the
 * header have to be valid C++, not just the declarations.
 */

#define UTILS_IMPLEMENTATION
#include "../utils.h"
#include "utest.h"

#include <string>
#include <vector>

TEST(cxx_struct_literal_macros) {
    /* V2, V3 and SV_LIT are compound literals in C and brace initialisation
     * in C++. They must mean the same thing in both. */
    Vec2 a = V2(3, 4);
    CHECK_DBL(vec2_len(a), 5.0f, 1e-5);
    CHECK_DBL(vec3_len(V3(2, 3, 6)), 7.0f, 1e-5);

    String_View lit = SV_LIT("literal");
    CHECK_INT(lit.count, 7);
    CHECK(sv_eq_cstr(lit, "literal"));
}

TEST(cxx_zeroed_aggregates) {
    /* {0} warns in C++ and {} is not C, so the library offers a spelling that
     * works in both. */
    StringBuilder sb = UTILS_ZEROED;
    sb_append(&sb, "from c++");
    CHECK_STR(sb_cstr(&sb), "from c++");
    sb_free(&sb);

    Cmd c = UTILS_ZEROED;
    cmd_append(&c, "true");
    CHECK_INT(c.count, 1);
    cmd_free(&c);
}

TEST(cxx_dynamic_array_macros) {
    /* da_reserve assigns the result of realloc to a typed member, which C++
     * will not do implicitly. */
    struct IntArray { int *items; size_t count, capacity; };
    IntArray a = UTILS_ZEROED;

    for (int i = 0; i < 500; i++) da_append(&a, i * 2);
    CHECK_INT(a.count, 500);
    CHECK_INT(a.items[499], 998);

    size_t at;
    da_index_of(&a, 42, at);
    CHECK_INT(at, 21);

    long sum = 0;
    da_foreach(int, it, &a) sum += *it;
    CHECK_INT(sum, 499L * 500);
    da_free(&a);
}

TEST(cxx_needs_rebuild1_is_a_function_now) {
    /* It used to be a macro over a compound literal, which C++ has no form of. */
    const char *path = "utest-tmp-cxx.txt";
    write_file(path, "x", 1);
    CHECK_INT(needs_rebuild1(path, path), 1);      /* itself, so not older */
    CHECK_INT(needs_rebuild1("no/such/output", path), 1);
    remove_file(path);
}

TEST(cxx_interop_with_the_standard_library) {
    /* The realistic use: C++ code holding std types, calling into the C API. */
    std::string     text  = "alpha,beta,gamma";
    std::vector<std::string> fields;

    String_View rest = sv_from_parts(text.data(), text.size());
    String_View field;
    while (sv_try_chop_by_delim(&rest, ',', &field))
        fields.push_back(std::string(field.data, field.count));

    CHECK_INT(fields.size(), 3);
    if (fields.size() == 3) {
        CHECK_STR(fields[0].c_str(), "alpha");
        CHECK_STR(fields[1].c_str(), "beta");
        CHECK_STR(fields[2].c_str(), "gamma");
    }

    Arena arena = UTILS_ZEROED;
    const char *copied = arena_sprintf(&arena, "%s has %zu fields",
                                       text.c_str(), fields.size());
    CHECK_STR(copied, "alpha,beta,gamma has 3 fields");
    arena_free(&arena);
}

TEST(cxx_hash_map_and_paths) {
    HashMap hm = UTILS_ZEROED;
    int one = 1, two = 2;
    CHECK(hm_set(&hm, "one", &one));
    CHECK(hm_set(&hm, "two", &two));
    CHECK(hm_get(&hm, "one") == &one);
    CHECK(!hm_has(&hm, "three"));
    hm_free(&hm);

    char buf[64];
    CHECK_STR(path_basename("/usr/lib/libc.so"), "libc.so");
    path_join(buf, sizeof(buf), "build", "out.o");
    CHECK(sv_ends_with_cstr(SV(buf), "out.o"));
}

int main(void) {
    log_set_level(LOG_CRITICAL);
    utest_begin("c++");
    RUN(cxx_struct_literal_macros);
    RUN(cxx_zeroed_aggregates);
    RUN(cxx_dynamic_array_macros);
    RUN(cxx_needs_rebuild1_is_a_function_now);
    RUN(cxx_interop_with_the_standard_library);
    RUN(cxx_hash_map_and_paths);
    return utest_report();
}
