/*
 * utils.h - Utility collection stb-style header
 *
 * USAGE:
 *   In exactly ONE C file, before including this header:
 *       #define UTILS_IMPLEMENTATION
 *       #include "utils.h"
 *
 *   In all other files:
 *       #include "utils.h"
 *
 * SECTIONS:
 *   1.  Logging
 *   2.  Files
 *   3.  Dynamic Arrays
 *   4.  String Views
 *   5.  Arena Allocator
 *   6.  Time / Stopwatch
 *   7.  Vectorial Math
 *   8.  CLI Args
 *   9.  String Builder
 *   10. Command Execution  (sync, async, capture)
 *   11. Hash / Misc
 *   12. HashMap
 *   13. Path Utilities
 *   14. Scalar Math
 *
 * REQUIREMENTS:
 *   C11 or later. On POSIX systems the implementation uses clock_gettime(),
 *   dprintf() and isatty(); the header requests them via _POSIX_C_SOURCE, so
 *   it must be included before any other system header when building with a
 *   strict -std=c11 (as opposed to -std=gnu11).
 */

#ifndef UTILS_H
#define UTILS_H

/* Requested before any system header: clock_gettime(), dprintf() and isatty()
 * are hidden behind these under a strict -std=c11. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#endif

#ifdef _WIN32
#    ifndef _CRT_SECURE_NO_WARNINGS
#        define _CRT_SECURE_NO_WARNINGS
#    endif
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <io.h>
#else
#    include <unistd.h>
#    include <sys/wait.h>
#    include <sys/stat.h>
#    include <fcntl.h>
#endif

/* --------------------------------------------------------------------------
 * Compiler helpers
 * -------------------------------------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
#    define UTILS_PRINTF_FORMAT(fmt_idx, first_idx) \
         __attribute__((format(printf, fmt_idx, first_idx)))
#else
#    define UTILS_PRINTF_FORMAT(fmt_idx, first_idx)
#endif

/* Marks utils_panic_impl as never returning, so that callers do not trigger
 * "control reaches end of non-void function" and the optimizer can drop the
 * code that follows a PANIC. */
#if defined(__GNUC__) || defined(__clang__)
#    define UTILS_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#    define UTILS_NORETURN __declspec(noreturn)
#else
#    define UTILS_NORETURN
#endif

#define UTILS_UNUSED(v)      (void)(v)
#define UTILS_ARRAY_LEN(a)   (sizeof(a) / sizeof((a)[0]))

/* Defer / cleanup pattern (inspired by nob_return_defer).
 *
 * Usage:
 *   bool my_func(void) {
 *       bool result = true;
 *       FILE *f = fopen(...);
 *       if (!f) return_defer(false);
 *       ...
 *   defer:
 *       if (f) fclose(f);
 *       return result;
 *   }
 */
#define return_defer(value) do { result = (value); goto defer; } while (0)

/* --------------------------------------------------------------------------
 * SECTION 1 : LOGGING
 * -------------------------------------------------------------------------- */

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_CRITICAL
} LogLevel;

/* When to emit ANSI escape sequences.
 *   LOG_COLOR_AUTO   colour only when the output stream is a terminal and the
 *                    NO_COLOR environment variable is unset (default)
 *   LOG_COLOR_ALWAYS force colour, e.g. when piping into a pager that renders it
 *   LOG_COLOR_NEVER  never colour
 */
typedef enum {
    LOG_COLOR_AUTO,
    LOG_COLOR_ALWAYS,
    LOG_COLOR_NEVER
} LogColorMode;

void log_set_level(LogLevel level);
void log_set_output(FILE *fp);   /* NULL resets to stderr (default) */
void log_set_color(LogColorMode mode);

void utils_log_impl(LogLevel level, const char *file, int line,
                    const char *fmt, ...) UTILS_PRINTF_FORMAT(4, 5);
UTILS_NORETURN void utils_panic_impl(const char *file, int line,
                                     const char *fmt, ...) UTILS_PRINTF_FORMAT(3, 4);

#define LOG(level, ...)  utils_log_impl(level, __FILE__, __LINE__, __VA_ARGS__)
#define PANIC(...)       utils_panic_impl(__FILE__, __LINE__, __VA_ARGS__)
#define TODO(msg)        PANIC("TODO: %s", msg)
#define UNREACHABLE(msg) PANIC("UNREACHABLE: %s", msg)

/* --------------------------------------------------------------------------
 * SECTION 2 : FILES
 * -------------------------------------------------------------------------- */

/* Read an entire file into a malloc'd, NUL-terminated buffer (caller owns it).
 * Returns NULL on error.
 *
 * The read is streamed, so it also works on files whose size is not known up
 * front: pipes, character devices and the synthetic files under /proc.
 *
 * read_file_ex additionally reports the byte count, which is the only way to
 * handle binary data containing embedded NUL bytes. The terminator is always
 * written, so the result stays usable as a C string for text files. */
char *read_file(const char *path);
char *read_file_ex(const char *path, size_t *out_size);

/* Write data to file. Returns false on error. */
bool write_file(const char *path, const void *data, size_t size);

/* Returns true if path exists and is a regular file. */
bool file_exists(const char *path);

/* Returns the size in bytes of the file at path, or -1 on error. */
long file_size(const char *path);

/* --------------------------------------------------------------------------
 * SECTION 3 : DYNAMIC ARRAYS
 *
 * Any struct with:
 *   T      *items;
 *   size_t  count;
 *   size_t  capacity;
 * works with these macros.
 * -------------------------------------------------------------------------- */

#ifndef DA_INIT_CAP
#define DA_INIT_CAP 256
#endif

/* Reserve at least `cap` slots.
 * The result of realloc lands in a temporary so that the original block is not
 * leaked when the allocation fails, and capacity growth is checked against
 * SIZE_MAX so that an absurd request aborts instead of wrapping around. */
#define da_reserve(da, cap)                                                         \
    do {                                                                            \
        size_t _want = (cap);                                                       \
        if (_want > (da)->capacity) {                                               \
            size_t _cap = (da)->capacity ? (da)->capacity : (size_t)DA_INIT_CAP;    \
            while (_want > _cap) {                                                  \
                if (_cap > SIZE_MAX / 2) PANIC("da_reserve: capacity overflow");    \
                _cap *= 2;                                                          \
            }                                                                       \
            if (_cap > SIZE_MAX / sizeof(*(da)->items))                             \
                PANIC("da_reserve: allocation size overflow");                      \
            void *_mem = realloc((da)->items, _cap * sizeof(*(da)->items));         \
            if (!_mem) PANIC("da_reserve: realloc of %zu bytes failed",             \
                             _cap * sizeof(*(da)->items));                          \
            (da)->items    = _mem;                                                  \
            (da)->capacity = _cap;                                                  \
        }                                                                           \
    } while (0)

/* Append a single item. */
#define da_append(da, item)                \
    do {                                   \
        da_reserve((da), (da)->count + 1); \
        (da)->items[(da)->count++] = (item); \
    } while (0)

