/*
 * kit.h next to the names a real project already uses. Every declaration below
 * collided with the library before it moved into the kit namespace: syslog.h
 * defines LOG_INFO and friends, raylib defines DEG2RAD, and read_file, Arena,
 * Vec2 or a TODO macro are what half the C code in the wild calls its own.
 *
 * This only has to compile. If a kit name leaks back out, it stops compiling.
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"

#include <syslog.h>

#define TODO(msg)        ((void)(msg))
#define PANIC(msg)       ((void)(msg))
#define DEG2RAD(d)       ((d) * 0.0174532925)
#define V2(x, y)         ((x) + (y))
#define SV(s)            (s)
#define da_append(a, x)  ((void)(a), (void)(x))

typedef struct { int unused; } Arena;
typedef struct { float x, y; } Vec2;
typedef struct { const char *data; size_t count; } String_View;
typedef struct { int unused; } Cmd;
typedef struct { int unused; } HashMap;
typedef int LogLevel;

static char *read_file(const char *path)             { (void)path; return NULL; }
static bool  file_exists(const char *path)           { (void)path; return false; }
static bool  mkdir_p(const char *path)               { (void)path; return false; }
static float clampf(float x, float lo, float hi)     { return x < lo ? lo : x > hi ? hi : x; }
static float lerpf(float a, float b, float t)        { return a + t * (b - a); }
static int   hash_str(const char *s)                 { return s ? s[0] : 0; }
static void  path_join(void)                         { }

int main(void) {
    /* Both worlds in the same function: the project's names, and kit's. */
    Arena arena = { 0 };
    Vec2 v = { 1, 2 };
    String_View sv = { "x", 1 };
    Cmd cmd = { 0 };
    HashMap map = { 0 };
    LogLevel level = LOG_INFO;                 /* syslog's, not ours */
    TODO("x");
    PANIC("x");
    da_append(&arena, 1);

    KitArena kit_arena = KIT_ZEROED;
    KitStr   kit_str   = KIT_STR("kit");
    kit_log_set_level(KIT_LOG_ERROR);

    int ok = read_file("x") == NULL && !file_exists("x") && !mkdir_p("x")
          && clampf(2, 0, 1) == 1 && lerpf(0, 2, 0.5f) == 1 && hash_str("a") == 'a'
          && kit_str_eq_cstr(kit_str, "kit") && kit_clampf(2, 0, 1) == 1
          && DEG2RAD(0) == 0 && V2(1, 2) == 3 && SV(1) == 1;

    path_join();
    kit_arena_free(&kit_arena);
    (void)v; (void)sv; (void)cmd; (void)map; (void)level;
    return ok ? 0 : 1;
}
