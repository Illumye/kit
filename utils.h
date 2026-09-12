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
 *   2.  Files & Filesystem
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
#include <stddef.h>
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
#    include <dirent.h>
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

/* Which fields precede the message. Combine with '|'.
 *
 *   log_set_fields(LOG_FIELD_DATE | LOG_FIELD_TIME | LOG_FIELD_USER);
 *   LOG(LOG_INFO, "started");
 *   -> [2026-09-12 (Sat)] [14:05:42] [illumye] started
 *
 * LOG_FIELD_COUNT numbers the records as they are emitted, which is how a
 * burst of identical lines stops looking like a stuck program. */
typedef enum {
    LOG_FIELD_TIME     = 1u << 0,   /* 14:05:42                */
    LOG_FIELD_DATE     = 1u << 1,   /* 2026-09-12 (Sat)        */
    LOG_FIELD_LEVEL    = 1u << 2,   /* INFO                    */
    LOG_FIELD_LOCATION = 1u << 3,   /* utils.h:42              */
    LOG_FIELD_USER     = 1u << 4,   /* from LOGNAME/USER       */
    LOG_FIELD_COUNT    = 1u << 5    /* record number           */
} LogField;

#define LOG_FIELDS_DEFAULT (LOG_FIELD_TIME | LOG_FIELD_LEVEL | LOG_FIELD_LOCATION)
#define LOG_FIELDS_ALL     (LOG_FIELD_TIME | LOG_FIELD_DATE | LOG_FIELD_LEVEL | \
                            LOG_FIELD_LOCATION | LOG_FIELD_USER | LOG_FIELD_COUNT)
#define LOG_FIELDS_NONE    0u

void log_set_level(LogLevel level);
void log_set_output(FILE *fp);   /* NULL resets to stderr (default) */
void log_set_color(LogColorMode mode);
void log_set_fields(unsigned fields);

void utils_log_impl(LogLevel level, const char *file, int line,
                    const char *fmt, ...) UTILS_PRINTF_FORMAT(4, 5);
UTILS_NORETURN void utils_panic_impl(const char *file, int line,
                                     const char *fmt, ...) UTILS_PRINTF_FORMAT(3, 4);

#define LOG(level, ...)  utils_log_impl(level, __FILE__, __LINE__, __VA_ARGS__)
#define PANIC(...)       utils_panic_impl(__FILE__, __LINE__, __VA_ARGS__)
#define TODO(msg)        PANIC("TODO: %s", msg)
#define UNREACHABLE(msg) PANIC("UNREACHABLE: %s", msg)

/* --------------------------------------------------------------------------
 * SECTION 2 : FILES & FILESYSTEM
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

/* What lives at path. FILE_KIND_NONE means nothing does, which is not an
 * error: use it to test existence regardless of the kind. */
typedef enum {
    FILE_KIND_NONE,
    FILE_KIND_REGULAR,
    FILE_KIND_DIRECTORY,
    FILE_KIND_OTHER      /* symlink target that is neither, device, socket... */
} FileKind;

FileKind file_kind(const char *path);
bool     dir_exists(const char *path);

/* Creates a directory and every missing parent, like `mkdir -p`.
 * Succeeds when the directory already exists. */
bool mkdir_p(const char *path);

/* Copies src over dst, creating or truncating it. On POSIX the permission
 * bits of the source are carried over. Returns false on error. */
bool copy_file(const char *src, const char *dst);

/* Removes a file. Returns false when it did not exist, which is why an
 * idempotent caller should test with file_exists first. */
bool remove_file(const char *path);

/* Renames or moves a file, replacing dst if it exists. Both paths must sit on
 * the same filesystem. */
bool rename_file(const char *from, const char *to);

/* Last modification time, in whole seconds since the Unix epoch, or -1.
 * needs_rebuild compares at the finest resolution the platform exposes,
 * which is why it does not go through this function. */
int64_t file_mtime(const char *path);

/* A list of owned, NUL-terminated paths. Works with the da_* macros. */
typedef struct {
    char  **items;
    size_t  count;
    size_t  capacity;
} FileList;

void file_list_free(FileList *list);

/* Appends the entries of a directory to `out`, excluding "." and "..".
 * Names only, not full paths. Sorted with strcmp, so a build driven from the
 * result is reproducible. Returns false on error, leaving `out` untouched. */
bool read_dir(const char *path, FileList *out);

/* Is `output` stale with respect to its inputs?
 *
 *   1  rebuild needed: output is missing, or an input is at least as recent
 *   0  output is up to date
 *  -1  error, already logged: an input is missing or unreadable
 *
 * The tri-state is the whole point. A bool would force a missing input to be
 * reported as "up to date", which silently skips the build step.
 *
 *   if (needs_rebuild(exe, srcs, n) != 0) { ... rebuild ... }
 */