/* Append n items from a pointer. */
#define da_append_many(da, src, n)                  \
    do {                                            \
        size_t _n = (n);                            \
        if (_n > 0) {                               \
            da_reserve((da), (da)->count + _n);     \
            memcpy((da)->items + (da)->count,       \
                   (src),                           \
                   _n * sizeof(*(da)->items));      \
            (da)->count += _n;                      \
        }                                           \
    } while (0)

/* Pop the last element (assert non-empty). */
#define da_pop(da) \
    ((da)->items[(da)->count > 0 ? --(da)->count \
                                 : (PANIC("da_pop on empty array"), (size_t)0)])

/* Access first / last with bounds check. */
#define da_first(da) \
    ((da)->items[(da)->count > 0 ? (size_t)0 \
                                 : (PANIC("da_first on empty array"), (size_t)0)])
#define da_last(da) \
    ((da)->items[(da)->count > 0 ? (da)->count - 1 \
                                 : (PANIC("da_last on empty array"), (size_t)0)])

/* Remove element at index i, swap with last (unordered). */
#define da_remove_unordered(da, i)                           \
    do {                                                     \
        size_t _i = (i);                                     \
        if (_i >= (da)->count) PANIC("da_remove_unordered: out of bounds"); \
        (da)->items[_i] = (da)->items[--(da)->count];        \
    } while (0)

/* Iterate. `it` is a pointer to the current element.
 * Example:
 *   da_foreach(int, x, &my_array) { printf("%d\n", *x); }
 */
#define da_foreach(Type, it, da) \
    for (Type *it = (da)->items; it < (da)->items + (da)->count; ++it)

/* Linear search — evaluates to true if any element == val.
 * Requires GCC/Clang (statement expression).
 * Example:
 *   if (da_contains(&my_array, 42)) { ... }
 */
#if defined(__GNUC__) || defined(__clang__)
#define da_contains(da, val)                                                 \
    __extension__({                                                          \
        bool _found = false;                                                 \
        for (size_t _j = 0; _j < (da)->count; _j++)                         \
            if ((da)->items[_j] == (val)) { _found = true; break; }         \
        _found;                                                              \
    })
#endif

/* Free and zero the array. */
#define da_free(da)         \
    do {                    \
        free((da)->items);  \
        (da)->items    = NULL; \
        (da)->count    = 0; \
        (da)->capacity = 0; \
    } while (0)

/* --------------------------------------------------------------------------
 * SECTION 4 : STRING VIEWS
 *
 * A non-owning slice over existing memory. Never NUL-terminated.
 * Use SV_Fmt / SV_Arg with printf.
 * -------------------------------------------------------------------------- */

typedef struct {
    const char *data;
    size_t      count;
} String_View;

#define SV(cstr)         sv_from_cstr(cstr)
#define SV_Fmt           "%.*s"
#define SV_Arg(sv)       (int)(sv).count, (sv).data
#define SV_LIT(literal)  ((String_View){ (literal), sizeof(literal) - 1 })

String_View sv_from_cstr(const char *cstr);

/* Whitespace trimming. */
String_View sv_trim_left(String_View sv);
String_View sv_trim_right(String_View sv);
String_View sv_trim(String_View sv);

/* Consume characters from the left up to (but not including) `delim`.
 * Advances *sv past the delimiter. Returns the consumed part. */
String_View sv_chop_by_delim(String_View *sv, char delim);

/* Consume n characters from the left. */
String_View sv_chop_left(String_View *sv, size_t n);

/* Comparison. */
bool sv_eq(String_View a, String_View b);
bool sv_eq_cstr(String_View a, const char *b);
bool sv_starts_with(String_View sv, String_View prefix);
bool sv_ends_with(String_View sv, String_View suffix);
bool sv_starts_with_cstr(String_View sv, const char *prefix);
bool sv_ends_with_cstr(String_View sv, const char *suffix);

/* --------------------------------------------------------------------------
 * SECTION 5 : ARENA ALLOCATOR
 * -------------------------------------------------------------------------- */

typedef struct {
    char   *buffer;
    size_t  length;
    size_t  offset;
} Arena;

Arena  arena_make(size_t size);
void  *arena_alloc(Arena *a, size_t size);

/* Convenience: arena_alloc_array(arena, T, n) allocates n items of type T. */
#define arena_alloc_array(a, T, n) ((T *)arena_alloc((a), sizeof(T) * (n)))

void arena_reset(Arena *a);
void arena_free(Arena *a);

/* --------------------------------------------------------------------------
 * SECTION 6 : TIME / STOPWATCH
 * -------------------------------------------------------------------------- */

typedef struct {
#ifdef _WIN32
    LARGE_INTEGER start;
#else
    struct timespec start;
#endif
} Stopwatch;

Stopwatch sw_start(void);
double    sw_elapsed_s(Stopwatch sw);
double    sw_elapsed_ms(Stopwatch sw);

/* --------------------------------------------------------------------------
 * SECTION 7 : VECTORIAL MATH
 *
 * Define UTILS_NO_VEC_MATH before including to skip this section and avoid
 * the dependency on -lm.
 * -------------------------------------------------------------------------- */

#ifndef UTILS_NO_VEC_MATH
#include <math.h>

typedef struct { float x, y; }    Vec2;
typedef struct { float x, y, z; } Vec3;

#define V2(x, y)    ((Vec2){(float)(x), (float)(y)})
#define V3(x, y, z) ((Vec3){(float)(x), (float)(y), (float)(z)})

#define V2_Fmt      "(%.2f, %.2f)"
#define V2_Arg(v)   (v).x, (v).y
#define V3_Fmt      "(%.2f, %.2f, %.2f)"
#define V3_Arg(v)   (v).x, (v).y, (v).z

Vec2  vec2_add(Vec2 a, Vec2 b);
Vec2  vec2_sub(Vec2 a, Vec2 b);
Vec2  vec2_scale(Vec2 a, float s);
Vec2  vec2_mul(Vec2 a, Vec2 b);
float vec2_len(Vec2 a);
float vec2_dist(Vec2 a, Vec2 b);
Vec2  vec2_norm(Vec2 a);
float vec2_dot(Vec2 a, Vec2 b);

Vec3  vec3_add(Vec3 a, Vec3 b);
Vec3  vec3_sub(Vec3 a, Vec3 b);
Vec3  vec3_scale(Vec3 a, float s);
Vec3  vec3_mul(Vec3 a, Vec3 b);
float vec3_len(Vec3 a);
Vec3  vec3_norm(Vec3 a);
float vec3_dot(Vec3 a, Vec3 b);
Vec3  vec3_cross(Vec3 a, Vec3 b);

#endif /* UTILS_NO_VEC_MATH */

/* --------------------------------------------------------------------------
 * SECTION 8 : CLI ARGS
 * -------------------------------------------------------------------------- */

/* Shift and return the next argument (NULL when exhausted). */
char *args_shift(int *argc, char ***argv);

