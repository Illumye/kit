/*
 * The header from C++. Not a second copy of the C suite: this checks that the
 * things C++ is stricter about actually work, since that is what breaks.
 *
 * Compiled as C++17 with the implementation included, so both halves of the
 * header have to be valid C++, not just the declarations.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"
#include "utest.h"

#include <string>
#include <vector>

TEST(cxx_struct_literal_macros) {
    /* KIT_VEC2, KIT_VEC3 and KIT_STR_LIT are compound literals in C and brace initialisation
     * in C++. They must mean the same thing in both. */
    KitVec2 a = KIT_VEC2(3, 4);
    CHECK_DBL(kit_vec2_len(a), 5.0f, 1e-5);
    CHECK_DBL(kit_vec3_len(KIT_VEC3(2, 3, 6)), 7.0f, 1e-5);

    KitStr lit = KIT_STR_LIT("literal");
    CHECK_INT(lit.count, 7);
    CHECK(kit_str_eq_cstr(lit, "literal"));
}

TEST(cxx_zeroed_aggregates) {
    /* {0} warns in C++ and {} is not C, so the library offers a spelling that
     * works in both. */
    KitBuf sb = KIT_ZEROED;
    kit_buf_append(&sb, "from c++");
    CHECK_STR(kit_buf_cstr(&sb), "from c++");
    kit_buf_free(&sb);

    KitCommand c = KIT_ZEROED;
    kit_command_push(&c, "true");
    CHECK_INT(c.count, 1);
    kit_command_free(&c);
}

TEST(cxx_dynamic_array_macros) {
    /* kit_array_reserve assigns the result of realloc to a typed member, which C++
     * will not do implicitly. */
    struct IntArray { int *items; size_t count, capacity; };
    IntArray a = KIT_ZEROED;

    for (int i = 0; i < 500; i++) kit_array_push(&a, i * 2);
    CHECK_INT(a.count, 500);
    CHECK_INT(a.items[499], 998);

    size_t at;
    kit_array_find(&a, 42, at);
    CHECK_INT(at, 21);

    long sum = 0;
    kit_array_each(int, it, &a) sum += *it;
    CHECK_INT(sum, 499L * 500);
    kit_array_free(&a);
}

TEST(cxx_fs_stale1_is_a_function_now) {
    /* It used to be a macro over a compound literal, which C++ has no form of. */
    const char *path = "utest-tmp-cxx.txt";
    kit_fs_write(path, "x", 1, NULL);
    CHECK_INT(kit_fs_stale1(path, path, NULL), 1);      /* itself, so not older */
    CHECK_INT(kit_fs_stale1("no/such/output", path, NULL), 1);
    kit_fs_remove(path, NULL);
}

TEST(cxx_interop_with_the_standard_library) {
    /* The realistic use: C++ code holding std types, calling into the C API. */
    std::string     text  = "alpha,beta,gamma";
    std::vector<std::string> fields;

    KitStr rest = kit_str_from_parts(text.data(), text.size());
    KitStr field;
    while (kit_str_next(&rest, ',', &field))
        fields.push_back(std::string(field.data, field.count));

    CHECK_INT(fields.size(), 3);
    if (fields.size() == 3) {
        CHECK_STR(fields[0].c_str(), "alpha");
        CHECK_STR(fields[1].c_str(), "beta");
        CHECK_STR(fields[2].c_str(), "gamma");
    }

    KitArena arena = KIT_ZEROED;
    const char *copied = kit_arena_printf(&arena, "%s has %zu fields",
                                       text.c_str(), fields.size());
    CHECK_STR(copied, "alpha,beta,gamma has 3 fields");
    kit_arena_free(&arena);
}

TEST(cxx_hash_map_and_paths) {
    KitMap hm = KIT_ZEROED;
    int one = 1, two = 2;
    CHECK(kit_map_set(&hm, "one", &one));
    CHECK(kit_map_set(&hm, "two", &two));
    CHECK(kit_map_get(&hm, "one") == &one);
    CHECK(!kit_map_has(&hm, "three"));
    kit_map_free(&hm);

    char buf[64];
    CHECK_STR(kit_path_basename("/usr/lib/libc.so"), "libc.so");
    kit_path_join(buf, sizeof(buf), "build", "out.o");
    CHECK(kit_str_ends_with_cstr(KIT_STR(buf), "out.o"));
}

int main(void) {
    kit_log_set_level(KIT_LOG_CRITICAL);
    utest_begin("c++");
    RUN(cxx_struct_literal_macros);
    RUN(cxx_zeroed_aggregates);
    RUN(cxx_dynamic_array_macros);
    RUN(cxx_fs_stale1_is_a_function_now);
    RUN(cxx_interop_with_the_standard_library);
    RUN(cxx_hash_map_and_paths);
    return utest_report();
}