int needs_rebuild(const char *output, const char **inputs, size_t n_inputs);

/* Same, for a single input. */
#define needs_rebuild1(output, input) \
    needs_rebuild((output), (const char *[]){ (input) }, 1)

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

/* Returned by the search functions when there is no match. */
#define SV_NPOS ((size_t)-1)

String_View sv_from_cstr(const char *cstr);
String_View sv_from_parts(const char *data, size_t count);

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

/* ASCII only: the library does no locale or Unicode case folding. */
bool sv_eq_ignorecase(String_View a, String_View b);

/* Search. Both return SV_NPOS when there is no match; an empty needle matches
 * at 0, which is what makes "starts with nothing" true. */
size_t sv_index_of(String_View sv, char c);
size_t sv_index_of_sv(String_View sv, String_View needle);
bool   sv_contains(String_View sv, String_View needle);

/* Consume up to a multi-character delimiter, the counterpart of
 * sv_chop_by_delim. When the delimiter is absent the whole view is returned
 * and *sv is left empty. */
String_View sv_chop_by_sv(String_View *sv, String_View delim);

/* Consume n characters from the right. */
String_View sv_chop_right(String_View *sv, size_t n);

/* Loop form of sv_chop_by_delim: writes the next field into *out and returns
 * false once the view is exhausted.
 *
 *   String_View field;
 *   while (sv_try_chop_by_delim(&line, ',', &field)) { ... }
 *
 * An empty view yields nothing, and a trailing delimiter does not produce an
 * extra empty field: both "a,b" and "a,b," yield "a" then "b". Telling those
 * two apart would mean hiding a state bit inside the String_View, which is a
 * worse trade than the simpler contract. A caller that needs the distinction
 * can test the input with sv_ends_with_cstr first. Delimiters in the middle
 * are never collapsed, so "a,,b" does yield an empty middle field. */
bool sv_try_chop_by_delim(String_View *sv, char delim, String_View *out);

/* Strict numeric parsing: the entire view must be consumed, leading and
 * trailing spaces included, or the call fails and *out is untouched.
 * Overflow is a failure, not a saturation. */
bool sv_to_i64(String_View sv, int64_t *out);
bool sv_to_u64(String_View sv, uint64_t *out);
bool sv_to_double(String_View sv, double *out);

/* A malloc'd, NUL-terminated copy. The caller owns it. */
char *sv_to_cstr(String_View sv);

/* --------------------------------------------------------------------------
 * SECTION 5 : ARENA ALLOCATOR
 * -------------------------------------------------------------------------- */

/* A bump allocator over a chain of regions. Running out of room grows the
 * chain instead of failing, so the initial size is a hint, not a ceiling.
 *
 *   Arena a = {0};                  // grows on demand
 *   Arena b = arena_make(1 << 20);  // same, with the first region preallocated
 *
 * Every allocation is zeroed. Individual blocks are never freed; the whole
 * arena is reset or released at once. */

#ifndef ARENA_REGION_SIZE
#define ARENA_REGION_SIZE (64 * 1024)
#endif

typedef struct Arena_Region Arena_Region;
struct Arena_Region {
    Arena_Region *next;
    size_t        capacity;
    size_t        used;
    char          data[];   /* the payload follows the header */
};

typedef struct {
    Arena_Region *first;
    Arena_Region *current;
    size_t        region_size;   /* hint for the regions allocated next */
} Arena;

/* Preallocates a first region of `size` bytes and uses it as the growth hint.
 * A zero-initialised Arena behaves identically, minus the preallocation. */
Arena arena_make(size_t size);

/* Aligned on max_align_t, which suits every standard type. */
void *arena_alloc(Arena *a, size_t size);

/* For over-aligned types: SIMD vectors, cache-line padding. `align` must be a
 * power of two. */
void *arena_alloc_aligned(Arena *a, size_t size, size_t align);

/* Convenience: arena_alloc_array(arena, T, n) allocates n items of type T. */
#define arena_alloc_array(a, T, n) ((T *)arena_alloc((a), sizeof(T) * (n)))

/* Copies into the arena. The result is NUL-terminated and owned by the arena,
 * so it must not be freed individually. */
char *arena_strdup(Arena *a, const char *s);
char *arena_strdup_n(Arena *a, const char *s, size_t n);
char *arena_sprintf(Arena *a, const char *fmt, ...) UTILS_PRINTF_FORMAT(2, 3);

/* Bytes handed out, and bytes held. The gap is alignment padding plus the
 * tail of every region that was left behind when the chain grew. */
size_t arena_used(const Arena *a);
size_t arena_capacity(const Arena *a);

/* Frees everything at once but keeps the regions for reuse. */
void arena_reset(Arena *a);