/* -- Option parsing --------------------------------------------------------
 *
 * Define an array of Opt, initialize destination variables with defaults,
 * then call opts_parse. Positional arguments are left in argv/argc.
 *
 * Usage:
 *   bool verbose  = false;
 *   const char *output = "a.out";
 *   int  count    = 1;
 *
 *   Opt opts[] = {
 *       OPT_FLAG('v', "verbose", "Enable verbose output",  &verbose),
 *       OPT_STR ('o', "output",  "FILE", "Output file",   &output),
 *       OPT_INT ('n', "count",   "N",    "Iterations",    &count),
 *   };
 *   if (!opts_parse_arr(opts, &argc, &argv)) return 1;
 *
 * Supported forms:
 *   -v           flag
 *   -o file      short with separate value
 *   -ofile       short with attached value
 *   -o=file      short with '=' separator
 *   --output file
 *   --output=file
 *   --           ends option parsing; remaining args are positional
 * -------------------------------------------------------------------------- */

typedef enum { OPTTYPE_FLAG, OPTTYPE_STR, OPTTYPE_INT } OptType;

typedef struct {
    char        short_name; /* single char, or 0 */
    const char *long_name;  /* without leading "--", or NULL */
    OptType     type;
    const char *meta;       /* value placeholder shown in help, e.g. "FILE" */
    const char *help;
    void       *dst;        /* bool* / const char** / int* */
} Opt;

#define OPT_FLAG(s, l,       help, dst) { (s), (l), OPTTYPE_FLAG, NULL,  (help), (dst) }
#define OPT_STR( s, l, meta, help, dst) { (s), (l), OPTTYPE_STR,  (meta),(help), (dst) }
#define OPT_INT( s, l, meta, help, dst) { (s), (l), OPTTYPE_INT,  (meta),(help), (dst) }

/* Parse options in-place. Unknown options or missing values → LOG_ERROR + false.
 * After the call, argc/argv contain only the positional arguments. */
bool opts_parse(Opt *opts, size_t n_opts, int *argc, char ***argv);

/* Print a formatted option summary to fp. */
void opts_usage(FILE *fp, const char *program, const Opt *opts, size_t n_opts);

#define opts_parse_arr(opts, argc, argv) \
    opts_parse((opts), UTILS_ARRAY_LEN(opts), (argc), (argv))
#define opts_usage_arr(fp, prog, opts) \
    opts_usage((fp), (prog), (opts), UTILS_ARRAY_LEN(opts))

/* --------------------------------------------------------------------------
 * SECTION 9 : STRING BUILDER
 * -------------------------------------------------------------------------- */

typedef struct {
    char   *items;
    size_t  count;
    size_t  capacity;
} StringBuilder;

void  sb_append(StringBuilder *sb, const char *str);
void  sb_append_n(StringBuilder *sb, const char *str, size_t n);
void  sb_append_char(StringBuilder *sb, char c);
void  sb_appendf(StringBuilder *sb, const char *fmt, ...) UTILS_PRINTF_FORMAT(2, 3);
void  sb_append_sv(StringBuilder *sb, String_View sv);

/* Return a NUL-terminated view into the builder (no copy). */
char *sb_cstr(StringBuilder *sb);

/* Return a freshly malloc'd copy. Caller owns the result. */
char *sb_to_string(StringBuilder *sb);

void sb_reset(StringBuilder *sb);
void sb_free(StringBuilder *sb);

/* --------------------------------------------------------------------------
 * SECTION 10 : COMMAND EXECUTION
 *
 * Three execution modes:
 *   cmd_run(c)          - synchronous, inherits stdout/stderr
 *   cmd_run_async(c)    - asynchronous, returns a Proc handle
 *   cmd_capture(c, sb)  - synchronous, captures stdout into a StringBuilder
 *
 * Variadic shorthand:
 *   cmd_run_args("cc", "-o", "out", "main.c", NULL)
 * -------------------------------------------------------------------------- */

typedef struct {
    const char **items;
    size_t       count;
    size_t       capacity;
} Cmd;

#ifdef _WIN32
typedef HANDLE Proc;
#define INVALID_PROC INVALID_HANDLE_VALUE
#else
typedef int    Proc;
#define INVALID_PROC (-1)
#endif

/* Append one argument. */
void cmd_append(Cmd *c, const char *arg);

/* Append multiple arguments at once (NULL-terminated varargs). */
void cmd_extend(Cmd *c, ...);

/* Run synchronously. Returns false on failure. */
bool cmd_run(Cmd *c);

/* Run asynchronously. Returns the child process handle (INVALID_PROC on error).
 * Call proc_wait() to reap it. The Cmd is NOT reset. */
Proc cmd_run_async(Cmd *c);

/* Wait for an async process. Returns false if the child exited non-zero. */
bool proc_wait(Proc p);

/* Run synchronously but capture stdout into sb (stderr goes to stderr).
 * Returns false on error. */
bool cmd_capture(Cmd *c, StringBuilder *sb);

/* Variadic shorthand — terminate with NULL.
 * cmd_run_args("ls", "-la", NULL); */
bool cmd_run_args(const char *first, ...);

/* Reset arguments without freeing the underlying allocation (reuse the Cmd). */
void cmd_reset(Cmd *c);

void cmd_free(Cmd *c);

/* --------------------------------------------------------------------------
 * SECTION 11 : HASH / MISC
 * -------------------------------------------------------------------------- */

/* djb2 over a NUL-terminated string, and over arbitrary bytes.
 *
 * Fast, but its low bits carry little entropy for keys sharing a prefix
 * ("key1", "key2", ...), which is exactly the pattern a power-of-two table
 * indexes on. Run the result through hash_mix32 before masking it. */
uint32_t hash_str(const char *s);
uint32_t hash_bytes(const void *data, size_t len);

/* Avalanche step: spreads the entropy of a 32-bit hash across all of its bits
 * so that the low ones are usable as a bucket index. This is the murmur3
 * finalizer with Stafford's constants. */
uint32_t hash_mix32(uint32_t h);

/* --------------------------------------------------------------------------
 * SECTION 12 : HASHMAP
 *
 * String-keyed, void*-valued hash map (open addressing, linear probing).
 * Keys are NOT copied — the caller must ensure they outlive the map.
 *
 * Usage:
 *   HashMap hm = {0};
 *   hm_set(&hm, "foo", my_ptr);
 *   void *v = hm_get(&hm, "foo");   // NULL if absent
 *   hm_delete(&hm, "foo");
 *   hm_foreach(&hm, e) { printf("%s\n", e->key); }
 *   hm_free(&hm);
 * -------------------------------------------------------------------------- */

typedef struct {
    const char *key;
    void       *value;
} HM_Entry;

typedef struct {
    HM_Entry *entries;
    size_t    count;     /* live entries */
    size_t    used;      /* live entries + tombstones; drives the load factor */
    size_t    capacity;
} HashMap;

/* Insert or update. Returns true if the key is new. */
bool  hm_set(HashMap *hm, const char *key, void *value);

/* Returns the value, or NULL if the key is absent. */
void *hm_get(const HashMap *hm, const char *key);

/* Returns true if the key exists (safe even when stored value is NULL). */
bool  hm_has(const HashMap *hm, const char *key);

/* Removes the key. Returns true if it was present. */
bool  hm_delete(HashMap *hm, const char *key);

/* Returns true if entry `e` is a live (non-deleted) slot. */
bool  hm_entry_live(const HM_Entry *e);

void  hm_free(HashMap *hm);

/* Iterate over live entries.
 * Example:
 *   hm_foreach(&hm, e) { printf("%s -> %p\n", e->key, e->value); }
 */
#define hm_foreach(hm, it) \
    for (HM_Entry *(it) = (hm)->entries; \
         (it) < (hm)->entries + (hm)->capacity; ++(it)) \
        if (hm_entry_live(it))

/* --------------------------------------------------------------------------
 * SECTION 13 : PATH UTILITIES
 * -------------------------------------------------------------------------- */

#ifdef _WIN32
#    define PATH_SEP '\\'
#else
#    define PATH_SEP '/'
#endif

/* Returns a pointer INTO path — no allocation. */
const char *path_basename(const char *path);

/* Returns a pointer to the extension (including '.'), or a pointer to the
 * terminating '\0' if there is no extension. No allocation. */
const char *path_ext(const char *path);

/* Writes the directory component of path into buf[bufsz]. Returns buf. */
char *path_dirname(const char *path, char *buf, size_t bufsz);

/* Joins two path components into buf[bufsz]. Returns buf. */
char *path_join(char *buf, size_t bufsz, const char *a, const char *b);

/* Returns true if path is absolute. */
bool  path_is_absolute(const char *path);

/* --------------------------------------------------------------------------
 * SECTION 14 : SCALAR MATH
 * -------------------------------------------------------------------------- */

#define UTILS_MIN(a, b) ((a) < (b) ? (a) : (b))
#define UTILS_MAX(a, b) ((a) > (b) ? (a) : (b))

/* clamp x to [lo, hi]. */
static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : x > hi ? hi : x;
}
static inline double clampd(double x, double lo, double hi) {
    return x < lo ? lo : x > hi ? hi : x;
}
static inline int clampi(int x, int lo, int hi) {
    return x < lo ? lo : x > hi ? hi : x;
}

/* Linear interpolation: lerp(a, b, 0) = a, lerp(a, b, 1) = b. */
static inline float lerpf(float a, float b, float t) {
    return a + t * (b - a);
}

/* Re-map x from [in_lo, in_hi] to [out_lo, out_hi]. */
static inline float map_range(float x,
                               float in_lo, float in_hi,
                               float out_lo, float out_hi) {
    if (in_hi == in_lo) return out_lo;
    return out_lo + (x - in_lo) * (out_hi - out_lo) / (in_hi - in_lo);
}

/* Degrees <-> radians. */
#define DEG2RAD(d) ((d) * (float)(3.14159265358979323846 / 180.0))
#define RAD2DEG(r) ((r) * (float)(180.0 / 3.14159265358979323846))

#endif /* UTILS_H */

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef UTILS_IMPLEMENTATION

/* --------------------------------------------------------------------------
 * Logging
 * -------------------------------------------------------------------------- */

static LogLevel      UTILS__MIN_LEVEL = LOG_DEBUG;
static FILE         *UTILS__OUTPUT    = NULL;  /* NULL -> stderr */
static LogColorMode  UTILS__COLOR     = LOG_COLOR_AUTO;

static const char *UTILS__COLORS[] = {
    "\x1b[90m",  /* DEBUG    - grey          */
    "\x1b[34m",  /* INFO     - blue          */
    "\x1b[33m",  /* WARNING  - yellow        */
    "\x1b[31m",  /* ERROR    - red           */
    "\x1b[41m",  /* CRITICAL - red bg        */
};

static const char *UTILS__LABELS[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "CRIT"
};

void log_set_level(LogLevel level) {
    UTILS__MIN_LEVEL = level;
}

void log_set_output(FILE *fp) {
    UTILS__OUTPUT = fp;
}

void log_set_color(LogColorMode mode) {
    UTILS__COLOR = mode;
}

static bool utils__stream_is_tty(FILE *out) {
#ifdef _WIN32
    return _isatty(_fileno(out)) != 0;
#else
    return isatty(fileno(out)) != 0;
#endif
}

/* Escape sequences are worthless in a log file and actively harmful when the
 * output is grepped, so AUTO keeps them for terminals only. NO_COLOR is the
 * cross-tool convention (https://no-color.org). */
static bool utils__use_color(FILE *out) {
    switch (UTILS__COLOR) {
        case LOG_COLOR_ALWAYS: return true;
        case LOG_COLOR_NEVER:  return false;
        default: break;
    }
    const char *no_color = getenv("NO_COLOR");
    if (no_color && *no_color) return false;
    return utils__stream_is_tty(out);
}

/* localtime() hands back a shared static buffer; the _r / _s variants keep the
 * logger usable from more than one thread. */
static void utils__timestamp(char *buf, size_t bufsz) {
    time_t t = time(NULL);
    struct tm tm_buf;
    struct tm *tm_info;
#ifdef _WIN32
    tm_info = (localtime_s(&tm_buf, &t) == 0) ? &tm_buf : NULL;
#else
    tm_info = localtime_r(&t, &tm_buf);
#endif
    if (!tm_info || strftime(buf, bufsz, "%H:%M:%S", tm_info) == 0)
        snprintf(buf, bufsz, "--:--:--");
}

static void utils__log_prefix(FILE *out, const char *color, const char *label,
                              const char *file, int line) {
    char time_buf[16];
    utils__timestamp(time_buf, sizeof(time_buf));

    if (utils__use_color(out))
        fprintf(out, "%s[%s] [%s]\x1b[0m \x1b[90m[%s:%d]\x1b[0m ",
                color, time_buf, label, file, line);
    else
        fprintf(out, "[%s] [%s] [%s:%d] ", time_buf, label, file, line);
}

void utils_log_impl(LogLevel level, const char *file, int line,
                    const char *fmt, ...) {
    if (level < UTILS__MIN_LEVEL) return;

    FILE *out = UTILS__OUTPUT ? UTILS__OUTPUT : stderr;
    utils__log_prefix(out, UTILS__COLORS[level], UTILS__LABELS[level], file, line);

    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);

    fputc('\n', out);
}

void utils_panic_impl(const char *file, int line, const char *fmt, ...) {
    FILE *out = UTILS__OUTPUT ? UTILS__OUTPUT : stderr;
    utils__log_prefix(out, UTILS__COLORS[LOG_CRITICAL], "PANIC", file, line);

    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);

    fprintf(out, "\nAborting...\n");
    fflush(out);
    abort();
}

/* --------------------------------------------------------------------------
 * Files
 * -------------------------------------------------------------------------- */

#ifndef UTILS_READ_CHUNK
#define UTILS_READ_CHUNK 65536
#endif

/* Streamed so that the buffer never depends on ftell(): pipes, terminals and
 * the /proc files all report a size of 0 while still delivering data. The
 * reported size, when plausible, is only used to seed the capacity. */