/* Releases every region back to the allocator. */
void arena_free(Arena *a);

/* A position in the arena, to roll back to. Taking a mark and rewinding to it
 * turns the arena into a stack: allocate freely, then release in one step.
 * A mark is invalidated by arena_reset and arena_free. */
typedef struct {
    Arena_Region *region;
    size_t        used;
} Arena_Mark;

Arena_Mark arena_mark(const Arena *a);
void       arena_rewind(Arena *a, Arena_Mark mark);

/* --------------------------------------------------------------------------
 * SECTION 5b : TEMPORARY ALLOCATOR
 *
 * A process-wide scratch arena for strings that live until the end of the
 * current step: a path being assembled, a formatted message, a command line.
 * Nothing here is ever freed individually.
 *
 *   const char *out = temp_sprintf("%s/%s.o", build_dir, name);
 *   cmd_append(&cmd, out);
 *   ...
 *   temp_reset();   // once the step is over
 *
 * Reclaim with temp_reset between iterations, or with a mark for nesting:
 *
 *   Arena_Mark m = temp_mark();
 *   ... temp_sprintf ...
 *   temp_rewind(m);
 *
 * The arena is global, so none of this is thread-safe. A thread that needs
 * scratch space should carry its own Arena.
 * -------------------------------------------------------------------------- */

void  *temp_alloc(size_t size);
char  *temp_strdup(const char *s);
char  *temp_sprintf(const char *fmt, ...) UTILS_PRINTF_FORMAT(1, 2);

Arena_Mark temp_mark(void);
void       temp_rewind(Arena_Mark mark);
void       temp_reset(void);

/* Releases the scratch memory to the allocator. Rarely needed: a program that
 * calls temp_reset already reuses the same regions forever. */
void       temp_free(void);

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
 *   -v            flag
 *   -vn           grouped flags, same as -v -n
 *   -o file       short with separate value
 *   -ofile        short with attached value
 *   -o=file       short with '=' separator
 *   -vofile       a group ending on a value option
 *   --output file
 *   --output=file
 *   --            ends option parsing; remaining args are positional
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

/* Drops every entry but keeps the allocation, ready for reuse. */
void  hm_reset(HashMap *hm);

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
static unsigned      UTILS__FIELDS    = LOG_FIELDS_DEFAULT;
static uint64_t      UTILS__RECORDS   = 0;

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