char *read_file_ex(const char *path, size_t *out_size) {
    bool   result = true;
    FILE  *f      = NULL;
    char  *buffer = NULL;
    size_t len    = 0;
    size_t cap    = 0;

    f = fopen(path, "rb");
    if (!f) {
        LOG(LOG_ERROR, "read_file: cannot open '%s': %s", path, strerror(errno));
        return_defer(false);
    }

    if (fseek(f, 0, SEEK_END) == 0) {
        long hint = ftell(f);
        if (hint > 0) cap = (size_t)hint;
        rewind(f);
    }
    if (cap == 0) cap = UTILS_READ_CHUNK;

    buffer = malloc(cap + 1);
    if (!buffer) {
        LOG(LOG_ERROR, "read_file: out of memory reading '%s'", path);
        return_defer(false);
    }

    for (;;) {
        if (len == cap) {
            if (cap > SIZE_MAX / 2 - 1) {
                LOG(LOG_ERROR, "read_file: '%s' is too large to buffer", path);
                return_defer(false);
            }
            cap *= 2;
            char *grown = realloc(buffer, cap + 1);
            if (!grown) {
                LOG(LOG_ERROR, "read_file: out of memory reading '%s'", path);
                return_defer(false);
            }
            buffer = grown;
        }

        size_t n = fread(buffer + len, 1, cap - len, f);
        len += n;
        if (n == 0) break;
    }

    if (ferror(f)) {
        LOG(LOG_ERROR, "read_file: read error on '%s': %s", path, strerror(errno));
        return_defer(false);
    }

    buffer[len] = '\0';
    LOG(LOG_DEBUG, "read_file: '%s' (%zu bytes)", path, len);

defer:
    if (f) fclose(f);
    if (!result) { free(buffer); return NULL; }
    if (out_size) *out_size = len;
    return buffer;
}

char *read_file(const char *path) {
    return read_file_ex(path, NULL);
}

/* A buffered write only reaches the disk on fclose, so its return value is the
 * one that reports a full filesystem or a failing quota. */
bool write_file(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        LOG(LOG_ERROR, "write_file: cannot open '%s': %s", path, strerror(errno));
        return false;
    }

    bool ok = (size == 0) || (fwrite(data, 1, size, f) == size);
    if (!ok) LOG(LOG_ERROR, "write_file: short write on '%s': %s", path, strerror(errno));

    if (fclose(f) != 0) {
        LOG(LOG_ERROR, "write_file: cannot flush '%s': %s", path, strerror(errno));
        ok = false;
    }

    if (ok) LOG(LOG_DEBUG, "write_file: '%s' (%zu bytes)", path, size);
    return ok;
}

bool file_exists(const char *path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES &&
           !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

long file_size(const char *path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) return -1;
    return (long)(((LONGLONG)info.nFileSizeHigh << 32) | info.nFileSizeLow);
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
#endif
}

/* --------------------------------------------------------------------------
 * String Views
 * -------------------------------------------------------------------------- */

String_View sv_from_cstr(const char *cstr) {
    return (String_View){ .data = cstr, .count = strlen(cstr) };
}

String_View sv_trim_left(String_View sv) {
    size_t i = 0;
    while (i < sv.count && isspace((unsigned char)sv.data[i])) i++;
    return (String_View){ .data = sv.data + i, .count = sv.count - i };
}

String_View sv_trim_right(String_View sv) {
    size_t i = 0;
    while (i < sv.count && isspace((unsigned char)sv.data[sv.count - 1 - i])) i++;
    return (String_View){ .data = sv.data, .count = sv.count - i };
}

String_View sv_trim(String_View sv) {
    return sv_trim_right(sv_trim_left(sv));
}

String_View sv_chop_by_delim(String_View *sv, char delim) {
    size_t i = 0;
    while (i < sv->count && sv->data[i] != delim) i++;
    String_View result = { .data = sv->data, .count = i };
    if (i < sv->count) {
        sv->data  += i + 1;
        sv->count -= i + 1;
    } else {
        sv->data  += i;
        sv->count -= i;
    }
    return result;
}

String_View sv_chop_left(String_View *sv, size_t n) {
    if (n > sv->count) n = sv->count;
    String_View result = { .data = sv->data, .count = n };
    sv->data  += n;
    sv->count -= n;
    return result;
}

bool sv_eq(String_View a, String_View b) {
    if (a.count != b.count) return false;
    return memcmp(a.data, b.data, a.count) == 0;
}

bool sv_eq_cstr(String_View a, const char *b) {
    return sv_eq(a, sv_from_cstr(b));
}

bool sv_starts_with(String_View sv, String_View prefix) {
    if (prefix.count > sv.count) return false;
    return memcmp(sv.data, prefix.data, prefix.count) == 0;
}

bool sv_ends_with(String_View sv, String_View suffix) {
    if (suffix.count > sv.count) return false;
    return memcmp(sv.data + sv.count - suffix.count,
                  suffix.data, suffix.count) == 0;
}

bool sv_starts_with_cstr(String_View sv, const char *prefix) {
    return sv_starts_with(sv, sv_from_cstr(prefix));
}

bool sv_ends_with_cstr(String_View sv, const char *suffix) {
    return sv_ends_with(sv, sv_from_cstr(suffix));
}

/* --------------------------------------------------------------------------
 * Arena
 * -------------------------------------------------------------------------- */

Arena arena_make(size_t size) {
    char *mem = malloc(size);
    if (!mem) PANIC("arena_make: malloc failed (%zu bytes)", size);
    return (Arena){ .buffer = mem, .length = size, .offset = 0 };
}

void *arena_alloc(Arena *a, size_t size) {
    /* Align to pointer size. */
    size_t align   = sizeof(void *);
    size_t padding = (align - ((uintptr_t)(a->buffer + a->offset) % align)) % align;

    if (a->offset + padding + size > a->length)
        PANIC("arena_alloc: OOM (capacity=%zu, requested=%zu)", a->length, size);

    a->offset += padding;
    void *ptr  = a->buffer + a->offset;
    a->offset += size;
    memset(ptr, 0, size);
    return ptr;
}

void arena_reset(Arena *a) { a->offset = 0; }

void arena_free(Arena *a) {
    free(a->buffer);
    a->buffer = NULL;
    a->offset = 0;
    a->length = 0;
}

/* --------------------------------------------------------------------------
 * Stopwatch
 * -------------------------------------------------------------------------- */

Stopwatch sw_start(void) {
    Stopwatch sw;
#ifdef _WIN32
    QueryPerformanceCounter(&sw.start);
#else
    clock_gettime(CLOCK_MONOTONIC, &sw.start);
#endif
    return sw;
}

double sw_elapsed_s(Stopwatch sw) {
#ifdef _WIN32
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return (double)(now.QuadPart - sw.start.QuadPart) / (double)freq.QuadPart;
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec  - sw.start.tv_sec) +
           (double)(now.tv_nsec - sw.start.tv_nsec) * 1e-9;
#endif
}

double sw_elapsed_ms(Stopwatch sw) {
    return sw_elapsed_s(sw) * 1000.0;
}

/* --------------------------------------------------------------------------
 * Vec2 / Vec3
 * -------------------------------------------------------------------------- */

#ifndef UTILS_NO_VEC_MATH

Vec2  vec2_add(Vec2 a, Vec2 b)        { return V2(a.x + b.x, a.y + b.y); }
Vec2  vec2_sub(Vec2 a, Vec2 b)        { return V2(a.x - b.x, a.y - b.y); }
Vec2  vec2_scale(Vec2 a, float s)     { return V2(a.x * s, a.y * s); }
Vec2  vec2_mul(Vec2 a, Vec2 b)        { return V2(a.x * b.x, a.y * b.y); }
float vec2_len(Vec2 a)                { return sqrtf(a.x*a.x + a.y*a.y); }
float vec2_dist(Vec2 a, Vec2 b)       { return vec2_len(vec2_sub(b, a)); }
float vec2_dot(Vec2 a, Vec2 b)        { return a.x*b.x + a.y*b.y; }