void log_set_fields(unsigned fields) {
    UTILS__FIELDS = fields;
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
static void utils__strftime_now(char *buf, size_t bufsz, const char *fmt) {
    time_t t = time(NULL);
    struct tm tm_buf;
    struct tm *tm_info;
#ifdef _WIN32
    tm_info = (localtime_s(&tm_buf, &t) == 0) ? &tm_buf : NULL;
#else
    tm_info = localtime_r(&t, &tm_buf);
#endif
    if (!tm_info || strftime(buf, bufsz, fmt, tm_info) == 0)
        snprintf(buf, bufsz, "?");
}

/* The name the shell reports, not the account the process runs as: a build
 * log is read by a person, and sudo should not rewrite the author. */
static const char *utils__username(void) {
#ifdef _WIN32
    const char *name = getenv("USERNAME");
#else
    const char *name = getenv("LOGNAME");
    if (!name || !*name) name = getenv("USER");
#endif
    return (name && *name) ? name : "unknown";
}

static void utils__log_prefix(FILE *out, const char *color, const char *label,
                              const char *file, int line) {
    unsigned    fields = UTILS__FIELDS;
    bool        color_on = utils__use_color(out);
    const char *on  = color_on ? color        : "";
    const char *dim = color_on ? "\x1b[90m"   : "";
    const char *off = color_on ? "\x1b[0m"    : "";

    if (fields & LOG_FIELD_COUNT)
        fprintf(out, "%s[%llu]%s ", dim, (unsigned long long)++UTILS__RECORDS, off);

    if (fields & LOG_FIELD_DATE) {
        char buf[32];
        utils__strftime_now(buf, sizeof(buf), "%Y-%m-%d (%a)");
        fprintf(out, "%s[%s]%s ", on, buf, off);
    }

    if (fields & LOG_FIELD_TIME) {
        char buf[16];
        utils__strftime_now(buf, sizeof(buf), "%H:%M:%S");
        fprintf(out, "%s[%s]%s ", on, buf, off);
    }

    if (fields & LOG_FIELD_LEVEL)
        fprintf(out, "%s[%s]%s ", on, label, off);

    if (fields & LOG_FIELD_USER)
        fprintf(out, "%s[%s]%s ", dim, utils__username(), off);

    if (fields & LOG_FIELD_LOCATION)
        fprintf(out, "%s[%s:%d]%s ", dim, file, line, off);
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

bool sv_eq_ignorecase(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; ++i) {
        int ca = tolower((unsigned char)a.data[i]);
        int cb = tolower((unsigned char)b.data[i]);
        if (ca != cb) return false;
    }
    return true;
}

size_t sv_index_of(String_View sv, char c) {
    for (size_t i = 0; i < sv.count; ++i)
        if (sv.data[i] == c) return i;
    return SV_NPOS;
}

size_t sv_index_of_sv(String_View sv, String_View needle) {
    if (needle.count == 0)      return 0;
    if (needle.count > sv.count) return SV_NPOS;

    for (size_t i = 0; i + needle.count <= sv.count; ++i)
        if (memcmp(sv.data + i, needle.data, needle.count) == 0) return i;
    return SV_NPOS;
}

bool sv_contains(String_View sv, String_View needle) {
    return sv_index_of_sv(sv, needle) != SV_NPOS;
}

String_View sv_chop_by_sv(String_View *sv, String_View delim) {
    size_t at = sv_index_of_sv(*sv, delim);
    if (at == SV_NPOS) {
        String_View all = *sv;
        sv->data  += sv->count;
        sv->count  = 0;
        return all;
    }

    String_View head = { .data = sv->data, .count = at };
    sv->data  += at + delim.count;
    sv->count -= at + delim.count;
    return head;
}

String_View sv_chop_right(String_View *sv, size_t n) {
    if (n > sv->count) n = sv->count;
    String_View tail = { .data = sv->data + sv->count - n, .count = n };
    sv->count -= n;
    return tail;
}

bool sv_try_chop_by_delim(String_View *sv, char delim, String_View *out) {
    if (sv->count == 0) return false;
    *out = sv_chop_by_delim(sv, delim);
    return true;
}

/* Shared by the integer parsers: consumes digits and reports overflow against
 * the caller's ceiling, so the signed and unsigned limits are both honoured
 * without ever computing an out-of-range value. */
static bool sv__parse_digits(String_View sv, uint64_t limit, uint64_t *out) {
    if (sv.count == 0) return false;

    uint64_t acc = 0;
    for (size_t i = 0; i < sv.count; ++i) {
        unsigned char c = (unsigned char)sv.data[i];
        if (c < '0' || c > '9') return false;

        uint64_t digit = (uint64_t)(c - '0');
        if (acc > (limit - digit) / 10) return false;   /* would overflow */
        acc = acc * 10 + digit;
    }
    *out = acc;
    return true;
}

bool sv_to_u64(String_View sv, uint64_t *out) {
    if (sv.count > 0 && sv.data[0] == '+') { sv.data += 1; sv.count -= 1; }
    uint64_t value;
    if (!sv__parse_digits(sv, UINT64_MAX, &value)) return false;
    *out = value;
    return true;
}

bool sv_to_i64(String_View sv, int64_t *out) {
    bool negative = false;
    if (sv.count > 0 && (sv.data[0] == '-' || sv.data[0] == '+')) {
        negative = (sv.data[0] == '-');
        sv.data  += 1;
        sv.count -= 1;
    }

    /* The magnitude of INT64_MIN is one past INT64_MAX, hence the two limits. */
    uint64_t limit = negative ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX;
    uint64_t value;
    if (!sv__parse_digits(sv, limit, &value)) return false;

    *out = negative ? (int64_t)(~value + 1) : (int64_t)value;
    return true;
}

bool sv_to_double(String_View sv, double *out) {
    /* strtod needs a terminator, and a real number never needs many digits. */
    char buf[64];
    if (sv.count == 0 || sv.count >= sizeof(buf)) return false;
    memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';

    char *end;
    errno = 0;
    double value = strtod(buf, &end);
    if (end != buf + sv.count || errno == ERANGE) return false;

    *out = value;
    return true;
}

char *sv_to_cstr(String_View sv) {
    char *copy = malloc(sv.count + 1);
    if (!copy) PANIC("sv_to_cstr: malloc of %zu bytes failed", sv.count + 1);
    if (sv.count > 0) memcpy(copy, sv.data, sv.count);
    copy[sv.count] = '\0';
    return copy;
}

/* --------------------------------------------------------------------------
 * Filesystem
 * -------------------------------------------------------------------------- */

/* Defined with the path helpers further down; needed here by mkdir_p. */
static bool path__is_sep(char c);

FileKind file_kind(const char *path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES)      return FILE_KIND_NONE;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)      return FILE_KIND_DIRECTORY;
    return FILE_KIND_REGULAR;
#else
    struct stat st;
    if (stat(path, &st) != 0)                 return FILE_KIND_NONE;
    if (S_ISREG(st.st_mode))                  return FILE_KIND_REGULAR;
    if (S_ISDIR(st.st_mode))                  return FILE_KIND_DIRECTORY;
    return FILE_KIND_OTHER;
#endif
}

bool dir_exists(const char *path) {
    return file_kind(path) == FILE_KIND_DIRECTORY;
}

/* Creates one component. An existing directory is a success, which is what
 * makes mkdir_p idempotent. */
static bool utils__mkdir_one(const char *path) {
#ifdef _WIN32
    if (CreateDirectoryA(path, NULL)) return true;
    if (GetLastError() == ERROR_ALREADY_EXISTS) return dir_exists(path);
    LOG(LOG_ERROR, "mkdir_p: cannot create '%s' (err=%lu)", path, GetLastError());
    return false;
#else
    if (mkdir(path, 0777) == 0) return true;
    if (errno == EEXIST) {
        if (dir_exists(path)) return true;
        LOG(LOG_ERROR, "mkdir_p: '%s' exists and is not a directory", path);
        return false;
    }
    LOG(LOG_ERROR, "mkdir_p: cannot create '%s': %s", path, strerror(errno));
    return false;
#endif
}

bool mkdir_p(const char *path) {
    if (!path || !*path) {
        LOG(LOG_ERROR, "mkdir_p: empty path");
        return false;
    }

    bool          result = true;
    StringBuilder sb     = {0};
    const char   *p      = path;

    /* Carry the leading separators over verbatim so that an absolute path
     * stays absolute and a UNC prefix survives. */
    while (path__is_sep(*p)) sb_append_char(&sb, *p++);

    while (*p) {
        const char *start = p;
        while (*p && !path__is_sep(*p)) p++;
        sb_append_n(&sb, start, (size_t)(p - start));
        while (path__is_sep(*p)) p++;

        const char *so_far = sb_cstr(&sb);
        /* A bare Windows drive ("C:") is not a directory anyone can create. */
        bool is_drive = (sb.count == 2 && so_far[1] == ':');
        if (!is_drive && !utils__mkdir_one(so_far)) return_defer(false);

        if (*p) sb_append_char(&sb, PATH_SEP);
    }

defer:
    sb_free(&sb);
    return result;
}

bool copy_file(const char *src, const char *dst) {
#ifdef _WIN32
    if (CopyFileA(src, dst, FALSE)) return true;
    LOG(LOG_ERROR, "copy_file: '%s' -> '%s' failed (err=%lu)",
        src, dst, GetLastError());
    return false;
#else
    bool result = true;
    int  in = -1, out = -1;

    in = open(src, O_RDONLY);
    if (in < 0) {
        LOG(LOG_ERROR, "copy_file: cannot open '%s': %s", src, strerror(errno));
        return_defer(false);
    }

    struct stat st;
    if (fstat(in, &st) != 0) {
        LOG(LOG_ERROR, "copy_file: cannot stat '%s': %s", src, strerror(errno));
        return_defer(false);
    }

    /* The mode is applied at creation rather than after, so the file is never
     * briefly visible with wider permissions than the source. */
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
    if (out < 0) {
        LOG(LOG_ERROR, "copy_file: cannot open '%s': %s", dst, strerror(errno));
        return_defer(false);
    }

    char buf[UTILS_READ_CHUNK];
    for (;;) {
        ssize_t n = read(in, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0) {
            if (errno == EINTR) continue;
            LOG(LOG_ERROR, "copy_file: read '%s': %s", src, strerror(errno));
            return_defer(false);
        }
        for (ssize_t off = 0; off < n; ) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if (w < 0) {
                if (errno == EINTR) continue;
                LOG(LOG_ERROR, "copy_file: write '%s': %s", dst, strerror(errno));
                return_defer(false);
            }
            off += w;
        }
    }

defer:
    if (in >= 0) close(in);
    /* close() is where a deferred write error surfaces, so it is checked. */
    if (out >= 0 && close(out) != 0 && result) {
        LOG(LOG_ERROR, "copy_file: cannot flush '%s': %s", dst, strerror(errno));
        result = false;
    }
    return result;
#endif
}

bool remove_file(const char *path) {
    if (remove(path) == 0) return true;
    LOG(LOG_ERROR, "remove_file: cannot remove '%s': %s", path, strerror(errno));
    return false;
}

bool rename_file(const char *from, const char *to) {
#ifdef _WIN32
    /* Plain rename() refuses an existing destination on Windows. */
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING)) return true;
    LOG(LOG_ERROR, "rename_file: '%s' -> '%s' failed (err=%lu)",
        from, to, GetLastError());
    return false;
#else
    if (rename(from, to) == 0) return true;
    LOG(LOG_ERROR, "rename_file: '%s' -> '%s': %s", from, to, strerror(errno));
    return false;
#endif
}