Vec2 vec2_norm(Vec2 a) {
    float l = vec2_len(a);
    return l == 0.0f ? V2(0, 0) : vec2_scale(a, 1.0f / l);
}

/* --------------------------------------------------------------------------
 * Vec3
 * -------------------------------------------------------------------------- */

Vec3  vec3_add(Vec3 a, Vec3 b)        { return V3(a.x+b.x, a.y+b.y, a.z+b.z); }
Vec3  vec3_sub(Vec3 a, Vec3 b)        { return V3(a.x-b.x, a.y-b.y, a.z-b.z); }
Vec3  vec3_scale(Vec3 a, float s)     { return V3(a.x*s, a.y*s, a.z*s); }
Vec3  vec3_mul(Vec3 a, Vec3 b)        { return V3(a.x*b.x, a.y*b.y, a.z*b.z); }
float vec3_len(Vec3 a)                { return sqrtf(a.x*a.x + a.y*a.y + a.z*a.z); }
float vec3_dot(Vec3 a, Vec3 b)        { return a.x*b.x + a.y*b.y + a.z*b.z; }

Vec3 vec3_norm(Vec3 a) {
    float l = vec3_len(a);
    return l == 0.0f ? V3(0, 0, 0) : vec3_scale(a, 1.0f / l);
}

Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return V3(a.y*b.z - a.z*b.y,
              a.z*b.x - a.x*b.z,
              a.x*b.y - a.y*b.x);
}

#endif /* UTILS_NO_VEC_MATH */

/* --------------------------------------------------------------------------
 * CLI Args
 * -------------------------------------------------------------------------- */

char *args_shift(int *argc, char ***argv) {
    if (*argc <= 0) return NULL;
    char *result = **argv;
    (*argc)--;
    (*argv)++;
    return result;
}

bool opts_parse(Opt *opts, size_t n_opts, int *argc, char ***argv) {
    int out = 0;
    for (int i = 0; i < *argc; ) {
        char *arg = (*argv)[i];

        if (strcmp(arg, "--") == 0) {
            i++;
            while (i < *argc) (*argv)[out++] = (*argv)[i++];
            break;
        }

        if (arg[0] != '-' || arg[1] == '\0') {
            (*argv)[out++] = arg;
            i++;
            continue;
        }

        bool        is_long  = (arg[1] == '-');
        const char *key      = is_long ? arg + 2 : arg + 1;
        const char *eq       = strchr(key, '=');
        size_t      key_len  = eq ? (size_t)(eq - key) : strlen(key);
        const char *attached = eq ? eq + 1 : NULL;

        /* -oVALUE: short opt with value glued to the flag */
        if (!is_long && !eq && key_len > 1) {
            key_len  = 1;
            attached = key + 1;
        }

        Opt *match = NULL;
        for (size_t j = 0; j < n_opts; j++) {
            Opt *o = &opts[j];
            if (is_long) {
                if (o->long_name && strlen(o->long_name) == key_len &&
                    strncmp(o->long_name, key, key_len) == 0) {
                    match = o; break;
                }
            } else {
                if (o->short_name && o->short_name == key[0]) {
                    match = o; break;
                }
            }
        }

        if (!match) {
            LOG(LOG_ERROR, "opts_parse: unknown option '%s'", arg);
            return false;
        }

        if (match->type == OPTTYPE_FLAG) {
            if (attached) {
                LOG(LOG_ERROR, "opts_parse: '%s' takes no argument", arg);
                return false;
            }
            *(bool *)match->dst = true;
            i++;
            continue;
        }

        const char *val = attached;
        if (!val) {
            if (++i >= *argc) {
                LOG(LOG_ERROR, "opts_parse: '%s' requires an argument", arg);
                return false;
            }
            val = (*argv)[i];
        }

        if (match->type == OPTTYPE_STR) {
            *(const char **)match->dst = val;
        } else {
            char *end;
            errno = 0;
            long v = strtol(val, &end, 10);
            if (*end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX) {
                LOG(LOG_ERROR, "opts_parse: '%s' expects an integer, got '%s'", arg, val);
                return false;
            }
            *(int *)match->dst = (int)v;
        }
        i++;
    }

    *argc = out;
    return true;
}

#define OPTS__HELP_COLUMN 36

/* The left column is built in a StringBuilder rather than a fixed buffer, so a
 * long option name wraps instead of being cut off. */
void opts_usage(FILE *fp, const char *program, const Opt *opts, size_t n_opts) {
    fprintf(fp, "Usage: %s [options] ...\n\nOptions:\n", program);

    StringBuilder left = {0};
    for (size_t i = 0; i < n_opts; i++) {
        const Opt *o = &opts[i];
        sb_reset(&left);

        if (o->short_name) sb_appendf(&left, "  -%c", o->short_name);
        else               sb_append(&left, "    ");

        if (o->short_name && o->long_name) sb_append(&left, ", ");
        else if (o->long_name)             sb_append(&left, "  ");

        if (o->long_name) {
            if (o->meta) sb_appendf(&left, "--%s=<%s>", o->long_name, o->meta);
            else         sb_appendf(&left, "--%s", o->long_name);
        } else if (o->meta) {
            sb_appendf(&left, " <%s>", o->meta);
        }

        const char *help = o->help ? o->help : "";
        if (left.count > OPTS__HELP_COLUMN)
            fprintf(fp, "%s\n%*s %s\n", sb_cstr(&left), OPTS__HELP_COLUMN, "", help);
        else
            fprintf(fp, "%-*s %s\n", OPTS__HELP_COLUMN, sb_cstr(&left), help);
    }
    sb_free(&left);
}

/* --------------------------------------------------------------------------
 * String Builder (internal helpers used by cmd_* below)
 * -------------------------------------------------------------------------- */

void sb_append_n(StringBuilder *sb, const char *str, size_t n) {
    da_reserve(sb, sb->count + n + 1);
    memcpy(sb->items + sb->count, str, n);
    sb->count += n;
}

void sb_append(StringBuilder *sb, const char *str) {
    sb_append_n(sb, str, strlen(str));
}

void sb_append_char(StringBuilder *sb, char c) {
    sb_append_n(sb, &c, 1);
}

void sb_appendf(StringBuilder *sb, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (n < 0) return;

    da_reserve(sb, sb->count + (size_t)n + 1);

    va_start(args, fmt);
    vsnprintf(sb->items + sb->count, (size_t)n + 1, fmt, args);
    va_end(args);
    sb->count += (size_t)n;
}

void sb_append_sv(StringBuilder *sb, String_View sv) {
    sb_append_n(sb, sv.data, sv.count);
}

char *sb_cstr(StringBuilder *sb) {
    da_reserve(sb, sb->count + 1);
    sb->items[sb->count] = '\0';
    return sb->items;
}