/* Modification time at the finest resolution the platform exposes. A build
 * driven by whole seconds misses a rebuild whenever an input and its output
 * are written inside the same second, which is common on a fast machine. */
typedef struct {
    int64_t sec;
    int32_t nsec;
} Utils__Mtime;

static bool utils__mtime(const char *path, Utils__Mtime *out) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) return false;
    ULONGLONG ticks = ((ULONGLONG)info.ftLastWriteTime.dwHighDateTime << 32) |
                       info.ftLastWriteTime.dwLowDateTime;
    /* FILETIME counts 100 ns intervals from 1601-01-01. */
    ULONGLONG since_epoch = ticks - 116444736000000000ULL;
    out->sec  = (int64_t)(since_epoch / 10000000ULL);
    out->nsec = (int32_t)((since_epoch % 10000000ULL) * 100ULL);
    return true;
#else
    struct stat st;
    if (stat(path, &st) != 0) return false;
    out->sec = (int64_t)st.st_mtime;
#    if defined(__APPLE__)
    out->nsec = (int32_t)st.st_mtimespec.tv_nsec;
#    elif defined(st_mtime)  /* POSIX.1-2008 names the field st_mtim */
    out->nsec = (int32_t)st.st_mtim.tv_nsec;
#    else
    out->nsec = 0;
#    endif
    return true;
#endif
}

int64_t file_mtime(const char *path) {
    Utils__Mtime t;
    if (!utils__mtime(path, &t)) {
        LOG(LOG_ERROR, "file_mtime: cannot stat '%s': %s", path, strerror(errno));
        return -1;
    }
    return t.sec;
}

int needs_rebuild(const char *output, const char **inputs, size_t n_inputs) {
    Utils__Mtime out_time;
    if (!utils__mtime(output, &out_time)) return 1;   /* missing: must build */

    for (size_t i = 0; i < n_inputs; ++i) {
        Utils__Mtime in_time;
        if (!utils__mtime(inputs[i], &in_time)) {
            LOG(LOG_ERROR, "needs_rebuild: input '%s' is unreadable: %s",
                inputs[i], strerror(errno));
            return -1;
        }
        /* ">=" and not ">": same-timestamp means the ordering is unknown, and
         * rebuilding needlessly is cheaper than shipping a stale artifact. */
        if (in_time.sec  >  out_time.sec) return 1;
        if (in_time.sec  == out_time.sec && in_time.nsec >= out_time.nsec) return 1;
    }
    return 0;
}

void file_list_free(FileList *list) {
    for (size_t i = 0; i < list->count; ++i) free(list->items[i]);
    da_free(list);
}

static int utils__cmp_cstr(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static char *utils__strdup(const char *s) {
    size_t n    = strlen(s) + 1;
    char  *copy = malloc(n);
    if (!copy) PANIC("read_dir: out of memory");
    memcpy(copy, s, n);
    return copy;
}

bool read_dir(const char *path, FileList *out) {
    /* Entries land in a scratch list first, so a failure halfway through
     * leaves the caller's list exactly as it was. */
    FileList found = {0};
    bool     result = true;

#ifdef _WIN32
    char pattern[MAX_PATH];
    if (snprintf(pattern, sizeof(pattern), "%s\\*", path) >= (int)sizeof(pattern)) {
        LOG(LOG_ERROR, "read_dir: path too long: '%s'", path);
        return false;
    }

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        LOG(LOG_ERROR, "read_dir: cannot open '%s' (err=%lu)", path, GetLastError());
        return false;
    }
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        da_append(&found, utils__strdup(fd.cFileName));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *dir = opendir(path);
    if (!dir) {
        LOG(LOG_ERROR, "read_dir: cannot open '%s': %s", path, strerror(errno));
        return false;
    }

    errno = 0;
    for (struct dirent *e = readdir(dir); e != NULL; e = readdir(dir)) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        da_append(&found, utils__strdup(e->d_name));
        errno = 0;
    }
    /* readdir returns NULL both at the end and on failure; errno tells them
     * apart, which is why it is cleared before each call. */
    if (errno != 0) {
        LOG(LOG_ERROR, "read_dir: error reading '%s': %s", path, strerror(errno));
        result = false;
    }
    closedir(dir);
#endif

    if (!result) {
        file_list_free(&found);
        return false;
    }

    if (found.count > 1)
        qsort(found.items, found.count, sizeof(*found.items), utils__cmp_cstr);

    da_append_many(out, found.items, found.count);
    da_free(&found);   /* the names themselves now belong to `out` */
    return true;
}

/* --------------------------------------------------------------------------
 * String Views
 * -------------------------------------------------------------------------- */

String_View sv_from_cstr(const char *cstr) {
    return (String_View){ .data = cstr, .count = strlen(cstr) };
}