char *sb_to_string(StringBuilder *sb) {
    char *copy = malloc(sb->count + 1);
    if (!copy) PANIC("sb_to_string: malloc failed");
    memcpy(copy, sb->items, sb->count);
    copy[sb->count] = '\0';
    return copy;
}

void sb_reset(StringBuilder *sb) { sb->count = 0; }

void sb_free(StringBuilder *sb) {
    free(sb->items);
    sb->items    = NULL;
    sb->count    = 0;
    sb->capacity = 0;
}

/* --------------------------------------------------------------------------
 * Command Execution
 * -------------------------------------------------------------------------- */

void cmd_append(Cmd *c, const char *arg) {
    da_append(c, arg);
}

void cmd_extend(Cmd *c, ...) {
    va_list args;
    va_start(args, c);
    const char *arg;
    while ((arg = va_arg(args, const char *)) != NULL)
        da_append(c, arg);
    va_end(args);
}

/* Echoes the command about to run. Honours the log level and the colour
 * policy, so a quiet program stays quiet and a redirected build log stays
 * free of escape sequences. */
static void utils__cmd_log(Cmd *c) {
    if (LOG_INFO < UTILS__MIN_LEVEL) return;

    FILE *out   = UTILS__OUTPUT ? UTILS__OUTPUT : stderr;
    bool  color = utils__use_color(out);

    fprintf(out, "%s[CMD]%s", color ? "\x1b[35m" : "", color ? "\x1b[0m" : "");
    for (size_t i = 0; i < c->count; ++i) {
        bool needs_quote = (strchr(c->items[i], ' ') != NULL);
        fprintf(out, needs_quote ? " '%s'" : " %s", c->items[i]);
    }
    fputc('\n', out);
    fflush(out);
}

#ifdef _WIN32

static char *utils__cmd_to_cmdline(Cmd *c) {
    StringBuilder sb = {0};
    for (size_t i = 0; i < c->count; ++i) {
        bool has_space = (strchr(c->items[i], ' ') != NULL);
        if (has_space) sb_append(&sb, "\"");
        sb_append(&sb, c->items[i]);
        if (has_space) sb_append(&sb, "\"");
        if (i + 1 < c->count) sb_append(&sb, " ");
    }
    return sb_cstr(&sb);
}

Proc cmd_run_async(Cmd *c) {
    utils__cmd_log(c);
    char *cmdline = utils__cmd_to_cmdline(c);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0,
                        NULL, NULL, &si, &pi)) {
        LOG(LOG_ERROR, "cmd_run_async: CreateProcess failed (err=%lu)", GetLastError());
        free(cmdline);
        return INVALID_PROC;
    }
    CloseHandle(pi.hThread);
    free(cmdline);
    return pi.hProcess;
}

bool proc_wait(Proc p) {
    if (p == INVALID_PROC) return false;
    WaitForSingleObject(p, INFINITE);
    DWORD exit_code;
    GetExitCodeProcess(p, &exit_code);
    CloseHandle(p);
    if (exit_code != 0) {
        LOG(LOG_ERROR, "proc_wait: process exited with code %lu", exit_code);
        return false;
    }
    return true;
}

bool cmd_capture(Cmd *c, StringBuilder *sb) {
    HANDLE pipe_r, pipe_w;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&pipe_r, &pipe_w, &sa, 0)) {
        LOG(LOG_ERROR, "cmd_capture: CreatePipe failed");
        return false;
    }
    SetHandleInformation(pipe_r, HANDLE_FLAG_INHERIT, 0);

    char *cmdline = utils__cmd_to_cmdline(c);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    si.hStdOutput = pipe_w;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags    = STARTF_USESTDHANDLES;
    ZeroMemory(&pi, sizeof(pi));

    bool ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0,
                              NULL, NULL, &si, &pi);
    CloseHandle(pipe_w);
    free(cmdline);

    if (!ok) {
        LOG(LOG_ERROR, "cmd_capture: CreateProcess failed");
        CloseHandle(pipe_r);
        return false;
    }
    CloseHandle(pi.hThread);

    char buf[4096];
    DWORD nread;
    while (ReadFile(pipe_r, buf, sizeof(buf), &nread, NULL) && nread > 0)
        sb_append_n(sb, buf, nread);
    CloseHandle(pipe_r);

    return proc_wait(pi.hProcess);
}

#else /* POSIX */

Proc cmd_run_async(Cmd *c) {
    utils__cmd_log(c);

    /* execvp needs a NULL sentinel — temporarily append it. */
    da_append(c, NULL);
    pid_t pid = fork();
    c->count--;   /* remove the sentinel regardless of outcome */

    if (pid < 0) {
        LOG(LOG_ERROR, "cmd_run_async: fork failed: %s", strerror(errno));
        return INVALID_PROC;
    }
    if (pid == 0) {
        execvp(c->items[0], (char *const *)(void *)c->items);
        /* fprintf on a shared FILE* is unsafe after fork - use dprintf */
        if (LOG_ERROR >= UTILS__MIN_LEVEL)
            dprintf(STDERR_FILENO, "cmd_run_async: execvp '%s' failed: %s\n",
                    c->items[0], strerror(errno));
        _exit(127);
    }
    return pid;
}

bool proc_wait(Proc p) {
    if (p == INVALID_PROC) return false;
    int status;
    if (waitpid(p, &status, 0) < 0) {
        LOG(LOG_ERROR, "proc_wait: waitpid failed: %s", strerror(errno));
        return false;
    }
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code != 0) {
            LOG(LOG_ERROR, "proc_wait: process exited with code %d", code);
            return false;
        }
        return true;
    }
    if (WIFSIGNALED(status)) {
        LOG(LOG_ERROR, "proc_wait: process killed by signal %d", WTERMSIG(status));
        return false;
    }
    LOG(LOG_ERROR, "proc_wait: process ended unexpectedly");
    return false;
}

bool cmd_capture(Cmd *c, StringBuilder *sb) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        LOG(LOG_ERROR, "cmd_capture: pipe failed: %s", strerror(errno));
        return false;
    }

    da_append(c, NULL);
    pid_t pid = fork();
    c->count--;

    if (pid < 0) {
        LOG(LOG_ERROR, "cmd_capture: fork failed: %s", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execvp(c->items[0], (char *const *)(void *)c->items);
        if (LOG_ERROR >= UTILS__MIN_LEVEL)
            dprintf(STDERR_FILENO, "cmd_capture: execvp '%s' failed: %s\n",
                    c->items[0], strerror(errno));
        _exit(127);
    }

    close(pipefd[1]);

    char buf[4096];
    ssize_t nread;
    while ((nread = read(pipefd[0], buf, sizeof(buf))) > 0)
        sb_append_n(sb, buf, (size_t)nread);
    close(pipefd[0]);

    return proc_wait(pid);
}

#endif /* _WIN32 / POSIX */

bool cmd_run(Cmd *c) {
    Proc p = cmd_run_async(c);
    return proc_wait(p);
}

bool cmd_run_args(const char *first, ...) {
    Cmd cmd = {0};
    cmd_append(&cmd, first);

    va_list args;
    va_start(args, first);
    const char *arg;
    while ((arg = va_arg(args, const char *)) != NULL)
        cmd_append(&cmd, arg);
    va_end(args);

    bool ok = cmd_run(&cmd);
    cmd_free(&cmd);
    return ok;
}

void cmd_reset(Cmd *c) {
    c->count = 0;
}

void cmd_free(Cmd *c) {
    da_free(c);
}

/* --------------------------------------------------------------------------
 * Hash
 * -------------------------------------------------------------------------- */

uint32_t hash_bytes(const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    uint32_t h = 5381;
    for (size_t i = 0; i < len; ++i)
        h = ((h << 5) + h) ^ p[i];
    return h;
}

uint32_t hash_str(const char *s) {
    return hash_bytes(s, strlen(s));
}

uint32_t hash_mix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

/* --------------------------------------------------------------------------
 * HashMap
 * -------------------------------------------------------------------------- */

/* Unique address used to mark deleted (tombstone) slots.
 * Never equal to any real string pointer. */
static char HM__TOMB = 0;
#define HM__TOMBSTONE ((const char *)&HM__TOMB)

bool hm_entry_live(const HM_Entry *e) {
    return e->key != NULL && e->key != HM__TOMBSTONE;
}

/* Returns the index of the slot for key, or hm->capacity if not found.
 * When for_write is true, returns the first usable slot on a miss. */
static size_t hm__find_slot(const HashMap *hm, const char *key, bool for_write) {
    size_t mask        = hm->capacity - 1;
    size_t idx         = (size_t)hash_mix32(hash_str(key)) & mask;
    size_t tombstone   = hm->capacity;

    for (size_t i = 0; i < hm->capacity; ++i) {
        size_t probe    = (idx + i) & mask;
        const char *k   = hm->entries[probe].key;

        if (k == NULL) {
            if (for_write) return (tombstone < hm->capacity) ? tombstone : probe;
            return hm->capacity;
        }
        if (k == HM__TOMBSTONE) {
            if (for_write && tombstone == hm->capacity) tombstone = probe;
            continue;
        }
        if (strcmp(k, key) == 0) return probe;
    }

    return (for_write && tombstone < hm->capacity) ? tombstone : hm->capacity;
}

/* Rebuilds the table, dropping every tombstone on the way. The capacity only
 * doubles when the LIVE entries justify it, so a set/delete workload that keeps
 * a stable population rehashes in place instead of growing forever. */
static void hm__rehash(HashMap *hm) {
    size_t new_cap = 16;
    if (hm->capacity != 0)
        new_cap = (hm->count * 10 >= hm->capacity * 7) ? hm->capacity * 2
                                                       : hm->capacity;

    HM_Entry *new_entries = calloc(new_cap, sizeof(HM_Entry));
    if (!new_entries) PANIC("hm__rehash: calloc of %zu entries failed", new_cap);

    HashMap tmp = { .entries = new_entries, .count = 0, .used = 0,
                    .capacity = new_cap };

    for (size_t i = 0; i < hm->capacity; ++i) {
        if (!hm_entry_live(&hm->entries[i])) continue;
        size_t slot = hm__find_slot(&tmp, hm->entries[i].key, true);
        tmp.entries[slot] = hm->entries[i];
        tmp.count++;
    }
    tmp.used = tmp.count;

    free(hm->entries);
    *hm = tmp;
}

bool hm_set(HashMap *hm, const char *key, void *value) {
    /* Counting tombstones here is what keeps probe sequences short: they occupy
     * a slot just like a live entry as far as linear probing is concerned. */
    if ((hm->used + 1) * 10 >= hm->capacity * 7) hm__rehash(hm);

    size_t slot  = hm__find_slot(hm, key, true);
    bool   is_new = !hm_entry_live(&hm->entries[slot]);
    /* Reusing a tombstone adds a live entry without occupying a new slot. */
    if (is_new && hm->entries[slot].key == NULL) hm->used++;
    hm->entries[slot].key   = key;
    hm->entries[slot].value = value;
    if (is_new) hm->count++;
    return is_new;
}

void *hm_get(const HashMap *hm, const char *key) {
    if (hm->capacity == 0) return NULL;
    size_t slot = hm__find_slot(hm, key, false);
    if (slot == hm->capacity) return NULL;
    return hm->entries[slot].value;
}

bool hm_has(const HashMap *hm, const char *key) {
    if (hm->capacity == 0) return false;
    return hm__find_slot(hm, key, false) != hm->capacity;
}

bool hm_delete(HashMap *hm, const char *key) {
    if (hm->capacity == 0) return false;
    size_t slot = hm__find_slot(hm, key, false);
    if (slot == hm->capacity) return false;
    hm->entries[slot].key   = HM__TOMBSTONE;
    hm->entries[slot].value = NULL;
    hm->count--;   /* `used` stays: the slot is still occupied by the tombstone */
    return true;
}

void hm_free(HashMap *hm) {
    free(hm->entries);
    hm->entries  = NULL;
    hm->count    = 0;
    hm->used     = 0;
    hm->capacity = 0;
}

/* --------------------------------------------------------------------------
 * Path Utilities
 * -------------------------------------------------------------------------- */

static bool path__is_sep(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

const char *path_basename(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; ++p)
        if (path__is_sep(*p) && *(p + 1)) base = p + 1;
    return base;
}

const char *path_ext(const char *path) {
    const char *base = path_basename(path);
    const char *dot  = strrchr(base, '.');
    return dot ? dot : path + strlen(path);
}

/* Every write is bounded by bufsz; a bufsz of 0 leaves buf untouched. The
 * previous "bufsz - 1" arithmetic wrapped around on an empty buffer. */
char *path_dirname(const char *path, char *buf, size_t bufsz) {
    if (bufsz == 0) return buf;

    const char *base = path_basename(path);
    size_t      len  = (size_t)(base - path);

    /* Drop the trailing separators, but keep a lone root "/". */
    while (len > 1 && path__is_sep(path[len - 1])) len--;

    if (len == 0) {
        if (bufsz >= 2) { buf[0] = '.'; buf[1] = '\0'; }
        else            { buf[0] = '\0'; }
        return buf;
    }

    size_t n = len < bufsz - 1 ? len : bufsz - 1;
    memcpy(buf, path, n);
    buf[n] = '\0';
    return buf;
}

char *path_join(char *buf, size_t bufsz, const char *a, const char *b) {
    if (bufsz == 0) return buf;

    size_t a_len   = strlen(a);
    size_t written = a_len < bufsz - 1 ? a_len : bufsz - 1;
    memcpy(buf, a, written);

    while (path__is_sep(*b)) b++;

    if (*b && written > 0 && written < bufsz - 1 && !path__is_sep(buf[written - 1]))
        buf[written++] = PATH_SEP;

    size_t b_len = strlen(b);
    size_t avail = bufsz - 1 - written;
    size_t n     = b_len < avail ? b_len : avail;
    memcpy(buf + written, b, n);
    buf[written + n] = '\0';
    return buf;
}

bool path_is_absolute(const char *path) {
#ifdef _WIN32
    return (path[0] && path[1] == ':') || path__is_sep(path[0]);
#else
    return path[0] == '/';
#endif
}

#endif /* UTILS_IMPLEMENTATION */