String_View sv_from_parts(const char *data, size_t count) {
    return (String_View){ .data = data, .count = count };
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

/* Regions carry their payload in the same allocation as their header, so a
 * region costs one malloc. */
static Arena_Region *arena__new_region(size_t capacity) {
    if (capacity > SIZE_MAX - sizeof(Arena_Region))
        PANIC("arena: region of %zu bytes is too large", capacity);

    Arena_Region *r = malloc(sizeof(Arena_Region) + capacity);
    if (!r) PANIC("arena: malloc of %zu bytes failed", capacity);

    r->next     = NULL;
    r->capacity = capacity;
    r->used     = 0;
    return r;
}

static void arena__append_region(Arena *a, size_t min_capacity) {
    size_t hint     = a->region_size ? a->region_size : (size_t)ARENA_REGION_SIZE;
    size_t capacity = min_capacity > hint ? min_capacity : hint;

    Arena_Region *r = arena__new_region(capacity);
    if (a->current) a->current->next = r;
    else            a->first         = r;
    a->current = r;
}

Arena arena_make(size_t size) {
    Arena a = { .first = NULL, .current = NULL, .region_size = size };
    if (size > 0) arena__append_region(&a, size);
    return a;
}

void *arena_alloc_aligned(Arena *a, size_t size, size_t align) {
    if (align == 0 || (align & (align - 1)) != 0)
        PANIC("arena_alloc_aligned: alignment %zu is not a power of two", align);
    if (size > SIZE_MAX - align)
        PANIC("arena_alloc_aligned: request of %zu bytes is too large", size);

    for (;;) {
        Arena_Region *r = a->current;
        if (r) {
            /* Padding is computed from the absolute address, so the alignment
             * of the region payload itself does not matter. */
            uintptr_t base = (uintptr_t)(r->data + r->used);
            size_t    pad  = (size_t)((~base + 1u) & (align - 1));

            if (pad <= r->capacity - r->used &&
                size <= r->capacity - r->used - pad) {
                r->used += pad;
                void *ptr = r->data + r->used;
                r->used  += size;
                memset(ptr, 0, size);
                return ptr;
            }
            /* This region is full. Reuse the next one if the chain already
             * has it, which is what makes arena_reset cheap. */
            if (r->next) {
                a->current = r->next;
                continue;
            }
        }
        arena__append_region(a, size + align);
    }
}

void *arena_alloc(Arena *a, size_t size) {
    return arena_alloc_aligned(a, size, sizeof(max_align_t));
}

char *arena_strdup_n(Arena *a, const char *s, size_t n) {
    char *copy = arena_alloc_aligned(a, n + 1, 1);
    if (n > 0) memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}

char *arena_strdup(Arena *a, const char *s) {
    return arena_strdup_n(a, s, strlen(s));
}

char *arena_sprintf(Arena *a, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (n < 0) PANIC("arena_sprintf: encoding error");

    char *out = arena_alloc_aligned(a, (size_t)n + 1, 1);
    va_start(args, fmt);
    vsnprintf(out, (size_t)n + 1, fmt, args);
    va_end(args);
    return out;
}

size_t arena_used(const Arena *a) {
    size_t total = 0;
    for (const Arena_Region *r = a->first; r; r = r->next) {
        total += r->used;
        if (r == a->current) break;   /* regions past current are not in use */
    }
    return total;
}

size_t arena_capacity(const Arena *a) {
    size_t total = 0;
    for (const Arena_Region *r = a->first; r; r = r->next) total += r->capacity;
    return total;
}

void arena_reset(Arena *a) {
    for (Arena_Region *r = a->first; r; r = r->next) r->used = 0;
    a->current = a->first;
}

void arena_free(Arena *a) {
    Arena_Region *r = a->first;
    while (r) {
        Arena_Region *next = r->next;
        free(r);
        r = next;
    }
    a->first       = NULL;
    a->current     = NULL;
    a->region_size = 0;
}

Arena_Mark arena_mark(const Arena *a) {
    Arena_Mark m = { .region = a->current, .used = a->current ? a->current->used : 0 };
    return m;
}

void arena_rewind(Arena *a, Arena_Mark mark) {
    if (!mark.region) {
        arena_reset(a);
        return;
    }
    mark.region->used = mark.used;
    for (Arena_Region *r = mark.region->next; r; r = r->next) r->used = 0;
    a->current = mark.region;
}

/* --------------------------------------------------------------------------
 * Temporary allocator
 * -------------------------------------------------------------------------- */

static Arena UTILS__TEMP = {0};

void *temp_alloc(size_t size) {
    return arena_alloc(&UTILS__TEMP, size);
}

char *temp_strdup(const char *s) {
    return arena_strdup(&UTILS__TEMP, s);
}

char *temp_sprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (n < 0) PANIC("temp_sprintf: encoding error");

    char *out = arena_alloc_aligned(&UTILS__TEMP, (size_t)n + 1, 1);
    va_start(args, fmt);
    vsnprintf(out, (size_t)n + 1, fmt, args);
    va_end(args);
    return out;
}

Arena_Mark temp_mark(void)             { return arena_mark(&UTILS__TEMP); }
void       temp_rewind(Arena_Mark m)   { arena_rewind(&UTILS__TEMP, m); }
void       temp_reset(void)            { arena_reset(&UTILS__TEMP); }
void       temp_free(void)             { arena_free(&UTILS__TEMP); }

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

static Opt *opts__find_short(Opt *opts, size_t n_opts, char c) {
    for (size_t i = 0; i < n_opts; i++)
        if (opts[i].short_name && opts[i].short_name == c) return &opts[i];
    return NULL;
}

static Opt *opts__find_long(Opt *opts, size_t n_opts, const char *key, size_t len) {
    for (size_t i = 0; i < n_opts; i++) {
        const char *name = opts[i].long_name;
        if (name && strlen(name) == len && strncmp(name, key, len) == 0)
            return &opts[i];
    }
    return NULL;
}

/* `origin` is the argument as the user typed it, quoted back in errors. */
static bool opts__assign(Opt *o, const char *val, const char *origin) {
    if (o->type == OPTTYPE_STR) {
        *(const char **)o->dst = val;
        return true;
    }

    char *end;
    errno = 0;
    long v = strtol(val, &end, 10);
    if (end == val || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX) {
        LOG(LOG_ERROR, "opts_parse: '%s' expects an integer, got '%s'", origin, val);
        return false;
    }
    *(int *)o->dst = (int)v;
    return true;
}

/* Walks one "-abc" token. Flags chain; the first option that takes a value
 * consumes whatever follows it, or the next argument when nothing does. */
static bool opts__parse_short_group(Opt *opts, size_t n_opts, const char *arg,
                                    int *i, int argc, char **argv) {
    for (const char *c = arg + 1; *c; ) {
        char  name  = *c++;
        Opt  *match = opts__find_short(opts, n_opts, name);
        if (!match) {
            LOG(LOG_ERROR, "opts_parse: unknown option '-%c' in '%s'", name, arg);
            return false;
        }

        if (match->type == OPTTYPE_FLAG) {
            if (*c == '=') {
                LOG(LOG_ERROR, "opts_parse: '-%c' takes no argument", name);
                return false;
            }
            *(bool *)match->dst = true;
            continue;
        }

        const char *val;
        if (*c == '=')   val = c + 1;    /* -o=file */
        else if (*c)     val = c;        /* -ofile  */
        else {                           /* -o file */
            if (++(*i) >= argc) {
                LOG(LOG_ERROR, "opts_parse: '-%c' requires an argument", name);
                return false;
            }
            val = argv[*i];
        }
        return opts__assign(match, val, arg);
    }
    return true;
}

bool opts_parse(Opt *opts, size_t n_opts, int *argc, char ***argv) {
    char **args = *argv;
    int    out  = 0;

    for (int i = 0; i < *argc; i++) {
        char *arg = args[i];

        if (strcmp(arg, "--") == 0) {
            while (++i < *argc) args[out++] = args[i];
            break;
        }

        /* Anything that is not "-x..." is positional, a lone "-" included. */
        if (arg[0] != '-' || arg[1] == '\0') {
            args[out++] = arg;
            continue;
        }

        if (arg[1] != '-') {
            if (!opts__parse_short_group(opts, n_opts, arg, &i, *argc, args))
                return false;
            continue;
        }

        const char *key      = arg + 2;
        const char *eq       = strchr(key, '=');
        size_t      key_len  = eq ? (size_t)(eq - key) : strlen(key);

        Opt *match = opts__find_long(opts, n_opts, key, key_len);
        if (!match) {
            LOG(LOG_ERROR, "opts_parse: unknown option '%s'", arg);
            return false;
        }

        if (match->type == OPTTYPE_FLAG) {
            if (eq) {
                LOG(LOG_ERROR, "opts_parse: '--%.*s' takes no argument",
                    (int)key_len, key);
                return false;
            }
            *(bool *)match->dst = true;
            continue;
        }

        const char *val = eq ? eq + 1 : NULL;
        if (!val) {
            if (++i >= *argc) {
                LOG(LOG_ERROR, "opts_parse: '%s' requires an argument", arg);
                return false;
            }
            val = args[i];
        }
        if (!opts__assign(match, val, arg)) return false;
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

void hm_reset(HashMap *hm) {
    if (hm->entries) memset(hm->entries, 0, hm->capacity * sizeof(HM_Entry));
    hm->count = 0;
    hm->used  = 0;
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