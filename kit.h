/*
 * kit.h - a single-header toolkit for C
 *
 * SPDX-License-Identifier: Unlicense
 * Public domain. The full text is at the end of this file, so that the
 * licence travels with the header when it is copied on its own.
 *
 * USAGE:
 *   In exactly ONE C file, before including this header:
 *       #define KIT_IMPLEMENTATION
 *       #include "kit.h"
 *
 *   In all other files:
 *       #include "kit.h"
 *
 * NAMING:
 *   Everything public starts with kit_, Kit or KIT_, so the header can sit
 *   next to any other library without a clash:
 *       kit_module_action   functions and function-like macros
 *       KitName             types
 *       KIT_NAME            constants, enumerators and the other macros
 *   A double underscore, kit__ or KIT__, marks what is internal.
 *
 * SECTIONS:
 *   1.  Logging      kit_log_*, KIT_LOG
 *   2.  Errors       kit_error_*, KitError
 *   3.  Filesystem   kit_fs_*
 *   4.  Arrays       kit_array_*
 *   5.  Strings      kit_str_*
 *   6.  Arena        kit_arena_*, kit_scratch_*
 *   7.  Timer        kit_timer_*
 *   8.  Vectors      kit_vec2_*, kit_vec3_*
 *   9.  Command line kit_cli_*
 *   10. Buffers      kit_buf_*
 *   11. Processes    kit_command_*, kit_process_*
 *   12. Hashing      kit_hash_*
 *   13. Map          kit_map_*
 *   14. Paths        kit_path_*
 *   15. Maths        kit_clampf, kit_lerpf, kit_remapf, KIT_MIN, KIT_MAX
 *
 * REQUIREMENTS:
 *   C11 or later. On POSIX systems the implementation uses clock_gettime(),
 *   dprintf() and isatty(), which a strict -std=c11 hides. The header asks
 *   for the C library's default feature set, so it must be included before
 *   any other system header when building that way.
 */

#ifndef KIT_H
#define KIT_H

/* A strict -std=c11 hides clock_gettime(), dprintf() and isatty(). This asks
 * glibc and musl for their default feature set, POSIX.1-2008 plus the BSD
 * extensions, which is exactly what a -std=gnu11 build already sees.
 *
 * It is deliberately not _POSIX_C_SOURCE. That one narrows the whole
 * translation unit to strict POSIX, so the host program silently loses
 * usleep(), strcasecmp() and the like from every header it includes after
 * this one. And nothing is defined if the program already chose a feature
 * set: that choice is not the library's to override. */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE) && !defined(_GNU_SOURCE) \
    && !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) && !defined(_BSD_SOURCE)
#    define _DEFAULT_SOURCE
#endif

/* mingw defaults to the msvcrt printf, which predates C99 and rejects %zu.
 * This switches it to mingw's own conforming implementation, and like the
 * macro above it only counts if nothing has included <stdio.h> yet. */
#if defined(__MINGW32__) && !defined(__USE_MINGW_ANSI_STDIO)
#    define __USE_MINGW_ANSI_STDIO 1
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

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <io.h>
#else
#    include <unistd.h>
#    include <dirent.h>
#    include <poll.h>
#    include <sys/wait.h>
#    include <sys/stat.h>
#    include <fcntl.h>
#endif

/* --------------------------------------------------------------------------
 * Compiler helpers
 * -------------------------------------------------------------------------- */

/* On mingw the "printf" archetype means the msvcrt dialect, which has no %zu,
 * so the checker rejects formats the runtime accepts once
 * __USE_MINGW_ANSI_STDIO is on. "gnu_printf" is the archetype that matches. */
#if defined(__MINGW32__) && (defined(__GNUC__) || defined(__clang__))
#    define KIT_PRINTF_FORMAT(fmt_idx, first_idx) \
         __attribute__((format(gnu_printf, fmt_idx, first_idx)))
#elif defined(__GNUC__) || defined(__clang__)
#    define KIT_PRINTF_FORMAT(fmt_idx, first_idx) \
         __attribute__((format(printf, fmt_idx, first_idx)))
#else
#    define KIT_PRINTF_FORMAT(fmt_idx, first_idx)
#endif

/* Marks kit__panic as never returning, so that callers do not trigger
 * "control reaches end of non-void function" and the optimizer can drop the
 * code that follows a KIT_PANIC. */
#if defined(__GNUC__) || defined(__clang__)
#    define KIT_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#    define KIT_NORETURN __declspec(noreturn)
#else
#    define KIT_NORETURN
#endif

/* Thread-local storage for the scratch arena. Define KIT_NO_THREAD_LOCAL to
 * fall back to a single shared one, for a freestanding target that has none. */
#if defined(KIT_NO_THREAD_LOCAL)
#    define KIT_THREAD_LOCAL
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#    define KIT_THREAD_LOCAL _Thread_local
#elif defined(__GNUC__) || defined(__clang__)
#    define KIT_THREAD_LOCAL __thread
#elif defined(_MSC_VER)
#    define KIT_THREAD_LOCAL __declspec(thread)
#else
#    define KIT_THREAD_LOCAL
#endif

/* C converts void * to any object pointer on its own; C++ does not, and the
 * dynamic array macros assign the result of realloc to a typed member. */
#if defined(__cplusplus)
#    define KIT_CAST_LIKE(lvalue, ptr) (decltype(lvalue))(ptr)
#elif defined(__GNUC__) || defined(__clang__)
#    define KIT_CAST_LIKE(lvalue, ptr) (__typeof__(lvalue))(ptr)
#else
#    define KIT_CAST_LIKE(lvalue, ptr) (ptr)
#endif

/* A zeroed aggregate. C spells it {0} and C++ warns about the fields that
 * leaves out; C++ spells it {} and C rejects that before C23. Library types
 * are all designed to start zeroed, so this is the portable way to say so:
 *
 *   KitCommand cmd = KIT_ZEROED;
 */
#ifdef __cplusplus
#    define KIT_ZEROED {}
#else
#    define KIT_ZEROED {0}
#endif

/* Struct literals: a C compound literal, brace initialisation in C++. */
#ifdef __cplusplus
#    define KIT_LITERAL(T) T
#else
#    define KIT_LITERAL(T) (T)
#endif

#define KIT_UNUSED(v)      (void)(v)
#define KIT_COUNTOF(a)   (sizeof(a) / sizeof((a)[0]))

/* Single exit point: record the result and jump to the cleanup label, so a
 * function releases everything it acquired in exactly one place however many
 * ways it can fail. Requires a `result` variable and a `cleanup:` label.
 *
 * Usage:
 *   bool my_func(void) {
 *       bool result = true;
 *       FILE *f = fopen(...);
 *       if (!f) KIT_BAIL(false);
 *       ...
 *   cleanup:
 *       if (f) fclose(f);
 *       return result;
 *   }
 */
#define KIT_BAIL(value) do { result = (value); goto cleanup; } while (0)

/* --------------------------------------------------------------------------
 * SECTION 1 : LOGGING
 * -------------------------------------------------------------------------- */

typedef enum {
    KIT_LOG_DEBUG,
    KIT_LOG_INFO,
    KIT_LOG_WARN,
    KIT_LOG_ERROR,
    KIT_LOG_CRITICAL
} KitLogLevel;

/* When to emit ANSI escape sequences.
 *   KIT_LOG_COLOR_AUTO    colour only when the output stream is a terminal
 *                         and NO_COLOR is unset (default)
 *   KIT_LOG_COLOR_ALWAYS  force colour, for a pager that renders it
 *   KIT_LOG_COLOR_NEVER   never colour
 */
typedef enum {
    KIT_LOG_COLOR_AUTO,
    KIT_LOG_COLOR_ALWAYS,
    KIT_LOG_COLOR_NEVER
} KitLogColor;

/* Which fields precede the message. Combine with '|'.
 *
 *   kit_log_set_fields(KIT_LOG_FIELD_DATE | KIT_LOG_FIELD_TIME |
 *                      KIT_LOG_FIELD_USER);
 *   KIT_INFO("started");
 *   -> [2026-09-12 (Sat)] [14:05:42] [illumye] started
 *
 * KIT_LOG_FIELD_COUNT numbers the records as they are emitted, which is how a
 * burst of identical lines stops looking like a stuck program. */
typedef enum {
    KIT_LOG_FIELD_TIME     = 1u << 0,   /* 14:05:42                */
    KIT_LOG_FIELD_DATE     = 1u << 1,   /* 2026-09-12 (Sat)        */
    KIT_LOG_FIELD_LEVEL    = 1u << 2,   /* INFO                    */
    KIT_LOG_FIELD_LOCATION = 1u << 3,   /* kit.h:42              */
    KIT_LOG_FIELD_USER     = 1u << 4,   /* from LOGNAME/USER       */
    KIT_LOG_FIELD_COUNT    = 1u << 5    /* record number           */
} KitLogField;

#define KIT_LOG_FIELDS_DEFAULT (KIT_LOG_FIELD_TIME | KIT_LOG_FIELD_LEVEL | KIT_LOG_FIELD_LOCATION)
#define KIT_LOG_FIELDS_ALL     (KIT_LOG_FIELD_TIME | KIT_LOG_FIELD_DATE | KIT_LOG_FIELD_LEVEL | \
                            KIT_LOG_FIELD_LOCATION | KIT_LOG_FIELD_USER | KIT_LOG_FIELD_COUNT)
#define KIT_LOG_FIELDS_NONE    0u

void kit_log_set_level(KitLogLevel level);
void kit_log_set_output(FILE *fp);   /* NULL resets to stderr (default) */
void kit_log_set_color(KitLogColor mode);
void kit_log_set_fields(unsigned fields);

void kit__log(KitLogLevel level, const char *file, int line,
                    const char *fmt, ...) KIT_PRINTF_FORMAT(4, 5);
KIT_NORETURN void kit__panic(const char *file, int line,
                                     const char *fmt, ...) KIT_PRINTF_FORMAT(3, 4);

#define KIT_LOG(level, ...)  kit__log(level, __FILE__, __LINE__, __VA_ARGS__)

/* One macro per level, for the usual case of a level fixed at the call site.
 * KIT_LOG stays for the rare one where the level is only known at run time. */
#define KIT_DEBUG(...)     KIT_LOG(KIT_LOG_DEBUG, __VA_ARGS__)
#define KIT_INFO(...)      KIT_LOG(KIT_LOG_INFO, __VA_ARGS__)
#define KIT_WARN(...)      KIT_LOG(KIT_LOG_WARN, __VA_ARGS__)
#define KIT_ERROR(...)     KIT_LOG(KIT_LOG_ERROR, __VA_ARGS__)
#define KIT_CRITICAL(...)  KIT_LOG(KIT_LOG_CRITICAL, __VA_ARGS__)
#define KIT_PANIC(...)       kit__panic(__FILE__, __LINE__, __VA_ARGS__)
#define KIT_TODO(msg)        KIT_PANIC("TODO: %s", msg)
#define KIT_UNREACHABLE(msg) KIT_PANIC("UNREACHABLE: %s", msg)

/* --------------------------------------------------------------------------
 * SECTION 2 : ERRORS
 *
 * A failure that says what went wrong, where it happened, and what the caller
 * can do about it. The caller owns the KitError, usually on its stack, and the
 * message lives inside it: recording an error never allocates, never fails,
 * and leaves nothing to free.
 *
 *   KitError err = KIT_ZEROED;
 *   char *text = kit_fs_read("config.ini", &err);
 *   if (!text) {
 *       kit_error_context(&err, "loading the configuration");
 *       KIT_ERROR("%s", err.message);
 *   }
 *
 *   loading the configuration: cannot open 'config.ini': No such file or directory
 *
 * The convention every fallible kit function follows: its last parameter is a
 * KitError *. Given one, the function fills it and logs nothing, because what
 * a failure means is the caller's decision. Given NULL, it logs the failure at
 * KIT_LOG_ERROR instead, for code that has nothing better to do with it. A kit
 * function never both logs a failure and reports it.
 * -------------------------------------------------------------------------- */

#ifndef KIT_ERROR_CAPACITY
#define KIT_ERROR_CAPACITY 512
#endif

/* What the caller can do about a failure, rather than exactly what the system
 * said: that precise value is kept in KitError.native. */
typedef enum {
    KIT_OK = 0,
    KIT_ERR_NOT_FOUND,     /* nothing there: create it, or skip it        */
    KIT_ERR_EXISTS,        /* already there                               */
    KIT_ERR_NOT_EMPTY,     /* a directory that still has entries          */
    KIT_ERR_WRONG_KIND,    /* a file where a directory was needed, or the
                            * reverse                                     */
    KIT_ERR_PERMISSION,    /* not allowed, or read-only                   */
    KIT_ERR_INVALID,       /* malformed input: fix the input              */
    KIT_ERR_RANGE,         /* a value, a size or a name is too large      */
    KIT_ERR_NO_SPACE,      /* disk or quota full                          */
    KIT_ERR_NO_MEMORY,     /* an allocation was refused                   */
    KIT_ERR_BUSY,          /* in use or locked: retry later               */
    KIT_ERR_INTERRUPTED,   /* a signal or a timeout: retry                */
    KIT_ERR_IO,            /* the device itself failed                    */
    KIT_ERR_PROCESS,       /* a child failed to start, or failed          */
    KIT_ERR_UNSUPPORTED,   /* not possible here, e.g. across devices      */
    KIT_ERR_OTHER
} KitErrorCode;

typedef struct {
    KitErrorCode code;
    int          native;      /* errno, GetLastError or exit status; 0 if none */
    bool         truncated;   /* part of the message was dropped to fit        */
    size_t       length;      /* of message, terminator excluded               */
    char         message[KIT_ERROR_CAPACITY];
} KitError;

/* Records a failure, replacing any already recorded. Returns false, so that a
 * bool function can end with
 *
 *   return kit_error_set(err, KIT_ERR_INVALID, "expected a number");
 *
 * With err NULL the message is logged instead, as described above. */
bool kit_error_set(KitError *err, KitErrorCode code, const char *fmt, ...)
    KIT_PRINTF_FORMAT(3, 4);

/* Same, with the code derived from an errno value, which is also kept in
 * native, and its description appended:
 *
 *   kit_error_errno(err, errno, "cannot open '%s'", path)
 *   -> cannot open 'x': No such file or directory */
bool kit_error_errno(KitError *err, int errnum, const char *fmt, ...)
    KIT_PRINTF_FORMAT(3, 4);

/* Prefixes the recorded message with where it happened, as the error travels
 * up. The outermost context comes first and the root cause last. When the
 * result does not fit, the middle is elided: the outermost context says where
 * to look and the root cause says what broke, the steps in between matter
 * least. Does nothing when err is NULL or records no failure. Returns false. */
bool kit_error_context(KitError *err, const char *fmt, ...)
    KIT_PRINTF_FORMAT(2, 3);

/* Back to "no failure", ready for reuse. NULL is accepted. */
void kit_error_clear(KitError *err);

/* A stable lowercase name for a code, such as "not_found", for logs and for
 * scripts that must not depend on the wording of a message. */
const char *kit_error_code_name(KitErrorCode code);

/* The category an errno value belongs to. */
KitErrorCode kit_error_code_from_errno(int errnum);

/* --------------------------------------------------------------------------
 * SECTION 3 : FILESYSTEM
 *
 * Every function here that can fail takes a KitError * last, following the
 * convention described in section 2: pass one to receive the failure, or NULL
 * to have it logged. The codes are normalised across platforms, so removing a
 * directory with kit_fs_remove is KIT_ERR_WRONG_KIND everywhere, although
 * Linux, macOS and Windows each report it differently.
 * -------------------------------------------------------------------------- */

/* Read an entire file into a malloc'd, NUL-terminated buffer (caller owns it).
 * Returns NULL on failure.
 *
 * The read is streamed, so it also works on files whose size is not known up
 * front: pipes, character devices and the synthetic files under /proc.
 *
 * kit_fs_read_sized also reports the byte count, which is the only way to
 * handle binary data containing embedded NUL bytes. The terminator is always
 * written, so the result stays usable as a C string for text files. */
char *kit_fs_read(const char *path, KitError *err);
char *kit_fs_read_sized(const char *path, size_t *out_size, KitError *err);

/* Write data to a file, creating or truncating it. */
bool kit_fs_write(const char *path, const void *data, size_t size, KitError *err);

/* Returns true if path exists and is a regular file. Not a failure otherwise:
 * it answers a question. */
bool kit_fs_is_file(const char *path);

/* Returns the size in bytes of the file at path, or -1 on failure.
 * int64_t rather than long, which is 32 bits on Windows and on every ILP32
 * target, and would silently cap the answer at 2 GB. */
int64_t kit_fs_size(const char *path, KitError *err);

/* What lives at path. KIT_FILE_KIND_NONE means nothing does, which is not a
 * failure: use it to test existence regardless of the kind. */
typedef enum {
    KIT_FILE_KIND_NONE,
    KIT_FILE_KIND_REGULAR,
    KIT_FILE_KIND_DIRECTORY,
    KIT_FILE_KIND_OTHER      /* a device, a socket, a fifo... */
} KitFileKind;

KitFileKind kit_fs_kind(const char *path);
bool        kit_fs_is_dir(const char *path);

/* Creates a directory and every missing parent, like `mkdir -p`. Succeeds
 * when the directory already exists; a file in the way is
 * KIT_ERR_WRONG_KIND. */
bool kit_fs_mkdir(const char *path, KitError *err);

/* Copies src over dst, creating or truncating it. On POSIX the permission
 * bits of the source are carried over. */
bool kit_fs_copy(const char *src, const char *dst, KitError *err);

/* Removes a file, never a directory: POSIX remove() would take an empty
 * directory too, Windows remove() would not, so neither is used. A path that
 * does not exist is KIT_ERR_NOT_FOUND, which is why an idempotent caller
 * tests with kit_fs_is_file first. */
bool kit_fs_remove(const char *path, KitError *err);

/* Removes an empty directory; one with entries is KIT_ERR_NOT_EMPTY.
 * Recursion is left to the caller: a library function that deletes a tree is
 * one typo away from deleting the wrong one, and the loop is four lines with
 * kit_fs_list. */
bool kit_fs_rmdir(const char *path, KitError *err);

/* Renames or moves a file, replacing dst if it exists. Both paths must sit on
 * the same filesystem; across devices it is KIT_ERR_UNSUPPORTED. */
bool kit_fs_rename(const char *from, const char *to, KitError *err);

/* Last modification time, in whole seconds since the Unix epoch, or -1.
 * kit_fs_stale compares at the finest resolution the platform exposes,
 * which is why it does not go through this function. */
int64_t kit_fs_mtime(const char *path, KitError *err);

/* A list of owned, NUL-terminated paths. Works with the kit_array_* macros. */
typedef struct {
    char  **items;
    size_t  count;
    size_t  capacity;
} KitFileList;

void kit_file_list_free(KitFileList *list);

/* Appends the entries of a directory to `out`, excluding "." and "..".
 * Names only, not full paths. Sorted with strcmp, so a build driven from the
 * result is reproducible. On failure `out` is left exactly as it was. */
bool kit_fs_list(const char *path, KitFileList *out, KitError *err);

/* Is `output` stale with respect to its inputs?
 *
 *   1  rebuild needed: output is missing, or an input is at least as recent
 *   0  output is up to date
 *  -1  failure: an input is missing or unreadable
 *
 * The tri-state is the whole point. A bool would force a missing input to be
 * reported as "up to date", which silently skips the build step.
 *
 *   if (kit_fs_stale(exe, srcs, n, &err) != 0) { ... rebuild ... }
 */
int kit_fs_stale(const char *output, const char *const *inputs, size_t n_inputs,
                 KitError *err);

/* Same, taking the inputs straight from a KitFileList, the shape kit_fs_list
 * fills. Spelling the array as const char *const * is what lets a caller pass
 * one without a cast that -Wcast-qual then objects to. */
int kit_fs_stale_list(const char *output, const KitFileList *inputs, KitError *err);

/* Same, for a single input. A function rather than a macro: the array had to
 * be a compound literal, and C++ has no equivalent. */
static inline int kit_fs_stale1(const char *output, const char *input, KitError *err) {
    const char *one[1];
    one[0] = input;
    return kit_fs_stale(output, one, 1, err);
}

/* --------------------------------------------------------------------------
 * SECTION 4 : ARRAYS
 *
 * Any struct with:
 *   T      *items;
 *   size_t  count;
 *   size_t  capacity;
 * works with these macros.
 * -------------------------------------------------------------------------- */

#ifndef KIT_ARRAY_INIT_CAP
#define KIT_ARRAY_INIT_CAP 256
#endif

/* Reserve at least `cap` slots.
 * The result of realloc lands in a temporary so that the original block is not
 * leaked when the allocation fails, and capacity growth is checked against
 * SIZE_MAX so that an absurd request aborts instead of wrapping around. */
#define kit_array_reserve(da, cap)                                                          \
    do {                                                                                    \
        size_t _want = (cap);                                                               \
        if (_want > (da)->capacity) {                                                       \
            size_t _cap = (da)->capacity ? (da)->capacity : (size_t)KIT_ARRAY_INIT_CAP;     \
            while (_want > _cap) {                                                          \
                if (_cap > SIZE_MAX / 2) KIT_PANIC("kit_array_reserve: capacity overflow"); \
                _cap *= 2;                                                                  \
            }                                                                               \
            if (_cap > SIZE_MAX / sizeof(*(da)->items))                                     \
                KIT_PANIC("kit_array_reserve: allocation size overflow");                   \
            void *_mem = realloc((da)->items, _cap * sizeof(*(da)->items));                 \
            if (!_mem) KIT_PANIC("kit_array_reserve: realloc of %zu bytes failed",          \
                             _cap * sizeof(*(da)->items));                                  \
            (da)->items    = KIT_CAST_LIKE((da)->items, _mem);                              \
            (da)->capacity = _cap;                                                          \
        }                                                                                   \
    } while (0)

/* Append a single item. */
#define kit_array_push(da, item)                  \
    do {                                          \
        kit_array_reserve((da), (da)->count + 1); \
        (da)->items[(da)->count++] = (item);      \
    } while (0)

/* Append n items from a pointer. */
#define kit_array_push_many(da, src, n)                \
    do {                                               \
        size_t _n = (n);                               \
        if (_n > 0) {                                  \
            kit_array_reserve((da), (da)->count + _n); \
            memcpy((da)->items + (da)->count,          \
                   (src),                              \
                   _n * sizeof(*(da)->items));         \
            (da)->count += _n;                         \
        }                                              \
    } while (0)

/* Pop the last element (assert non-empty). */
#define kit_array_pop(da)                        \
    ((da)->items[(da)->count > 0 ? --(da)->count \
                                 : (KIT_PANIC("kit_array_pop on empty array"), (size_t)0)])

/* Access first / last with bounds check. */
#define kit_array_first(da)                  \
    ((da)->items[(da)->count > 0 ? (size_t)0 \
                                 : (KIT_PANIC("kit_array_first on empty array"), (size_t)0)])
#define kit_array_last(da)                         \
    ((da)->items[(da)->count > 0 ? (da)->count - 1 \
                                 : (KIT_PANIC("kit_array_last on empty array"), (size_t)0)])

/* Remove element at index i, swap with last (unordered). */
#define kit_array_swap_remove(da, i)                                              \
    do {                                                                          \
        size_t _i = (i);                                                          \
        if (_i >= (da)->count) KIT_PANIC("kit_array_swap_remove: out of bounds"); \
        (da)->items[_i] = (da)->items[--(da)->count];                             \
    } while (0)

/* Iterate. `it` is a pointer to the current element.
 * Example:
 *   kit_array_each(int, x, &my_array) { printf("%d\n", *x); }
 */
#define kit_array_each(Type, it, da) \
    for (Type *it = (da)->items; it < (da)->items + (da)->count; ++it)

/* Linear search writing the index of the first match into `out_index`, or
 * (da)->count when there is none. Portable everywhere, unlike
 * kit_array_contains.
 * Example:
 *   size_t at;
 *   kit_array_find(&my_array, 42, at);
 *   if (at < my_array.count) { ... }
 */
#define kit_array_find(da, val, out_index)          \
    do {                                            \
        (out_index) = (da)->count;                  \
        for (size_t _k = 0; _k < (da)->count; _k++) \
            if ((da)->items[_k] == (val)) {         \
                (out_index) = _k;                   \
                break;                              \
            }                                       \
    } while (0)

/* Linear search - evaluates to true if any element == val.
 * Requires GCC/Clang (statement expression); use kit_array_find elsewhere.
 * Example:
 *   if (kit_array_contains(&my_array, 42)) { ... }
 */
#if defined(__GNUC__) || defined(__clang__)
#define kit_array_contains(da, val)                                 \
    __extension__({                                                 \
        bool _found = false;                                        \
        for (size_t _j = 0; _j < (da)->count; _j++)                 \
            if ((da)->items[_j] == (val)) { _found = true; break; } \
        _found;                                                     \
    })
#endif

/* Free and zero the array. */
#define kit_array_free(da)     \
    do {                       \
        free((da)->items);     \
        (da)->items    = NULL; \
        (da)->count    = 0;    \
        (da)->capacity = 0;    \
    } while (0)

/* --------------------------------------------------------------------------
 * SECTION 5 : STRINGS
 *
 * A non-owning slice over existing memory. Never NUL-terminated.
 * Use KIT_STR_FMT / KIT_STR_ARG with printf.
 * -------------------------------------------------------------------------- */

typedef struct {
    const char *data;
    size_t      count;
} KitStr;

#define KIT_STR(cstr)         kit_str_from(cstr)
#define KIT_STR_FMT           "%.*s"
#define KIT_STR_ARG(sv)       (int)(sv).count, (sv).data
#define KIT_STR_LIT(literal)  (KIT_LITERAL(KitStr){ (literal), sizeof(literal) - 1 })

/* Returned by the search functions when there is no match. */
#define KIT_NPOS ((size_t)-1)

KitStr kit_str_from(const char *cstr);
KitStr kit_str_from_parts(const char *data, size_t count);

/* Whitespace trimming. */
KitStr kit_str_trim_left(KitStr sv);
KitStr kit_str_trim_right(KitStr sv);
KitStr kit_str_trim(KitStr sv);

/* Consume characters from the left up to (but not including) `delim`.
 * Advances *sv past the delimiter. Returns the consumed part. */
KitStr kit_str_cut(KitStr *sv, char delim);

/* Consume n characters from the left. */
KitStr kit_str_take(KitStr *sv, size_t n);

/* Comparison. */
bool kit_str_eq(KitStr a, KitStr b);
bool kit_str_eq_cstr(KitStr a, const char *b);
bool kit_str_starts_with(KitStr sv, KitStr prefix);
bool kit_str_ends_with(KitStr sv, KitStr suffix);
bool kit_str_starts_with_cstr(KitStr sv, const char *prefix);
bool kit_str_ends_with_cstr(KitStr sv, const char *suffix);

/* ASCII only: the library does no locale or Unicode case folding. */
bool kit_str_eq_nocase(KitStr a, KitStr b);

/* Search. Both return KIT_NPOS when there is no match; an empty needle matches
 * at 0, which is what makes "starts with nothing" true. */
size_t kit_str_find_char(KitStr sv, char c);
size_t kit_str_find(KitStr sv, KitStr needle);
bool   kit_str_contains(KitStr sv, KitStr needle);

/* Consume up to a multi-character delimiter, the counterpart of
 * kit_str_cut. When the delimiter is absent the whole view is returned
 * and *sv is left empty. */
KitStr kit_str_cut_str(KitStr *sv, KitStr delim);

/* Consume n characters from the right. */
KitStr kit_str_take_right(KitStr *sv, size_t n);

/* Loop form of kit_str_cut: writes the next field into *out and returns
 * false once the view is exhausted.
 *
 *   KitStr field;
 *   while (kit_str_next(&line, ',', &field)) { ... }
 *
 * An empty view yields nothing, and a trailing delimiter does not produce an
 * extra empty field: both "a,b" and "a,b," yield "a" then "b". Telling those
 * two apart would mean hiding a state bit inside the KitStr, which is a
 * worse trade than the simpler contract. A caller that needs the distinction
 * can test the input with kit_str_ends_with_cstr first. Delimiters in the
 * middle are never collapsed, so "a,,b" does yield an empty middle field. */
bool kit_str_next(KitStr *sv, char delim, KitStr *out);

/* Strict numeric parsing: the entire view must be consumed, leading and
 * trailing spaces included, or the call fails and *out is untouched.
 * Overflow is a failure, not a saturation. */
bool kit_str_to_i64(KitStr sv, int64_t *out);
bool kit_str_to_u64(KitStr sv, uint64_t *out);
bool kit_str_to_double(KitStr sv, double *out);

/* A malloc'd, NUL-terminated copy. The caller owns it. */
char *kit_str_dup(KitStr sv);

/* --------------------------------------------------------------------------
 * SECTION 6 : ARENA
 * -------------------------------------------------------------------------- */

/* A bump allocator over a chain of regions. Running out of room grows the
 * chain instead of failing, so the initial size is a hint, not a ceiling.
 *
 *   KitArena a = KIT_ZEROED;               // grows on demand
 *   KitArena b = kit_arena_make(1 << 20);  // same, first region preallocated
 *
 * Every allocation is zeroed. Individual blocks are never freed; the whole
 * arena is reset or released at once. */

#ifndef KIT_ARENA_REGION_SIZE
#define KIT_ARENA_REGION_SIZE (64 * 1024)
#endif

typedef struct KitArenaRegion KitArenaRegion;
struct KitArenaRegion {
    KitArenaRegion *next;
    size_t        capacity;
    size_t        used;
    char          data[];   /* the payload follows the header */
};

typedef struct {
    KitArenaRegion *first;
    KitArenaRegion *current;
    size_t        region_size;   /* hint for the regions allocated next */
} KitArena;

/* Preallocates a first region of `size` bytes and uses it as the growth hint.
 * A zero-initialised KitArena behaves identically, minus the preallocation. */
KitArena kit_arena_make(size_t size);

/* Aligned on max_align_t, which suits every standard type. */
void *kit_arena_alloc(KitArena *a, size_t size);

/* For over-aligned types: SIMD vectors, cache-line padding. `align` must be a
 * power of two. */
void *kit_arena_alloc_aligned(KitArena *a, size_t size, size_t align);

/* Allocates n items of type T: kit_arena_alloc_array(arena, T, n). */
#define kit_arena_alloc_array(a, T, n) ((T *)kit_arena_alloc((a), sizeof(T) * (n)))

/* Copies into the arena. The result is NUL-terminated and owned by the arena,
 * so it must not be freed individually. */
char *kit_arena_strdup(KitArena *a, const char *s);
char *kit_arena_strndup(KitArena *a, const char *s, size_t n);
char *kit_arena_printf(KitArena *a, const char *fmt, ...) KIT_PRINTF_FORMAT(2, 3);

/* Bytes handed out, and bytes held. The gap is alignment padding plus the
 * tail of every region that was left behind when the chain grew. */
size_t kit_arena_used(const KitArena *a);
size_t kit_arena_capacity(const KitArena *a);

/* Frees everything at once but keeps the regions for reuse. */
void kit_arena_reset(KitArena *a);

/* Releases every region back to the allocator. */
void kit_arena_free(KitArena *a);

/* A position in the arena, to roll back to. Taking a mark and rewinding to it
 * turns the arena into a stack: allocate freely, then release in one step.
 * A mark is invalidated by kit_arena_reset and kit_arena_free. */
typedef struct {
    KitArenaRegion *region;
    size_t        used;
} KitArenaMark;

KitArenaMark kit_arena_mark(const KitArena *a);
void       kit_arena_rewind(KitArena *a, KitArenaMark mark);

/* --------------------------------------------------------------------------
 * SECTION 6b : SCRATCH MEMORY
 *
 * A process-wide scratch arena for strings that live until the end of the
 * current step: a path being assembled, a formatted message, a command line.
 * Nothing here is ever freed individually.
 *
 *   const char *out = kit_scratch_printf("%s/%s.o", build_dir, name);
 *   kit_command_push(&cmd, out);
 *   ...
 *   kit_scratch_reset();   // once the step is over
 *
 * Reclaim with kit_scratch_reset between iterations, or a mark for nesting:
 *
 *   KitArenaMark m = kit_scratch_mark();
 *   ... kit_scratch_printf ...
 *   kit_scratch_rewind(m);
 *
 * The arena is thread-local, so two threads never hand each other a pointer
 * and never race. The consequence is that each thread owns its own regions:
 * a thread that allocates scratch and then exits leaves them behind unless it
 * calls kit_scratch_free on its way out. Long-lived threads want
 * kit_scratch_reset per unit of work, worker threads that come and go want
 * kit_scratch_free before returning.
 *
 * Define KIT_NO_THREAD_LOCAL to go back to one shared arena.
 * -------------------------------------------------------------------------- */

void  *kit_scratch_alloc(size_t size);
char  *kit_scratch_strdup(const char *s);
char  *kit_scratch_printf(const char *fmt, ...) KIT_PRINTF_FORMAT(1, 2);

KitArenaMark kit_scratch_mark(void);
void       kit_scratch_rewind(KitArenaMark mark);
void       kit_scratch_reset(void);

/* Releases the scratch memory to the allocator. Rarely needed: a program that
 * calls kit_scratch_reset already reuses the same regions forever. */
void       kit_scratch_free(void);

/* --------------------------------------------------------------------------
 * SECTION 7 : TIMER
 * -------------------------------------------------------------------------- */

typedef struct {
#ifdef _WIN32
    LARGE_INTEGER start;
#else
    struct timespec start;
#endif
} KitTimer;

KitTimer kit_timer_start(void);
double    kit_timer_s(KitTimer sw);
double    kit_timer_ms(KitTimer sw);

/* --------------------------------------------------------------------------
 * SECTION 8 : VECTORS
 *
 * Define KIT_NO_VEC_MATH before including to skip this section and avoid
 * the dependency on -lm.
 * -------------------------------------------------------------------------- */

#ifndef KIT_NO_VEC_MATH
#include <math.h>

typedef struct { float x, y; }    KitVec2;
typedef struct { float x, y, z; } KitVec3;

#define KIT_VEC2(x, y)    (KIT_LITERAL(KitVec2){(float)(x), (float)(y)})
#define KIT_VEC3(x, y, z) (KIT_LITERAL(KitVec3){(float)(x), (float)(y), (float)(z)})

#define KIT_VEC2_FMT      "(%.2f, %.2f)"
#define KIT_VEC2_ARG(v)   (v).x, (v).y
#define KIT_VEC3_FMT      "(%.2f, %.2f, %.2f)"
#define KIT_VEC3_ARG(v)   (v).x, (v).y, (v).z

KitVec2  kit_vec2_add(KitVec2 a, KitVec2 b);
KitVec2  kit_vec2_sub(KitVec2 a, KitVec2 b);
KitVec2  kit_vec2_scale(KitVec2 a, float s);
KitVec2  kit_vec2_mul(KitVec2 a, KitVec2 b);
float kit_vec2_len(KitVec2 a);
float kit_vec2_dist(KitVec2 a, KitVec2 b);
KitVec2  kit_vec2_norm(KitVec2 a);
float kit_vec2_dot(KitVec2 a, KitVec2 b);

KitVec3  kit_vec3_add(KitVec3 a, KitVec3 b);
KitVec3  kit_vec3_sub(KitVec3 a, KitVec3 b);
KitVec3  kit_vec3_scale(KitVec3 a, float s);
KitVec3  kit_vec3_mul(KitVec3 a, KitVec3 b);
float kit_vec3_len(KitVec3 a);
KitVec3  kit_vec3_norm(KitVec3 a);
float kit_vec3_dot(KitVec3 a, KitVec3 b);
KitVec3  kit_vec3_cross(KitVec3 a, KitVec3 b);

#endif /* KIT_NO_VEC_MATH */

/* --------------------------------------------------------------------------
 * SECTION 9 : COMMAND LINE
 * -------------------------------------------------------------------------- */

/* Shift and return the next argument (NULL when exhausted). */
char *kit_cli_shift(int *argc, char ***argv);

/* -- Option parsing --------------------------------------------------------
 *
 * Define an array of KitCliOpt, initialize destination variables with defaults,
 * then call kit_cli_parse. Positional arguments are left in argv/argc.
 *
 * Usage:
 *   bool verbose  = false;
 *   const char *output = "a.out";
 *   int  count    = 1;
 *
 *   KitCliOpt opts[] = {
 *       KIT_CLI_FLAG('v', "verbose", "Enable verbose output",  &verbose),
 *       KIT_CLI_STR ('o', "output",  "FILE", "Output file",   &output),
 *       KIT_CLI_INT ('n', "count",   "N",    "Iterations",    &count),
 *   };
 *   if (!kit_cli_parse_arr(opts, &argc, &argv)) return 1;
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

typedef enum { KIT_CLI_OPT_FLAG, KIT_CLI_OPT_STR, KIT_CLI_OPT_INT } KitCliOptType;

typedef struct {
    char        short_name; /* single char, or 0 */
    const char *long_name;  /* without leading "--", or NULL */
    KitCliOptType     type;
    const char *meta;       /* value placeholder shown in help, e.g. "FILE" */
    const char *help;
    void       *dst;        /* bool* / const char** / int* */
} KitCliOpt;

#define KIT_CLI_FLAG(s, l,       help, dst) { (s), (l), KIT_CLI_OPT_FLAG, NULL,  (help), (dst) }
#define KIT_CLI_STR( s, l, meta, help, dst) { (s), (l), KIT_CLI_OPT_STR,  (meta),(help), (dst) }
#define KIT_CLI_INT( s, l, meta, help, dst) { (s), (l), KIT_CLI_OPT_INT,  (meta),(help), (dst) }

/* Parse options in place. An unknown option or a missing value is logged and
 * returns false. After the call, argc and argv hold only the positional
 * arguments. */
bool kit_cli_parse(KitCliOpt *opts, size_t n_opts, int *argc, char ***argv);

/* Print a formatted option summary to fp. */
void kit_cli_usage(FILE *fp, const char *program, const KitCliOpt *opts, size_t n_opts);

#define kit_cli_parse_arr(opts, argc, argv) \
    kit_cli_parse((opts), KIT_COUNTOF(opts), (argc), (argv))
#define kit_cli_usage_arr(fp, prog, opts) \
    kit_cli_usage((fp), (prog), (opts), KIT_COUNTOF(opts))

/* --------------------------------------------------------------------------
 * SECTION 10 : BUFFERS
 * -------------------------------------------------------------------------- */

typedef struct {
    char   *items;
    size_t  count;
    size_t  capacity;
} KitBuf;

void  kit_buf_append(KitBuf *sb, const char *str);
void  kit_buf_append_n(KitBuf *sb, const char *str, size_t n);
void  kit_buf_append_char(KitBuf *sb, char c);
void  kit_buf_printf(KitBuf *sb, const char *fmt, ...) KIT_PRINTF_FORMAT(2, 3);
void  kit_buf_append_str(KitBuf *sb, KitStr sv);

/* Return a NUL-terminated view into the builder (no copy). */
char *kit_buf_cstr(KitBuf *sb);

/* Return a freshly malloc'd copy. Caller owns the result. */
char *kit_buf_dup(KitBuf *sb);

void kit_buf_reset(KitBuf *sb);
void kit_buf_free(KitBuf *sb);

/* --------------------------------------------------------------------------
 * SECTION 11 : PROCESSES
 *
 * A KitCommand is an argument list; running it gives a KitProcess.
 *   kit_command_run(c)          synchronous, inherits stdout and stderr
 *   kit_command_spawn(c)        asynchronous, returns a KitProcess to wait on
 *   kit_command_capture(c, b)   synchronous, collects stdout into a KitBuf
 *
 * Variadic shorthand, NULL-terminated:
 *   kit_command_run_args("cc", "-o", "out", "main.c", NULL)
 * -------------------------------------------------------------------------- */

typedef struct {
    const char **items;
    size_t       count;
    size_t       capacity;
} KitCommand;

#ifdef _WIN32
typedef HANDLE KitProcess;
#define KIT_PROCESS_INVALID INVALID_HANDLE_VALUE
#else
typedef int    KitProcess;
#define KIT_PROCESS_INVALID (-1)
#endif

/* Append one argument. */
void kit_command_push(KitCommand *c, const char *arg);

/* Append multiple arguments at once (NULL-terminated varargs). */
void kit_command_push_all(KitCommand *c, ...);

/* Run synchronously. Returns false on failure. */
bool kit_command_run(KitCommand *c);

/* Run asynchronously. Returns the child process, or KIT_PROCESS_INVALID on
 * error. Call kit_process_wait to reap it. The KitCommand is NOT reset. */
KitProcess kit_command_spawn(KitCommand *c);

/* Wait for an async process. Returns false if the child exited non-zero. */
bool kit_process_wait(KitProcess p);

/* Run synchronously and capture the child's output.
 *
 *   kit_command_capture         stdout only; stderr passes through
 *   kit_command_capture_merged  both streams interleaved, as a terminal shows
 *   kit_command_capture_split   each stream into its own builder
 *
 * kit_command_capture_split takes either builder as NULL, in which case that
 * stream is left alone and passes through. Passing the same builder for both
 * is the same as kit_command_capture_merged.
 *
 * Two pipes are drained together rather than one after the other: reading
 * them in sequence deadlocks as soon as the child fills the one nobody is
 * reading, and 64 KiB of diagnostics is not a rare thing for a compiler.
 *
 * All three return false when the child fails to start or exits non-zero.
 * Whatever it managed to write is still appended. */
bool kit_command_capture(KitCommand *c, KitBuf *sb);
bool kit_command_capture_merged(KitCommand *c, KitBuf *sb);
bool kit_command_capture_split(KitCommand *c, KitBuf *out, KitBuf *err);

/* Variadic shorthand, terminated by NULL.
 * kit_command_run_args("ls", "-la", NULL); */
bool kit_command_run_args(const char *first, ...);

/* Clears the arguments but keeps the allocation, to reuse the KitCommand. */
void kit_command_reset(KitCommand *c);

void kit_command_free(KitCommand *c);

/* --------------------------------------------------------------------------
 * SECTION 12 : HASHING
 * -------------------------------------------------------------------------- */

/* djb2 over a NUL-terminated string, and over arbitrary bytes.
 *
 * Fast, but its low bits carry little entropy for keys sharing a prefix
 * ("key1", "key2", ...), which is exactly the pattern a power-of-two table
 * indexes on. Run the result through kit_hash_mix32 before masking it. */
uint32_t kit_hash_str(const char *s);
uint32_t kit_hash_bytes(const void *data, size_t len);

/* Avalanche step: spreads the entropy of a 32-bit hash across all of its bits
 * so that the low ones are usable as a bucket index. This is the murmur3
 * finalizer with Stafford's constants. */
uint32_t kit_hash_mix32(uint32_t h);

/* --------------------------------------------------------------------------
 * SECTION 13 : MAP
 *
 * String-keyed, void*-valued hash map (open addressing, linear probing).
 * Keys are NOT copied: the caller must ensure they outlive the map.
 *
 * Usage:
 *   KitMap hm = {0};
 *   kit_map_set(&hm, "foo", my_ptr);
 *   void *v = kit_map_get(&hm, "foo");   // NULL if absent
 *   kit_map_delete(&hm, "foo");
 *   kit_map_each(&hm, e) { printf("%s\n", e->key); }
 *   kit_map_free(&hm);
 * -------------------------------------------------------------------------- */

typedef struct {
    const char *key;
    void       *value;
} KitMapEntry;

typedef struct {
    KitMapEntry *entries;
    size_t    count;     /* live entries */
    size_t    used;      /* live entries + tombstones; drives the load factor */
    size_t    capacity;
} KitMap;

/* Insert or update. Returns true if the key is new. */
bool  kit_map_set(KitMap *hm, const char *key, void *value);

/* Returns the value, or NULL if the key is absent. */
void *kit_map_get(const KitMap *hm, const char *key);

/* Returns true if the key exists (safe even when stored value is NULL). */
bool  kit_map_has(const KitMap *hm, const char *key);

/* Removes the key. Returns true if it was present. */
bool  kit_map_delete(KitMap *hm, const char *key);

/* Returns true if entry `e` is a live (non-deleted) slot. */
bool  kit_map_entry_live(const KitMapEntry *e);

/* Drops every entry but keeps the allocation, ready for reuse. */
void  kit_map_reset(KitMap *hm);

void  kit_map_free(KitMap *hm);

/* Iterate over live entries.
 * Example:
 *   kit_map_each(&hm, e) { printf("%s -> %p\n", e->key, e->value); }
 */
#define kit_map_each(hm, it)                            \
    for (KitMapEntry *(it) = (hm)->entries;             \
         (it) < (hm)->entries + (hm)->capacity; ++(it)) \
        if (kit_map_entry_live(it))

/* --------------------------------------------------------------------------
 * SECTION 14 : PATHS
 * -------------------------------------------------------------------------- */

#ifdef _WIN32
#    define KIT_PATH_SEP '\\'
#else
#    define KIT_PATH_SEP '/'
#endif

/* Returns a pointer INTO path, with no allocation. */
const char *kit_path_basename(const char *path);

/* Returns a pointer to the extension (including '.'), or a pointer to the
 * terminating '\0' if there is no extension. No allocation. */
const char *kit_path_ext(const char *path);

/* Writes the directory component of path into buf[bufsz]. Returns buf. */
char *kit_path_dirname(const char *path, char *buf, size_t bufsz);

/* Joins two path components into buf[bufsz]. Returns buf. */
char *kit_path_join(char *buf, size_t bufsz, const char *a, const char *b);

/* Returns true if path is absolute. */
bool  kit_path_is_absolute(const char *path);

/* --------------------------------------------------------------------------
 * SECTION 15 : MATHS
 * -------------------------------------------------------------------------- */

#define KIT_MIN(a, b) ((a) < (b) ? (a) : (b))
#define KIT_MAX(a, b) ((a) > (b) ? (a) : (b))

/* clamp x to [lo, hi]. */
static inline float kit_clampf(float x, float lo, float hi) {
    return x < lo ? lo : x > hi ? hi : x;
}
static inline double kit_clampd(double x, double lo, double hi) {
    return x < lo ? lo : x > hi ? hi : x;
}
static inline int kit_clampi(int x, int lo, int hi) {
    return x < lo ? lo : x > hi ? hi : x;
}

/* Linear interpolation: lerp(a, b, 0) = a, lerp(a, b, 1) = b. */
static inline float kit_lerpf(float a, float b, float t) {
    return a + t * (b - a);
}

/* Re-map x from [in_lo, in_hi] to [out_lo, out_hi]. */
static inline float kit_remapf(float x,
                               float in_lo, float in_hi,
                               float out_lo, float out_hi) {
    if (in_hi == in_lo) return out_lo;
    return out_lo + (x - in_lo) * (out_hi - out_lo) / (in_hi - in_lo);
}

/* Degrees <-> radians. */
#define KIT_DEG2RAD(d) ((d) * (float)(3.14159265358979323846 / 180.0))
#define KIT_RAD2DEG(r) ((r) * (float)(180.0 / 3.14159265358979323846))

#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif /* KIT_H */

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

/* The declarations above sit behind KIT_H, but this block deliberately does
 * not, so that it can be emitted after the guard. It therefore needs a guard
 * of its own: without it, a translation unit that defines
 * KIT_IMPLEMENTATION and then reaches kit.h twice, directly and through
 * another header, emits every definition twice and fails to compile. */
#if defined(KIT_IMPLEMENTATION) && !defined(KIT__IMPLEMENTATION_DONE)
#define KIT__IMPLEMENTATION_DONE

/* --------------------------------------------------------------------------
 * Logging
 * -------------------------------------------------------------------------- */

static KitLogLevel      KIT__MIN_LEVEL = KIT_LOG_DEBUG;
static FILE         *KIT__OUTPUT    = NULL;  /* NULL -> stderr */
static KitLogColor  KIT__COLOR     = KIT_LOG_COLOR_AUTO;
static unsigned      KIT__FIELDS    = KIT_LOG_FIELDS_DEFAULT;
static uint64_t      KIT__RECORDS   = 0;

static const char *KIT__COLORS[] = {
    "\x1b[90m",  /* DEBUG    - grey          */
    "\x1b[34m",  /* INFO     - blue          */
    "\x1b[33m",  /* WARNING  - yellow        */
    "\x1b[31m",  /* ERROR    - red           */
    "\x1b[41m",  /* CRITICAL - red bg        */
};

static const char *KIT__LABELS[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "CRIT"
};

void kit_log_set_level(KitLogLevel level) {
    KIT__MIN_LEVEL = level;
}

void kit_log_set_output(FILE *fp) {
    KIT__OUTPUT = fp;
}

void kit_log_set_color(KitLogColor mode) {
    KIT__COLOR = mode;
}

void kit_log_set_fields(unsigned fields) {
    KIT__FIELDS = fields;
}

static bool kit__stream_is_tty(FILE *out) {
#ifdef _WIN32
    return _isatty(_fileno(out)) != 0;
#else
    return isatty(fileno(out)) != 0;
#endif
}

/* Escape sequences are worthless in a log file and actively harmful when the
 * output is grepped, so AUTO keeps them for terminals only. NO_COLOR is the
 * cross-tool convention (https://no-color.org). */
static bool kit__use_color(FILE *out) {
    switch (KIT__COLOR) {
        case KIT_LOG_COLOR_ALWAYS: return true;
        case KIT_LOG_COLOR_NEVER:  return false;
        default: break;
    }
    const char *no_color = getenv("NO_COLOR");
    if (no_color && *no_color) return false;
    return kit__stream_is_tty(out);
}

/* localtime() hands back a shared static buffer; the _r / _s variants keep the
 * logger usable from more than one thread. */
static void kit__strftime_now(char *buf, size_t bufsz, const char *fmt) {
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
static const char *kit__username(void) {
#ifdef _WIN32
    const char *name = getenv("USERNAME");
#else
    const char *name = getenv("LOGNAME");
    if (!name || !*name) name = getenv("USER");
#endif
    return (name && *name) ? name : "unknown";
}

/* stdio locks per call, so a record built from several fprintf calls can be
 * split down the middle by another thread. Holding the lock for the whole
 * record is what keeps a line intact. */
static void kit__stream_lock(FILE *out) {
#ifdef _WIN32
    _lock_file(out);
#else
    flockfile(out);
#endif
}

static void kit__stream_unlock(FILE *out) {
#ifdef _WIN32
    _unlock_file(out);
#else
    funlockfile(out);
#endif
}

/* Called with the stream already locked. */
static void kit__log_prefix(FILE *out, const char *color, const char *label,
                              const char *file, int line) {
    unsigned    fields = KIT__FIELDS;
    bool        color_on = kit__use_color(out);
    const char *on  = color_on ? color        : "";
    const char *dim = color_on ? "\x1b[90m"   : "";
    const char *off = color_on ? "\x1b[0m"    : "";

    if (fields & KIT_LOG_FIELD_COUNT)
        fprintf(out, "%s[%llu]%s ", dim, (unsigned long long)++KIT__RECORDS, off);

    if (fields & KIT_LOG_FIELD_DATE) {
        char buf[32];
        kit__strftime_now(buf, sizeof(buf), "%Y-%m-%d (%a)");
        fprintf(out, "%s[%s]%s ", on, buf, off);
    }

    if (fields & KIT_LOG_FIELD_TIME) {
        char buf[16];
        kit__strftime_now(buf, sizeof(buf), "%H:%M:%S");
        fprintf(out, "%s[%s]%s ", on, buf, off);
    }

    if (fields & KIT_LOG_FIELD_LEVEL)
        fprintf(out, "%s[%s]%s ", on, label, off);

    if (fields & KIT_LOG_FIELD_USER)
        fprintf(out, "%s[%s]%s ", dim, kit__username(), off);

    if (fields & KIT_LOG_FIELD_LOCATION)
        fprintf(out, "%s[%s:%d]%s ", dim, file, line, off);
}

void kit__log(KitLogLevel level, const char *file, int line,
                    const char *fmt, ...) {
    if (level < KIT__MIN_LEVEL) return;

    FILE *out = KIT__OUTPUT ? KIT__OUTPUT : stderr;

    kit__stream_lock(out);
    kit__log_prefix(out, KIT__COLORS[level], KIT__LABELS[level], file, line);

    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);

    fputc('\n', out);
    kit__stream_unlock(out);
}

void kit__panic(const char *file, int line, const char *fmt, ...) {
    FILE *out = KIT__OUTPUT ? KIT__OUTPUT : stderr;

    kit__stream_lock(out);
    kit__log_prefix(out, KIT__COLORS[KIT_LOG_CRITICAL], "PANIC", file, line);

    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);

    fprintf(out, "\nAborting...\n");
    fflush(out);
    kit__stream_unlock(out);
    abort();
}

/* --------------------------------------------------------------------------
 * Errors
 * -------------------------------------------------------------------------- */

/* The largest prefix of s[0, len) that does not end inside a UTF-8 sequence,
 * so that cutting a message never leaves half a character behind. */
static size_t kit__utf8_cut(const char *s, size_t len) {
    while (len > 0 && ((unsigned char)s[len] & 0xC0) == 0x80) len--;
    return len;
}

/* Marks a message that did not fit: cut at a character boundary and end it
 * with "..." so a reader can tell. */
static void kit__error_mark_cut(KitError *err) {
    size_t keep = sizeof(err->message) - 1 - 3;
    if (keep > err->length) keep = err->length;
    keep = kit__utf8_cut(err->message, keep);
    memcpy(err->message + keep, "...", 4);
    err->length    = keep + 3;
    err->truncated = true;
}

static void kit__error_vappend(KitError *err, const char *fmt, va_list ap) {
    size_t room = sizeof(err->message) - err->length;
    int    n    = vsnprintf(err->message + err->length, room, fmt, ap);
    if (n < 0) return;
    if ((size_t)n >= room) {
        err->length    = sizeof(err->message) - 1;
        err->truncated = true;
    } else {
        err->length += (size_t)n;
    }
}

static void kit__error_append(KitError *err, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kit__error_vappend(err, fmt, ap);
    va_end(ap);
}

/* Formats into err, or into a scratch KitError that is logged when err is
 * NULL. This is the one place the "report or log, never both" rule lives. */
static bool kit__error_record(KitError *err, KitErrorCode code, int native,
                              const char *detail, const char *fmt, va_list ap) {
    KitError  logged;
    KitError *target = err ? err : &logged;

    target->code      = code;
    target->native    = native;
    target->truncated = false;
    target->length    = 0;
    target->message[0] = '\0';

    kit__error_vappend(target, fmt, ap);
    if (detail && !target->truncated) kit__error_append(target, ": %s", detail);
    if (target->truncated) kit__error_mark_cut(target);

    if (!err) kit__log(KIT_LOG_ERROR, __FILE__, __LINE__, "%s", target->message);
    return false;
}

bool kit_error_set(KitError *err, KitErrorCode code, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kit__error_record(err, code, 0, NULL, fmt, ap);
    va_end(ap);
    return false;
}

bool kit_error_errno(KitError *err, int errnum, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kit__error_record(err, kit_error_code_from_errno(errnum), errnum,
                      strerror(errnum), fmt, ap);
    va_end(ap);
    return false;
}

bool kit_error_context(KitError *err, const char *fmt, ...) {
    if (!err || err->code == KIT_OK) return false;

    size_t cap = sizeof(err->message);
    char   prefix[KIT_ERROR_CAPACITY];

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(prefix, sizeof(prefix), fmt, ap);
    va_end(ap);
    if (n < 0) return false;

    size_t plen = (size_t)n < sizeof(prefix) ? (size_t)n : sizeof(prefix) - 1;

    /* Everything fits: shift the message right and write the prefix in. */
    if (plen + 2 + err->length <= cap - 1) {
        memmove(err->message + plen + 2, err->message, err->length + 1);
        memcpy(err->message, prefix, plen);
        memcpy(err->message + plen, ": ", 2);
        err->length += plen + 2;
        return false;
    }

    /* It does not. The context may take half of the room at most, and the
     * rest goes to the tail of the existing message, which is where its root
     * cause is, behind an ellipsis that marks the elided middle. */
    char   joined[KIT_ERROR_CAPACITY];
    size_t half = (cap - 1) / 2;
    size_t used = 0;

    if (plen > half) {
        size_t keep = kit__utf8_cut(prefix, half - 3);
        memcpy(joined, prefix, keep);
        memcpy(joined + keep, "...", 3);
        used = keep + 3;
    } else {
        memcpy(joined, prefix, plen);
        used = plen;
    }
    memcpy(joined + used, ": ...", 5);
    used += 5;

    size_t room  = cap - 1 - used;
    size_t start = err->length > room ? err->length - room : 0;
    while (start < err->length && ((unsigned char)err->message[start] & 0xC0) == 0x80) start++;

    size_t tail = err->length - start;
    memcpy(joined + used, err->message + start, tail);
    used += tail;
    joined[used] = '\0';

    memcpy(err->message, joined, used + 1);
    err->length    = used;
    err->truncated = true;
    return false;
}

void kit_error_clear(KitError *err) {
    if (!err) return;
    err->code       = KIT_OK;
    err->native     = 0;
    err->truncated  = false;
    err->length     = 0;
    err->message[0] = '\0';
}

const char *kit_error_code_name(KitErrorCode code) {
    switch (code) {
        case KIT_OK:              return "ok";
        case KIT_ERR_NOT_FOUND:   return "not_found";
        case KIT_ERR_EXISTS:      return "exists";
        case KIT_ERR_NOT_EMPTY:   return "not_empty";
        case KIT_ERR_WRONG_KIND:  return "wrong_kind";
        case KIT_ERR_PERMISSION:  return "permission";
        case KIT_ERR_INVALID:     return "invalid";
        case KIT_ERR_RANGE:       return "range";
        case KIT_ERR_NO_SPACE:    return "no_space";
        case KIT_ERR_NO_MEMORY:   return "no_memory";
        case KIT_ERR_BUSY:        return "busy";
        case KIT_ERR_INTERRUPTED: return "interrupted";
        case KIT_ERR_IO:          return "io";
        case KIT_ERR_PROCESS:     return "process";
        case KIT_ERR_UNSUPPORTED: return "unsupported";
        case KIT_ERR_OTHER:       return "other";
    }
    return "unknown";
}

/* An if chain rather than a switch: several of these share a value on some
 * systems (EAGAIN and EWOULDBLOCK, EEXIST and ENOTEMPTY on AIX), and some do
 * not exist at all on Windows, hence the guards. */
KitErrorCode kit_error_code_from_errno(int e) {
    if (e == 0)            return KIT_OK;
    if (e == ENOENT)       return KIT_ERR_NOT_FOUND;
    if (e == EEXIST)       return KIT_ERR_EXISTS;
#ifdef ENOTEMPTY
    if (e == ENOTEMPTY)    return KIT_ERR_NOT_EMPTY;
#endif
    if (e == EISDIR)       return KIT_ERR_WRONG_KIND;
    if (e == ENOTDIR)      return KIT_ERR_WRONG_KIND;
    if (e == EACCES)       return KIT_ERR_PERMISSION;
    if (e == EPERM)        return KIT_ERR_PERMISSION;
    if (e == EROFS)        return KIT_ERR_PERMISSION;
    if (e == EINVAL)       return KIT_ERR_INVALID;
    if (e == EILSEQ)       return KIT_ERR_INVALID;
    if (e == ERANGE)       return KIT_ERR_RANGE;
    if (e == ENAMETOOLONG) return KIT_ERR_RANGE;
    if (e == E2BIG)        return KIT_ERR_RANGE;
    if (e == EFBIG)        return KIT_ERR_RANGE;
#ifdef EOVERFLOW
    if (e == EOVERFLOW)    return KIT_ERR_RANGE;
#endif
    if (e == ENOSPC)       return KIT_ERR_NO_SPACE;
#ifdef EDQUOT
    if (e == EDQUOT)       return KIT_ERR_NO_SPACE;
#endif
    if (e == ENOMEM)       return KIT_ERR_NO_MEMORY;
    if (e == EBUSY)        return KIT_ERR_BUSY;
#ifdef ETXTBSY
    if (e == ETXTBSY)      return KIT_ERR_BUSY;
#endif
    if (e == EINTR)        return KIT_ERR_INTERRUPTED;
    if (e == EAGAIN)       return KIT_ERR_INTERRUPTED;
#ifdef ETIMEDOUT
    if (e == ETIMEDOUT)    return KIT_ERR_INTERRUPTED;
#endif
    if (e == EIO)          return KIT_ERR_IO;
    if (e == ENOSYS)       return KIT_ERR_UNSUPPORTED;
    if (e == EXDEV)        return KIT_ERR_UNSUPPORTED;
#ifdef ENOTSUP
    if (e == ENOTSUP)      return KIT_ERR_UNSUPPORTED;
#endif
    return KIT_ERR_OTHER;
}


#ifdef _WIN32
static KitErrorCode kit__error_code_from_win32(DWORD e) {
    switch (e) {
        case ERROR_SUCCESS:             return KIT_OK;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_DRIVE:       return KIT_ERR_NOT_FOUND;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:         return KIT_ERR_EXISTS;
        case ERROR_DIR_NOT_EMPTY:       return KIT_ERR_NOT_EMPTY;
        case ERROR_DIRECTORY:           return KIT_ERR_WRONG_KIND;
        case ERROR_ACCESS_DENIED:
        case ERROR_WRITE_PROTECT:       return KIT_ERR_PERMISSION;
        case ERROR_INVALID_NAME:
        case ERROR_INVALID_PARAMETER:   return KIT_ERR_INVALID;
        case ERROR_FILENAME_EXCED_RANGE:
        case ERROR_BUFFER_OVERFLOW:     return KIT_ERR_RANGE;
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL:    return KIT_ERR_NO_SPACE;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:         return KIT_ERR_NO_MEMORY;
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
        case ERROR_BUSY:                return KIT_ERR_BUSY;
        case ERROR_NOT_SAME_DEVICE:
        case ERROR_NOT_SUPPORTED:       return KIT_ERR_UNSUPPORTED;
        default:                        return KIT_ERR_OTHER;
    }
}

/* The Win32 counterpart of kit_error_errno, for API calls that report
 * through GetLastError rather than errno. */
static bool kit__error_win32(KitError *err, DWORD code, const char *fmt, ...) {
    /* The wide API and an explicit conversion, because FormatMessageA answers
     * in the ANSI code page and the message must stay UTF-8. English is asked
     * for first, to match what strerror gives under the default C locale and
     * not splice a translated clause onto an English sentence; the user's own
     * language is the fallback where English resources are not installed. */
    wchar_t wide[256];
    DWORD   flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD   wn    = FormatMessageW(flags, NULL, code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                                   wide, (DWORD)(sizeof(wide) / sizeof(wide[0])), NULL);
    if (wn == 0) wn = FormatMessageW(flags, NULL, code, 0,
                                     wide, (DWORD)(sizeof(wide) / sizeof(wide[0])), NULL);

    char detail[512];
    int  n = wn ? WideCharToMultiByte(CP_UTF8, 0, wide, (int)wn, detail,
                                      (int)sizeof(detail) - 1, NULL, NULL)
                : 0;
    if (n < 0) n = 0;
    detail[n] = '\0';

    /* System messages end with ".\r\n", which reads badly mid-sentence. */
    while (n > 0 && (detail[n - 1] == '\n' || detail[n - 1] == '\r' || detail[n - 1] == '.'))
        detail[--n] = '\0';
    if (n == 0) snprintf(detail, sizeof(detail), "system error %lu", (unsigned long)code);

    va_list ap;
    va_start(ap, fmt);
    kit__error_record(err, kit__error_code_from_win32(code), (int)code, detail, fmt, ap);
    va_end(ap);
    return false;
}
#endif

/* Records the failure of the operating system call that just returned: errno
 * on POSIX, GetLastError on Windows. The value is read before any argument is
 * formatted, so the formatting cannot clobber it. */
#ifdef _WIN32
#    define kit__error_os(err, ...) kit__error_win32((err), GetLastError(), __VA_ARGS__)
#else
#    define kit__error_os(err, ...) kit_error_errno((err), errno, __VA_ARGS__)
#endif

/* --------------------------------------------------------------------------
 * Files
 * -------------------------------------------------------------------------- */

#ifndef KIT_READ_CHUNK
#define KIT_READ_CHUNK 65536
#endif

/* Defined with the path helpers further down; needed by the filesystem. */
static bool kit__path_is_sep(char c);

/* Windows reports a path that runs through an existing file as not found,
 * POSIX as "not a directory". A caller should get one answer whatever the
 * platform, so a not-found whose path passes through a file is reclassified,
 * and the message says which component is in the way. Only on the failure
 * path, so the extra lookups cost nothing when things work. */
static void kit__error_refine_path(KitError *err, const char *path) {
    if (!err || err->code != KIT_ERR_NOT_FOUND || !path || !path[0]) return;

    KitBuf prefix = KIT_ZEROED;
    for (const char *p = path + 1; *p; ++p) {
        if (!kit__path_is_sep(*p)) continue;
        kit_buf_reset(&prefix);
        kit_buf_append_n(&prefix, path, (size_t)(p - path));

        KitFileKind kind = kit_fs_kind(kit_buf_cstr(&prefix));
        if (kind == KIT_FILE_KIND_NONE) break;       /* nothing deeper can exist */
        if (kind != KIT_FILE_KIND_DIRECTORY) {
            err->code = KIT_ERR_WRONG_KIND;
            kit__error_append(err, " ('%s' is not a directory)", kit_buf_cstr(&prefix));
            if (err->truncated) kit__error_mark_cut(err);
            break;
        }
    }
    kit_buf_free(&prefix);
}

/* Streamed so that the buffer never depends on ftell(): pipes, terminals and
 * the /proc files all report a size of 0 while still delivering data. The
 * reported size, when plausible, is only used to seed the capacity. */
char *kit_fs_read_sized(const char *path, size_t *out_size, KitError *err) {
    bool   result = true;
    FILE  *f      = NULL;
    char  *buffer = NULL;
    size_t len    = 0;
    size_t cap    = 0;

    f = fopen(path, "rb");
    if (!f) {
        kit_error_errno(err, errno, "cannot open '%s' for reading", path);
        kit__error_refine_path(err, path);
        KIT_BAIL(false);
    }

    if (fseek(f, 0, SEEK_END) == 0) {
        long hint = ftell(f);
        if (hint > 0) cap = (size_t)hint;
        rewind(f);
    }
    if (cap == 0) cap = KIT_READ_CHUNK;

    buffer = (char *)malloc(cap + 1);
    if (!buffer) {
        kit_error_set(err, KIT_ERR_NO_MEMORY, "out of memory reading '%s'", path);
        KIT_BAIL(false);
    }

    for (;;) {
        if (len == cap) {
            if (cap > SIZE_MAX / 2 - 1) {
                kit_error_set(err, KIT_ERR_RANGE, "'%s' is too large to read into memory", path);
                KIT_BAIL(false);
            }
            cap *= 2;
            char *grown = (char *)realloc(buffer, cap + 1);
            if (!grown) {
                kit_error_set(err, KIT_ERR_NO_MEMORY, "out of memory reading '%s'", path);
                KIT_BAIL(false);
            }
            buffer = grown;
        }

        size_t n = fread(buffer + len, 1, cap - len, f);
        len += n;
        if (n == 0) break;
    }

    if (ferror(f)) {
        kit_error_errno(err, errno, "cannot read '%s'", path);
        KIT_BAIL(false);
    }

    buffer[len] = '\0';

cleanup:
    if (f) fclose(f);
    if (!result) { free(buffer); return NULL; }
    if (out_size) *out_size = len;
    return buffer;
}

char *kit_fs_read(const char *path, KitError *err) {
    return kit_fs_read_sized(path, NULL, err);
}

/* A buffered write only reaches the disk on fclose, so its return value is the
 * one that reports a full filesystem or a failing quota. */
bool kit_fs_write(const char *path, const void *data, size_t size, KitError *err) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        kit_error_errno(err, errno, "cannot open '%s' for writing", path);
        kit__error_refine_path(err, path);
        return false;
    }

    if (size > 0 && fwrite(data, 1, size, f) != size) {
        /* Captured before fclose, which may overwrite errno on its way out. */
        int cause = errno;
        fclose(f);
        return kit_error_errno(err, cause, "cannot write to '%s'", path);
    }

    if (fclose(f) != 0) return kit_error_errno(err, errno, "cannot finish writing '%s'", path);
    return true;
}

bool kit_fs_is_file(const char *path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES &&
           !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

int64_t kit_fs_size(const char *path, KitError *err) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) {
        kit__error_os(err, "cannot read the size of '%s'", path);
        kit__error_refine_path(err, path);
        return -1;
    }
    return (int64_t)(((ULONGLONG)info.nFileSizeHigh << 32) | info.nFileSizeLow);
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        kit__error_os(err, "cannot read the size of '%s'", path);
        kit__error_refine_path(err, path);
        return -1;
    }
    return (int64_t)st.st_size;
#endif
}

bool kit_str_eq_nocase(KitStr a, KitStr b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; ++i) {
        int ca = tolower((unsigned char)a.data[i]);
        int cb = tolower((unsigned char)b.data[i]);
        if (ca != cb) return false;
    }
    return true;
}

size_t kit_str_find_char(KitStr sv, char c) {
    for (size_t i = 0; i < sv.count; ++i)
        if (sv.data[i] == c) return i;
    return KIT_NPOS;
}

size_t kit_str_find(KitStr sv, KitStr needle) {
    if (needle.count == 0)      return 0;
    if (needle.count > sv.count) return KIT_NPOS;

    for (size_t i = 0; i + needle.count <= sv.count; ++i)
        if (memcmp(sv.data + i, needle.data, needle.count) == 0) return i;
    return KIT_NPOS;
}

bool kit_str_contains(KitStr sv, KitStr needle) {
    return kit_str_find(sv, needle) != KIT_NPOS;
}

KitStr kit_str_cut_str(KitStr *sv, KitStr delim) {
    size_t at = kit_str_find(*sv, delim);
    if (at == KIT_NPOS) {
        KitStr all = *sv;
        sv->data  += sv->count;
        sv->count  = 0;
        return all;
    }

    KitStr head = { sv->data, at };
    sv->data  += at + delim.count;
    sv->count -= at + delim.count;
    return head;
}

KitStr kit_str_take_right(KitStr *sv, size_t n) {
    if (n > sv->count) n = sv->count;
    KitStr tail = { sv->data + sv->count - n, n };
    sv->count -= n;
    return tail;
}

bool kit_str_next(KitStr *sv, char delim, KitStr *out) {
    if (sv->count == 0) return false;
    *out = kit_str_cut(sv, delim);
    return true;
}

/* Shared by the integer parsers: consumes digits and reports overflow against
 * the caller's ceiling, so the signed and unsigned limits are both honoured
 * without ever computing an out-of-range value. */
static bool kit__str_parse_digits(KitStr sv, uint64_t limit, uint64_t *out) {
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

bool kit_str_to_u64(KitStr sv, uint64_t *out) {
    if (sv.count > 0 && sv.data[0] == '+') { sv.data += 1; sv.count -= 1; }
    uint64_t value;
    if (!kit__str_parse_digits(sv, UINT64_MAX, &value)) return false;
    *out = value;
    return true;
}

bool kit_str_to_i64(KitStr sv, int64_t *out) {
    bool negative = false;
    if (sv.count > 0 && (sv.data[0] == '-' || sv.data[0] == '+')) {
        negative = (sv.data[0] == '-');
        sv.data  += 1;
        sv.count -= 1;
    }

    /* The magnitude of INT64_MIN is one past INT64_MAX, hence the two limits. */
    uint64_t limit = negative ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX;
    uint64_t value;
    if (!kit__str_parse_digits(sv, limit, &value)) return false;

    *out = negative ? (int64_t)(~value + 1) : (int64_t)value;
    return true;
}

bool kit_str_to_double(KitStr sv, double *out) {
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

char *kit_str_dup(KitStr sv) {
    char *copy = (char *)malloc(sv.count + 1);
    if (!copy) KIT_PANIC("kit_str_dup: malloc of %zu bytes failed", sv.count + 1);
    if (sv.count > 0) memcpy(copy, sv.data, sv.count);
    copy[sv.count] = '\0';
    return copy;
}

/* --------------------------------------------------------------------------
 * Filesystem
 * -------------------------------------------------------------------------- */

KitFileKind kit_fs_kind(const char *path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES)      return KIT_FILE_KIND_NONE;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)      return KIT_FILE_KIND_DIRECTORY;
    return KIT_FILE_KIND_REGULAR;
#else
    struct stat st;
    if (stat(path, &st) != 0)                 return KIT_FILE_KIND_NONE;
    if (S_ISREG(st.st_mode))                  return KIT_FILE_KIND_REGULAR;
    if (S_ISDIR(st.st_mode))                  return KIT_FILE_KIND_DIRECTORY;
    return KIT_FILE_KIND_OTHER;
#endif
}

bool kit_fs_is_dir(const char *path) {
    return kit_fs_kind(path) == KIT_FILE_KIND_DIRECTORY;
}

/* Creates one component. An existing directory is a success, which is what
 * makes kit_fs_mkdir idempotent. */
static bool kit__mkdir_one(const char *path, KitError *err) {
#ifdef _WIN32
    if (CreateDirectoryA(path, NULL)) return true;
    DWORD cause = GetLastError();
    if (cause != ERROR_ALREADY_EXISTS)
        return kit__error_win32(err, cause, "cannot create directory '%s'", path);
#else
    if (mkdir(path, 0777) == 0) return true;
    if (errno != EEXIST) return kit_error_errno(err, errno, "cannot create directory '%s'", path);
#endif
    if (kit_fs_is_dir(path)) return true;
    return kit_error_set(err, KIT_ERR_WRONG_KIND,
                         "cannot create directory '%s': a file is in the way", path);
}

bool kit_fs_mkdir(const char *path, KitError *err) {
    if (!path || !*path)
        return kit_error_set(err, KIT_ERR_INVALID, "cannot create a directory from an empty path");

    bool        result = true;
    KitBuf      sb     = KIT_ZEROED;
    const char *p      = path;

    /* Carry the leading separators over verbatim so that an absolute path
     * stays absolute and a UNC prefix survives. */
    while (kit__path_is_sep(*p)) kit_buf_append_char(&sb, *p++);

    while (*p) {
        const char *start = p;
        while (*p && !kit__path_is_sep(*p)) p++;
        kit_buf_append_n(&sb, start, (size_t)(p - start));
        while (kit__path_is_sep(*p)) p++;

        const char *so_far = kit_buf_cstr(&sb);
        /* A bare Windows drive ("C:") is not a directory anyone can create. */
        bool is_drive = (sb.count == 2 && so_far[1] == ':');
        if (!is_drive && !kit__mkdir_one(so_far, err)) KIT_BAIL(false);

        if (*p) kit_buf_append_char(&sb, KIT_PATH_SEP);
    }

cleanup:
    kit_buf_free(&sb);
    return result;
}

bool kit_fs_copy(const char *src, const char *dst, KitError *err) {
#ifdef _WIN32
    if (CopyFileA(src, dst, FALSE)) return true;
    kit__error_os(err, "cannot copy '%s' to '%s'", src, dst);
    kit__error_refine_path(err, src);
    kit__error_refine_path(err, dst);
    return false;
#else
    bool result = true;
    int  in = -1, out = -1;

    in = open(src, O_RDONLY);
    if (in < 0) {
        kit_error_errno(err, errno, "cannot open '%s' for copying", src);
        kit__error_refine_path(err, src);
        KIT_BAIL(false);
    }

    struct stat st;
    if (fstat(in, &st) != 0) {
        kit_error_errno(err, errno, "cannot read the attributes of '%s'", src);
        KIT_BAIL(false);
    }

    /* The mode is applied at creation rather than after, so the file is never
     * briefly visible with wider permissions than the source. */
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
    if (out < 0) {
        kit_error_errno(err, errno, "cannot create '%s'", dst);
        kit__error_refine_path(err, dst);
        KIT_BAIL(false);
    }

    char buf[KIT_READ_CHUNK];
    for (;;) {
        ssize_t n = read(in, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0) {
            if (errno == EINTR) continue;
            kit_error_errno(err, errno, "cannot read '%s'", src);
            KIT_BAIL(false);
        }
        for (ssize_t off = 0; off < n; ) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if (w < 0) {
                if (errno == EINTR) continue;
                kit_error_errno(err, errno, "cannot write to '%s'", dst);
                KIT_BAIL(false);
            }
            off += w;
        }
    }

cleanup:
    if (in >= 0) close(in);
    /* close() is where a deferred write error surfaces, so it is checked;
     * but an earlier failure is the one worth reporting. */
    if (out >= 0 && close(out) != 0 && result) {
        kit_error_errno(err, errno, "cannot finish writing '%s'", dst);
        result = false;
    }
    return result;
#endif
}

bool kit_fs_remove(const char *path, KitError *err) {
#ifdef _WIN32
    if (DeleteFileA(path)) return true;
    DWORD cause = GetLastError();
#else
    /* unlink rather than remove: remove() also takes an empty directory on
     * POSIX and not on Windows, and one behaviour on both is worth more. */
    if (unlink(path) == 0) return true;
    int cause = errno;
#endif
    /* Linux says EISDIR, macOS EPERM and Windows ERROR_ACCESS_DENIED for the
     * same mistake; the caller should see one answer. */
    if (kit_fs_is_dir(path))
        return kit_error_set(err, KIT_ERR_WRONG_KIND,
                             "cannot remove '%s': it is a directory", path);
#ifdef _WIN32
    kit__error_win32(err, cause, "cannot remove '%s'", path);
#else
    kit_error_errno(err, cause, "cannot remove '%s'", path);
#endif
    kit__error_refine_path(err, path);
    return false;
}

bool kit_fs_rmdir(const char *path, KitError *err) {
#ifdef _WIN32
    if (RemoveDirectoryA(path)) return true;
    DWORD cause = GetLastError();
#else
    if (rmdir(path) == 0) return true;
    int cause = errno;
#endif
    KitFileKind kind = kit_fs_kind(path);
    if (kind == KIT_FILE_KIND_REGULAR || kind == KIT_FILE_KIND_OTHER)
        return kit_error_set(err, KIT_ERR_WRONG_KIND,
                             "cannot remove directory '%s': it is not a directory", path);
#ifdef _WIN32
    kit__error_win32(err, cause, "cannot remove directory '%s'", path);
#else
    /* POSIX allows EEXIST as well as ENOTEMPTY for a directory with entries. */
    if (cause == EEXIST && kind == KIT_FILE_KIND_DIRECTORY) cause = ENOTEMPTY;
    kit_error_errno(err, cause, "cannot remove directory '%s'", path);
#endif
    kit__error_refine_path(err, path);
    return false;
}

bool kit_fs_rename(const char *from, const char *to, KitError *err) {
#ifdef _WIN32
    /* Plain rename() refuses an existing destination on Windows. */
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING)) return true;
#else
    if (rename(from, to) == 0) return true;
#endif
    kit__error_os(err, "cannot rename '%s' to '%s'", from, to);
    kit__error_refine_path(err, from);
    kit__error_refine_path(err, to);
    return false;
}

/* Modification time at the finest resolution the platform exposes. A build
 * driven by whole seconds misses a rebuild whenever an input and its output
 * are written inside the same second, which is common on a fast machine. */
typedef struct {
    int64_t sec;
    int32_t nsec;
} Kit__Mtime;

static bool kit__mtime(const char *path, Kit__Mtime *out) {
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

int64_t kit_fs_mtime(const char *path, KitError *err) {
    Kit__Mtime t;
    if (!kit__mtime(path, &t)) {
        kit__error_os(err, "cannot read the modification time of '%s'", path);
        kit__error_refine_path(err, path);
        return -1;
    }
    return t.sec;
}

int kit_fs_stale(const char *output, const char *const *inputs, size_t n_inputs,
                 KitError *err) {
    Kit__Mtime out_time;
    if (!kit__mtime(output, &out_time)) return 1;   /* missing: must build */

    for (size_t i = 0; i < n_inputs; ++i) {
        Kit__Mtime in_time;
        if (!kit__mtime(inputs[i], &in_time)) {
            kit__error_os(err, "cannot read the modification time of input '%s'", inputs[i]);
            kit__error_refine_path(err, inputs[i]);
            return -1;
        }
        /* ">=" and not ">": same-timestamp means the ordering is unknown, and
         * rebuilding needlessly is cheaper than shipping a stale artifact. */
        if (in_time.sec  >  out_time.sec) return 1;
        if (in_time.sec  == out_time.sec && in_time.nsec >= out_time.nsec) return 1;
    }
    return 0;
}

int kit_fs_stale_list(const char *output, const KitFileList *inputs, KitError *err) {
    return kit_fs_stale(output, (const char *const *)inputs->items, inputs->count, err);
}

void kit_file_list_free(KitFileList *list) {
    for (size_t i = 0; i < list->count; ++i) free(list->items[i]);
    kit_array_free(list);
}

static int kit__cmp_cstr(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static char *kit__strdup(const char *s) {
    size_t n    = strlen(s) + 1;
    char  *copy = (char *)malloc(n);
    if (!copy) KIT_PANIC("kit_fs_list: out of memory");
    memcpy(copy, s, n);
    return copy;
}

bool kit_fs_list(const char *path, KitFileList *out, KitError *err) {
    /* Entries land in a scratch list first, so a failure halfway through
     * leaves the caller's list exactly as it was. */
    KitFileList found = KIT_ZEROED;

#ifdef _WIN32
    char pattern[MAX_PATH];
    if (snprintf(pattern, sizeof(pattern), "%s\\*", path) >= (int)sizeof(pattern))
        return kit_error_set(err, KIT_ERR_RANGE, "cannot list '%s': the path is too long", path);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD cause = GetLastError();
        KitFileKind kind = kit_fs_kind(path);
        if (kind == KIT_FILE_KIND_REGULAR || kind == KIT_FILE_KIND_OTHER)
            return kit_error_set(err, KIT_ERR_WRONG_KIND,
                                 "cannot list '%s': it is not a directory", path);
        kit__error_win32(err, cause, "cannot list '%s'", path);
        kit__error_refine_path(err, path);
        return false;
    }
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        kit_array_push(&found, kit__strdup(fd.cFileName));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *dir = opendir(path);
    if (!dir) {
        kit_error_errno(err, errno, "cannot list '%s'", path);
        kit__error_refine_path(err, path);
        return false;
    }

    errno = 0;
    for (struct dirent *e = readdir(dir); e != NULL; e = readdir(dir)) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        kit_array_push(&found, kit__strdup(e->d_name));
        errno = 0;
    }
    /* readdir returns NULL both at the end and on failure; errno tells them
     * apart, which is why it is cleared before each call. */
    int cause = errno;
    closedir(dir);
    if (cause != 0) {
        kit_file_list_free(&found);
        return kit_error_errno(err, cause, "cannot list '%s'", path);
    }
#endif

    if (found.count > 1)
        qsort(found.items, found.count, sizeof(*found.items), kit__cmp_cstr);

    kit_array_push_many(out, found.items, found.count);
    kit_array_free(&found);   /* the names themselves now belong to `out` */
    return true;
}

/* --------------------------------------------------------------------------
 * Strings
 * -------------------------------------------------------------------------- */

KitStr kit_str_from(const char *cstr) {
    KitStr sv = { cstr, strlen(cstr) };
    return sv;
}

KitStr kit_str_from_parts(const char *data, size_t count) {
    KitStr sv = { data, count };
    return sv;
}

KitStr kit_str_trim_left(KitStr sv) {
    size_t i = 0;
    while (i < sv.count && isspace((unsigned char)sv.data[i])) i++;
    KitStr out = { sv.data + i, sv.count - i };
    return out;
}

KitStr kit_str_trim_right(KitStr sv) {
    size_t i = 0;
    while (i < sv.count && isspace((unsigned char)sv.data[sv.count - 1 - i])) i++;
    KitStr out = { sv.data, sv.count - i };
    return out;
}

KitStr kit_str_trim(KitStr sv) {
    return kit_str_trim_right(kit_str_trim_left(sv));
}

KitStr kit_str_cut(KitStr *sv, char delim) {
    size_t i = 0;
    while (i < sv->count && sv->data[i] != delim) i++;
    KitStr result = { sv->data, i };
    if (i < sv->count) {
        sv->data  += i + 1;
        sv->count -= i + 1;
    } else {
        sv->data  += i;
        sv->count -= i;
    }
    return result;
}

KitStr kit_str_take(KitStr *sv, size_t n) {
    if (n > sv->count) n = sv->count;
    KitStr result = { sv->data, n };
    sv->data  += n;
    sv->count -= n;
    return result;
}

bool kit_str_eq(KitStr a, KitStr b) {
    if (a.count != b.count) return false;
    return memcmp(a.data, b.data, a.count) == 0;
}

bool kit_str_eq_cstr(KitStr a, const char *b) {
    return kit_str_eq(a, kit_str_from(b));
}

bool kit_str_starts_with(KitStr sv, KitStr prefix) {
    if (prefix.count > sv.count) return false;
    return memcmp(sv.data, prefix.data, prefix.count) == 0;
}

bool kit_str_ends_with(KitStr sv, KitStr suffix) {
    if (suffix.count > sv.count) return false;
    return memcmp(sv.data + sv.count - suffix.count,
                  suffix.data, suffix.count) == 0;
}

bool kit_str_starts_with_cstr(KitStr sv, const char *prefix) {
    return kit_str_starts_with(sv, kit_str_from(prefix));
}

bool kit_str_ends_with_cstr(KitStr sv, const char *suffix) {
    return kit_str_ends_with(sv, kit_str_from(suffix));
}

/* --------------------------------------------------------------------------
 * Arena
 * -------------------------------------------------------------------------- */

/* Regions carry their payload in the same allocation as their header, so a
 * region costs one malloc. */
static KitArenaRegion *kit__arena_new_region(size_t capacity) {
    if (capacity > SIZE_MAX - sizeof(KitArenaRegion))
        KIT_PANIC("arena: region of %zu bytes is too large", capacity);

    KitArenaRegion *r = (KitArenaRegion *)malloc(sizeof(KitArenaRegion) + capacity);
    if (!r) KIT_PANIC("arena: malloc of %zu bytes failed", capacity);

    r->next     = NULL;
    r->capacity = capacity;
    r->used     = 0;
    return r;
}

static void kit__arena_append_region(KitArena *a, size_t min_capacity) {
    size_t hint     = a->region_size ? a->region_size : (size_t)KIT_ARENA_REGION_SIZE;
    size_t capacity = min_capacity > hint ? min_capacity : hint;

    KitArenaRegion *r = kit__arena_new_region(capacity);
    if (a->current) a->current->next = r;
    else            a->first         = r;
    a->current = r;
}

KitArena kit_arena_make(size_t size) {
    KitArena a = { NULL, NULL, size };   /* first, current, region_size */
    if (size > 0) kit__arena_append_region(&a, size);
    return a;
}

void *kit_arena_alloc_aligned(KitArena *a, size_t size, size_t align) {
    if (align == 0 || (align & (align - 1)) != 0)
        KIT_PANIC("kit_arena_alloc_aligned: alignment %zu is not a power of two", align);
    if (size > SIZE_MAX - align)
        KIT_PANIC("kit_arena_alloc_aligned: request of %zu bytes is too large", size);

    for (;;) {
        KitArenaRegion *r = a->current;
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
             * has it, which is what makes kit_arena_reset cheap. */
            if (r->next) {
                a->current = r->next;
                continue;
            }
        }
        kit__arena_append_region(a, size + align);
    }
}

void *kit_arena_alloc(KitArena *a, size_t size) {
    return kit_arena_alloc_aligned(a, size, sizeof(max_align_t));
}

char *kit_arena_strndup(KitArena *a, const char *s, size_t n) {
    char *copy = (char *)kit_arena_alloc_aligned(a, n + 1, 1);
    if (n > 0) memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}

char *kit_arena_strdup(KitArena *a, const char *s) {
    return kit_arena_strndup(a, s, strlen(s));
}

char *kit_arena_printf(KitArena *a, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (n < 0) KIT_PANIC("kit_arena_printf: encoding error");

    char *out = (char *)kit_arena_alloc_aligned(a, (size_t)n + 1, 1);
    va_start(args, fmt);
    vsnprintf(out, (size_t)n + 1, fmt, args);
    va_end(args);
    return out;
}

size_t kit_arena_used(const KitArena *a) {
    size_t total = 0;
    for (const KitArenaRegion *r = a->first; r; r = r->next) {
        total += r->used;
        if (r == a->current) break;   /* regions past current are not in use */
    }
    return total;
}

size_t kit_arena_capacity(const KitArena *a) {
    size_t total = 0;
    for (const KitArenaRegion *r = a->first; r; r = r->next) total += r->capacity;
    return total;
}

void kit_arena_reset(KitArena *a) {
    for (KitArenaRegion *r = a->first; r; r = r->next) r->used = 0;
    a->current = a->first;
}

void kit_arena_free(KitArena *a) {
    KitArenaRegion *r = a->first;
    while (r) {
        KitArenaRegion *next = r->next;
        free(r);
        r = next;
    }
    a->first       = NULL;
    a->current     = NULL;
    a->region_size = 0;
}

KitArenaMark kit_arena_mark(const KitArena *a) {
    KitArenaMark m = { a->current, a->current ? a->current->used : 0 };
    return m;
}

void kit_arena_rewind(KitArena *a, KitArenaMark mark) {
    if (!mark.region) {
        kit_arena_reset(a);
        return;
    }
    mark.region->used = mark.used;
    for (KitArenaRegion *r = mark.region->next; r; r = r->next) r->used = 0;
    a->current = mark.region;
}

/* --------------------------------------------------------------------------
 * Scratch memory
 * -------------------------------------------------------------------------- */

/* One arena per thread: see the header for who is expected to free it. */
static KIT_THREAD_LOCAL KitArena KIT__TEMP = KIT_ZEROED;

void *kit_scratch_alloc(size_t size) {
    return kit_arena_alloc(&KIT__TEMP, size);
}

char *kit_scratch_strdup(const char *s) {
    return kit_arena_strdup(&KIT__TEMP, s);
}

char *kit_scratch_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (n < 0) KIT_PANIC("kit_scratch_printf: encoding error");

    char *out = (char *)kit_arena_alloc_aligned(&KIT__TEMP, (size_t)n + 1, 1);
    va_start(args, fmt);
    vsnprintf(out, (size_t)n + 1, fmt, args);
    va_end(args);
    return out;
}

KitArenaMark kit_scratch_mark(void)             { return kit_arena_mark(&KIT__TEMP); }
void       kit_scratch_rewind(KitArenaMark m)   { kit_arena_rewind(&KIT__TEMP, m); }
void       kit_scratch_reset(void)            { kit_arena_reset(&KIT__TEMP); }
void       kit_scratch_free(void)             { kit_arena_free(&KIT__TEMP); }

/* --------------------------------------------------------------------------
 * Timer
 * -------------------------------------------------------------------------- */

KitTimer kit_timer_start(void) {
    KitTimer sw;
#ifdef _WIN32
    QueryPerformanceCounter(&sw.start);
#else
    clock_gettime(CLOCK_MONOTONIC, &sw.start);
#endif
    return sw;
}

double kit_timer_s(KitTimer sw) {
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

double kit_timer_ms(KitTimer sw) {
    return kit_timer_s(sw) * 1000.0;
}

/* --------------------------------------------------------------------------
 * Vectors
 * -------------------------------------------------------------------------- */

#ifndef KIT_NO_VEC_MATH

KitVec2  kit_vec2_add(KitVec2 a, KitVec2 b)        { return KIT_VEC2(a.x + b.x, a.y + b.y); }
KitVec2  kit_vec2_sub(KitVec2 a, KitVec2 b)        { return KIT_VEC2(a.x - b.x, a.y - b.y); }
KitVec2  kit_vec2_scale(KitVec2 a, float s)     { return KIT_VEC2(a.x * s, a.y * s); }
KitVec2  kit_vec2_mul(KitVec2 a, KitVec2 b)        { return KIT_VEC2(a.x * b.x, a.y * b.y); }
float kit_vec2_len(KitVec2 a)                { return sqrtf(a.x*a.x + a.y*a.y); }
float kit_vec2_dist(KitVec2 a, KitVec2 b)       { return kit_vec2_len(kit_vec2_sub(b, a)); }
float kit_vec2_dot(KitVec2 a, KitVec2 b)        { return a.x*b.x + a.y*b.y; }

KitVec2 kit_vec2_norm(KitVec2 a) {
    float l = kit_vec2_len(a);
    return l == 0.0f ? KIT_VEC2(0, 0) : kit_vec2_scale(a, 1.0f / l);
}

KitVec3  kit_vec3_add(KitVec3 a, KitVec3 b)        { return KIT_VEC3(a.x+b.x, a.y+b.y, a.z+b.z); }
KitVec3  kit_vec3_sub(KitVec3 a, KitVec3 b)        { return KIT_VEC3(a.x-b.x, a.y-b.y, a.z-b.z); }
KitVec3  kit_vec3_scale(KitVec3 a, float s)     { return KIT_VEC3(a.x*s, a.y*s, a.z*s); }
KitVec3  kit_vec3_mul(KitVec3 a, KitVec3 b)        { return KIT_VEC3(a.x*b.x, a.y*b.y, a.z*b.z); }
float kit_vec3_len(KitVec3 a)                { return sqrtf(a.x*a.x + a.y*a.y + a.z*a.z); }
float kit_vec3_dot(KitVec3 a, KitVec3 b)        { return a.x*b.x + a.y*b.y + a.z*b.z; }

KitVec3 kit_vec3_norm(KitVec3 a) {
    float l = kit_vec3_len(a);
    return l == 0.0f ? KIT_VEC3(0, 0, 0) : kit_vec3_scale(a, 1.0f / l);
}

KitVec3 kit_vec3_cross(KitVec3 a, KitVec3 b) {
    return KIT_VEC3(a.y*b.z - a.z*b.y,
              a.z*b.x - a.x*b.z,
              a.x*b.y - a.y*b.x);
}

#endif /* KIT_NO_VEC_MATH */

/* --------------------------------------------------------------------------
 * Command line
 * -------------------------------------------------------------------------- */

char *kit_cli_shift(int *argc, char ***argv) {
    if (*argc <= 0) return NULL;
    char *result = **argv;
    (*argc)--;
    (*argv)++;
    return result;
}

static KitCliOpt *kit__cli_find_short(KitCliOpt *opts, size_t n_opts, char c) {
    for (size_t i = 0; i < n_opts; i++)
        if (opts[i].short_name && opts[i].short_name == c) return &opts[i];
    return NULL;
}

static KitCliOpt *kit__cli_find_long(KitCliOpt *opts, size_t n_opts, const char *key, size_t len) {
    for (size_t i = 0; i < n_opts; i++) {
        const char *name = opts[i].long_name;
        if (name && strlen(name) == len && strncmp(name, key, len) == 0)
            return &opts[i];
    }
    return NULL;
}

/* `origin` is the argument as the user typed it, quoted back in errors. */
static bool kit__cli_assign(KitCliOpt *o, const char *val, const char *origin) {
    if (o->type == KIT_CLI_OPT_STR) {
        *(const char **)o->dst = val;
        return true;
    }

    char *end;
    errno = 0;
    long v = strtol(val, &end, 10);
    if (end == val || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX) {
        KIT_ERROR("kit_cli_parse: '%s' expects an integer, got '%s'", origin, val);
        return false;
    }
    *(int *)o->dst = (int)v;
    return true;
}

/* Walks one "-abc" token. Flags chain; the first option that takes a value
 * consumes whatever follows it, or the next argument when nothing does. */
static bool kit__cli_parse_short_group(KitCliOpt *opts, size_t n_opts, const char *arg,
                                    int *i, int argc, char **argv) {
    for (const char *c = arg + 1; *c; ) {
        char  name  = *c++;
        KitCliOpt  *match = kit__cli_find_short(opts, n_opts, name);
        if (!match) {
            KIT_ERROR("kit_cli_parse: unknown option '-%c' in '%s'", name, arg);
            return false;
        }

        if (match->type == KIT_CLI_OPT_FLAG) {
            if (*c == '=') {
                KIT_ERROR("kit_cli_parse: '-%c' takes no argument", name);
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
                KIT_ERROR("kit_cli_parse: '-%c' requires an argument", name);
                return false;
            }
            val = argv[*i];
        }
        return kit__cli_assign(match, val, arg);
    }
    return true;
}

bool kit_cli_parse(KitCliOpt *opts, size_t n_opts, int *argc, char ***argv) {
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
            if (!kit__cli_parse_short_group(opts, n_opts, arg, &i, *argc, args))
                return false;
            continue;
        }

        const char *key      = arg + 2;
        const char *eq       = strchr(key, '=');
        size_t      key_len  = eq ? (size_t)(eq - key) : strlen(key);

        KitCliOpt *match = kit__cli_find_long(opts, n_opts, key, key_len);
        if (!match) {
            KIT_ERROR("kit_cli_parse: unknown option '%s'", arg);
            return false;
        }

        if (match->type == KIT_CLI_OPT_FLAG) {
            if (eq) {
                KIT_ERROR("kit_cli_parse: '--%.*s' takes no argument",
                    (int)key_len, key);
                return false;
            }
            *(bool *)match->dst = true;
            continue;
        }

        const char *val = eq ? eq + 1 : NULL;
        if (!val) {
            if (++i >= *argc) {
                KIT_ERROR("kit_cli_parse: '%s' requires an argument", arg);
                return false;
            }
            val = args[i];
        }
        if (!kit__cli_assign(match, val, arg)) return false;
    }

    *argc = out;
    return true;
}

#define KIT__CLI_HELP_COLUMN 36

/* The left column is built in a KitBuf rather than a fixed buffer, so a
 * long option name wraps instead of being cut off. */
void kit_cli_usage(FILE *fp, const char *program, const KitCliOpt *opts, size_t n_opts) {
    fprintf(fp, "Usage: %s [options] ...\n\nOptions:\n", program);

    KitBuf left = KIT_ZEROED;
    for (size_t i = 0; i < n_opts; i++) {
        const KitCliOpt *o = &opts[i];
        kit_buf_reset(&left);

        if (o->short_name) kit_buf_printf(&left, "  -%c", o->short_name);
        else               kit_buf_append(&left, "    ");

        if (o->short_name && o->long_name) kit_buf_append(&left, ", ");
        else if (o->long_name)             kit_buf_append(&left, "  ");

        if (o->long_name) {
            if (o->meta) kit_buf_printf(&left, "--%s=<%s>", o->long_name, o->meta);
            else         kit_buf_printf(&left, "--%s", o->long_name);
        } else if (o->meta) {
            kit_buf_printf(&left, " <%s>", o->meta);
        }

        const char *help = o->help ? o->help : "";
        if (left.count > KIT__CLI_HELP_COLUMN)
            fprintf(fp, "%s\n%*s %s\n", kit_buf_cstr(&left), KIT__CLI_HELP_COLUMN, "", help);
        else
            fprintf(fp, "%-*s %s\n", KIT__CLI_HELP_COLUMN, kit_buf_cstr(&left), help);
    }
    kit_buf_free(&left);
}

/* --------------------------------------------------------------------------
 * Buffers
 * -------------------------------------------------------------------------- */

void kit_buf_append_n(KitBuf *sb, const char *str, size_t n) {
    kit_array_reserve(sb, sb->count + n + 1);
    memcpy(sb->items + sb->count, str, n);
    sb->count += n;
}

void kit_buf_append(KitBuf *sb, const char *str) {
    kit_buf_append_n(sb, str, strlen(str));
}

void kit_buf_append_char(KitBuf *sb, char c) {
    kit_buf_append_n(sb, &c, 1);
}

void kit_buf_printf(KitBuf *sb, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (n < 0) return;

    kit_array_reserve(sb, sb->count + (size_t)n + 1);

    va_start(args, fmt);
    vsnprintf(sb->items + sb->count, (size_t)n + 1, fmt, args);
    va_end(args);
    sb->count += (size_t)n;
}

void kit_buf_append_str(KitBuf *sb, KitStr sv) {
    kit_buf_append_n(sb, sv.data, sv.count);
}

char *kit_buf_cstr(KitBuf *sb) {
    kit_array_reserve(sb, sb->count + 1);
    sb->items[sb->count] = '\0';
    return sb->items;
}

char *kit_buf_dup(KitBuf *sb) {
    char *copy = (char *)malloc(sb->count + 1);
    if (!copy) KIT_PANIC("kit_buf_dup: malloc failed");
    memcpy(copy, sb->items, sb->count);
    copy[sb->count] = '\0';
    return copy;
}

void kit_buf_reset(KitBuf *sb) { sb->count = 0; }

void kit_buf_free(KitBuf *sb) {
    free(sb->items);
    sb->items    = NULL;
    sb->count    = 0;
    sb->capacity = 0;
}

/* --------------------------------------------------------------------------
 * Processes
 * -------------------------------------------------------------------------- */

void kit_command_push(KitCommand *c, const char *arg) {
    kit_array_push(c, arg);
}

void kit_command_push_all(KitCommand *c, ...) {
    va_list args;
    va_start(args, c);
    const char *arg;
    while ((arg = va_arg(args, const char *)) != NULL)
        kit_array_push(c, arg);
    va_end(args);
}

/* Echoes the command about to run. Honours the log level and the colour
 * policy, so a quiet program stays quiet and a redirected build log stays
 * free of escape sequences. */
static void kit__cmd_log(KitCommand *c) {
    if (KIT_LOG_INFO < KIT__MIN_LEVEL) return;

    FILE *out   = KIT__OUTPUT ? KIT__OUTPUT : stderr;
    bool  color = kit__use_color(out);

    fprintf(out, "%s[CMD]%s", color ? "\x1b[35m" : "", color ? "\x1b[0m" : "");
    for (size_t i = 0; i < c->count; ++i) {
        bool needs_quote = (strchr(c->items[i], ' ') != NULL);
        fprintf(out, needs_quote ? " '%s'" : " %s", c->items[i]);
    }
    fputc('\n', out);
    fflush(out);
}

#ifdef _WIN32

static char *kit__cmd_to_cmdline(KitCommand *c) {
    KitBuf sb = KIT_ZEROED;
    for (size_t i = 0; i < c->count; ++i) {
        bool has_space = (strchr(c->items[i], ' ') != NULL);
        if (has_space) kit_buf_append(&sb, "\"");
        kit_buf_append(&sb, c->items[i]);
        if (has_space) kit_buf_append(&sb, "\"");
        if (i + 1 < c->count) kit_buf_append(&sb, " ");
    }
    return kit_buf_cstr(&sb);
}

KitProcess kit_command_spawn(KitCommand *c) {
    kit__cmd_log(c);
    char *cmdline = kit__cmd_to_cmdline(c);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0,
                        NULL, NULL, &si, &pi)) {
        KIT_ERROR("kit_command_spawn: CreateProcess failed (err=%lu)", GetLastError());
        free(cmdline);
        return KIT_PROCESS_INVALID;
    }
    CloseHandle(pi.hThread);
    free(cmdline);
    return pi.hProcess;
}

bool kit_process_wait(KitProcess p) {
    if (p == KIT_PROCESS_INVALID) return false;
    WaitForSingleObject(p, INFINITE);
    DWORD exit_code;
    GetExitCodeProcess(p, &exit_code);
    CloseHandle(p);
    if (exit_code != 0) {
        KIT_ERROR("kit_process_wait: process exited with code %lu", exit_code);
        return false;
    }
    return true;
}

/* Windows has no poll for anonymous pipes, so the two are polled with
 * PeekNamedPipe. Blocking on a ReadFile from one while the child fills the
 * other is the deadlock this avoids; the millisecond of sleep is what keeps
 * the loop from spinning a core while the child thinks. */
static bool kit__cmd_capture(KitCommand *c, KitBuf *out, KitBuf *err) {
    bool merged   = (out != NULL && out == err);
    bool need_out = (out != NULL);
    bool need_err = (err != NULL && !merged);

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE out_r = NULL, out_w = NULL, err_r = NULL, err_w = NULL;

    if (need_out) {
        if (!CreatePipe(&out_r, &out_w, &sa, 0)) {
            KIT_ERROR("kit_command_capture: CreatePipe failed (err=%lu)", GetLastError());
            return false;
        }
        SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    }
    if (need_err) {
        if (!CreatePipe(&err_r, &err_w, &sa, 0)) {
            KIT_ERROR("kit_command_capture: CreatePipe failed (err=%lu)", GetLastError());
            if (out_r) { CloseHandle(out_r); CloseHandle(out_w); }
            return false;
        }
        SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);
    }

    char *cmdline = kit__cmd_to_cmdline(c);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    /* With STARTF_USESTDHANDLES all three must be valid, inherited ones
     * included, or the child starts with no console at all. */
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = need_out ? out_w : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = merged   ? out_w
                  : need_err ? err_w : GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags    = STARTF_USESTDHANDLES;
    ZeroMemory(&pi, sizeof(pi));

    bool started = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0,
                                  NULL, NULL, &si, &pi);
    free(cmdline);
    if (out_w) CloseHandle(out_w);
    if (err_w) CloseHandle(err_w);

    if (!started) {
        KIT_ERROR("kit_command_capture: CreateProcess failed (err=%lu)", GetLastError());
        if (out_r) CloseHandle(out_r);
        if (err_r) CloseHandle(err_r);
        return false;
    }
    CloseHandle(pi.hThread);

    HANDLE         handles[2];
    KitBuf *sinks[2];
    int            n = 0;
    if (need_out) { handles[n] = out_r; sinks[n] = out; n++; }
    if (need_err) { handles[n] = err_r; sinks[n] = err; n++; }

    char buf[4096];
    int  still_open = n;
    while (still_open > 0) {
        bool progress = false;

        for (int i = 0; i < n; i++) {
            if (handles[i] == NULL) continue;

            DWORD available = 0;
            if (!PeekNamedPipe(handles[i], NULL, 0, NULL, &available, NULL)) {
                CloseHandle(handles[i]);   /* the child closed its end */
                handles[i] = NULL;
                still_open--;
                continue;
            }
            if (available == 0) continue;

            DWORD got = 0;
            if (available > sizeof(buf)) available = sizeof(buf);
            if (!ReadFile(handles[i], buf, available, &got, NULL) || got == 0) {
                CloseHandle(handles[i]);
                handles[i] = NULL;
                still_open--;
                continue;
            }
            kit_buf_append_n(sinks[i], buf, got);
            progress = true;
        }

        if (!progress && still_open > 0) Sleep(1);
    }

    return kit_process_wait(pi.hProcess);
}

#else /* POSIX */

KitProcess kit_command_spawn(KitCommand *c) {
    kit__cmd_log(c);

    /* execvp needs a NULL sentinel, appended for the duration of the call. */
    kit_array_push(c, NULL);
    pid_t pid = fork();
    c->count--;   /* remove the sentinel regardless of outcome */

    if (pid < 0) {
        KIT_ERROR("kit_command_spawn: fork failed: %s", strerror(errno));
        return KIT_PROCESS_INVALID;
    }
    if (pid == 0) {
        execvp(c->items[0], (char *const *)(void *)c->items);
        /* fprintf on a shared FILE* is unsafe after fork - use dprintf */
        if (KIT_LOG_ERROR >= KIT__MIN_LEVEL)
            dprintf(STDERR_FILENO, "kit_command_spawn: execvp '%s' failed: %s\n",
                    c->items[0], strerror(errno));
        _exit(127);
    }
    return pid;
}

bool kit_process_wait(KitProcess p) {
    if (p == KIT_PROCESS_INVALID) return false;
    int status;
    if (waitpid(p, &status, 0) < 0) {
        KIT_ERROR("kit_process_wait: waitpid failed: %s", strerror(errno));
        return false;
    }
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code != 0) {
            KIT_ERROR("kit_process_wait: process exited with code %d", code);
            return false;
        }
        return true;
    }
    if (WIFSIGNALED(status)) {
        KIT_ERROR("kit_process_wait: process killed by signal %d", WTERMSIG(status));
        return false;
    }
    KIT_ERROR("kit_process_wait: process ended unexpectedly");
    return false;
}

/* One pipe carries both streams when they are merged, so the child can never
 * block on a full pipe that the parent is not reading. */
/* Both pipes are drained by one poll loop. Reading them in sequence would
 * deadlock the moment the child fills the one nobody is reading, and a
 * compiler emitting a wall of warnings does exactly that. */
static bool kit__cmd_capture(KitCommand *c, KitBuf *out, KitBuf *err) {
    bool merged   = (out != NULL && out == err);
    bool need_out = (out != NULL);
    bool need_err = (err != NULL && !merged);

    int op[2] = { -1, -1 };
    int ep[2] = { -1, -1 };

    if (need_out && pipe(op) < 0) {
        KIT_ERROR("kit_command_capture: pipe failed: %s", strerror(errno));
        return false;
    }
    if (need_err && pipe(ep) < 0) {
        KIT_ERROR("kit_command_capture: pipe failed: %s", strerror(errno));
        if (op[0] >= 0) { close(op[0]); close(op[1]); }
        return false;
    }

    kit_array_push(c, NULL);
    pid_t pid = fork();
    c->count--;

    if (pid < 0) {
        KIT_ERROR("kit_command_capture: fork failed: %s", strerror(errno));
        if (op[0] >= 0) { close(op[0]); close(op[1]); }
        if (ep[0] >= 0) { close(ep[0]); close(ep[1]); }
        return false;
    }

    if (pid == 0) {
        if (op[0] >= 0) close(op[0]);
        if (ep[0] >= 0) close(ep[0]);

        if (need_out) {
            dup2(op[1], STDOUT_FILENO);
            if (merged) dup2(op[1], STDERR_FILENO);
        }
        if (need_err) dup2(ep[1], STDERR_FILENO);

        if (op[1] >= 0) close(op[1]);
        if (ep[1] >= 0) close(ep[1]);

        execvp(c->items[0], (char *const *)(void *)c->items);
        if (KIT_LOG_ERROR >= KIT__MIN_LEVEL)
            dprintf(STDERR_FILENO, "kit_command_capture: execvp '%s' failed: %s\n",
                    c->items[0], strerror(errno));
        _exit(127);
    }

    if (op[1] >= 0) close(op[1]);
    if (ep[1] >= 0) close(ep[1]);

    struct pollfd  fds[2];
    KitBuf *sinks[2];
    nfds_t         n = 0;
    if (need_out) { fds[n].fd = op[0]; fds[n].events = POLLIN; sinks[n] = out; n++; }
    if (need_err) { fds[n].fd = ep[0]; fds[n].events = POLLIN; sinks[n] = err; n++; }

    char   buf[4096];
    nfds_t still_open = n;
    while (still_open > 0) {
        if (poll(fds, n, -1) < 0) {
            if (errno == EINTR) continue;
            KIT_ERROR("kit_command_capture: poll failed: %s", strerror(errno));
            break;
        }
        for (nfds_t i = 0; i < n; i++) {
            if (fds[i].fd < 0 || fds[i].revents == 0) continue;

            ssize_t nread = read(fds[i].fd, buf, sizeof(buf));
            if (nread > 0) { kit_buf_append_n(sinks[i], buf, (size_t)nread); continue; }
            if (nread < 0 && errno == EINTR) continue;

            close(fds[i].fd);      /* end of stream, or an error we cannot use */
            fds[i].fd = -1;
            still_open--;
        }
    }
    for (nfds_t i = 0; i < n; i++) if (fds[i].fd >= 0) close(fds[i].fd);

    return kit_process_wait(pid);
}

#endif /* _WIN32 / POSIX */

bool kit_command_capture(KitCommand *c, KitBuf *sb) {
    return kit__cmd_capture(c, sb, NULL);
}

bool kit_command_capture_merged(KitCommand *c, KitBuf *sb) {
    return kit__cmd_capture(c, sb, sb);
}

bool kit_command_capture_split(KitCommand *c, KitBuf *out, KitBuf *err) {
    return kit__cmd_capture(c, out, err);
}

bool kit_command_run(KitCommand *c) {
    KitProcess p = kit_command_spawn(c);
    return kit_process_wait(p);
}

bool kit_command_run_args(const char *first, ...) {
    KitCommand cmd = KIT_ZEROED;
    kit_command_push(&cmd, first);

    va_list args;
    va_start(args, first);
    const char *arg;
    while ((arg = va_arg(args, const char *)) != NULL)
        kit_command_push(&cmd, arg);
    va_end(args);

    bool ok = kit_command_run(&cmd);
    kit_command_free(&cmd);
    return ok;
}

void kit_command_reset(KitCommand *c) {
    c->count = 0;
}

void kit_command_free(KitCommand *c) {
    kit_array_free(c);
}

/* --------------------------------------------------------------------------
 * Hashing
 * -------------------------------------------------------------------------- */

uint32_t kit_hash_bytes(const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    uint32_t h = 5381;
    for (size_t i = 0; i < len; ++i)
        h = ((h << 5) + h) ^ p[i];
    return h;
}

uint32_t kit_hash_str(const char *s) {
    return kit_hash_bytes(s, strlen(s));
}

uint32_t kit_hash_mix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

/* --------------------------------------------------------------------------
 * Map
 * -------------------------------------------------------------------------- */

/* Unique address used to mark deleted (tombstone) slots.
 * Never equal to any real string pointer. */
static char KIT__MAP_TOMB = 0;
#define KIT__MAP_TOMBSTONE ((const char *)&KIT__MAP_TOMB)

bool kit_map_entry_live(const KitMapEntry *e) {
    return e->key != NULL && e->key != KIT__MAP_TOMBSTONE;
}

/* Returns the index of the slot for key, or hm->capacity if not found.
 * When for_write is true, returns the first usable slot on a miss. */
static size_t kit__map_find_slot(const KitMap *hm, const char *key, bool for_write) {
    size_t mask        = hm->capacity - 1;
    size_t idx         = (size_t)kit_hash_mix32(kit_hash_str(key)) & mask;
    size_t tombstone   = hm->capacity;

    for (size_t i = 0; i < hm->capacity; ++i) {
        size_t probe    = (idx + i) & mask;
        const char *k   = hm->entries[probe].key;

        if (k == NULL) {
            if (for_write) return (tombstone < hm->capacity) ? tombstone : probe;
            return hm->capacity;
        }
        if (k == KIT__MAP_TOMBSTONE) {
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
static void kit__map_rehash(KitMap *hm) {
    size_t new_cap = 16;
    if (hm->capacity != 0)
        new_cap = (hm->count * 10 >= hm->capacity * 7) ? hm->capacity * 2
                                                       : hm->capacity;

    KitMapEntry *new_entries = (KitMapEntry *)calloc(new_cap, sizeof(KitMapEntry));
    if (!new_entries) KIT_PANIC("kit__map_rehash: calloc of %zu entries failed", new_cap);

    /* Positional: entries, count, used, capacity. */
    KitMap tmp = { new_entries, 0, 0, new_cap };

    for (size_t i = 0; i < hm->capacity; ++i) {
        if (!kit_map_entry_live(&hm->entries[i])) continue;
        size_t slot = kit__map_find_slot(&tmp, hm->entries[i].key, true);
        tmp.entries[slot] = hm->entries[i];
        tmp.count++;
    }
    tmp.used = tmp.count;

    free(hm->entries);
    *hm = tmp;
}

bool kit_map_set(KitMap *hm, const char *key, void *value) {
    /* Counting tombstones here is what keeps probe sequences short: they occupy
     * a slot just like a live entry as far as linear probing is concerned. */
    if ((hm->used + 1) * 10 >= hm->capacity * 7) kit__map_rehash(hm);

    size_t slot  = kit__map_find_slot(hm, key, true);
    bool   is_new = !kit_map_entry_live(&hm->entries[slot]);
    /* Reusing a tombstone adds a live entry without occupying a new slot. */
    if (is_new && hm->entries[slot].key == NULL) hm->used++;
    hm->entries[slot].key   = key;
    hm->entries[slot].value = value;
    if (is_new) hm->count++;
    return is_new;
}

void *kit_map_get(const KitMap *hm, const char *key) {
    if (hm->capacity == 0) return NULL;
    size_t slot = kit__map_find_slot(hm, key, false);
    if (slot == hm->capacity) return NULL;
    return hm->entries[slot].value;
}

bool kit_map_has(const KitMap *hm, const char *key) {
    if (hm->capacity == 0) return false;
    return kit__map_find_slot(hm, key, false) != hm->capacity;
}

bool kit_map_delete(KitMap *hm, const char *key) {
    if (hm->capacity == 0) return false;
    size_t slot = kit__map_find_slot(hm, key, false);
    if (slot == hm->capacity) return false;
    hm->entries[slot].key   = KIT__MAP_TOMBSTONE;
    hm->entries[slot].value = NULL;
    hm->count--;   /* `used` stays: the slot is still occupied by the tombstone */
    return true;
}

void kit_map_reset(KitMap *hm) {
    if (hm->entries) memset(hm->entries, 0, hm->capacity * sizeof(KitMapEntry));
    hm->count = 0;
    hm->used  = 0;
}

void kit_map_free(KitMap *hm) {
    free(hm->entries);
    hm->entries  = NULL;
    hm->count    = 0;
    hm->used     = 0;
    hm->capacity = 0;
}

/* --------------------------------------------------------------------------
 * Paths
 * -------------------------------------------------------------------------- */

static bool kit__path_is_sep(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

const char *kit_path_basename(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; ++p)
        if (kit__path_is_sep(*p) && *(p + 1)) base = p + 1;
    return base;
}

const char *kit_path_ext(const char *path) {
    const char *base = kit_path_basename(path);
    const char *dot  = strrchr(base, '.');
    return dot ? dot : path + strlen(path);
}

/* Every write is bounded by bufsz; a bufsz of 0 leaves buf untouched. The
 * previous "bufsz - 1" arithmetic wrapped around on an empty buffer. */
char *kit_path_dirname(const char *path, char *buf, size_t bufsz) {
    if (bufsz == 0) return buf;

    const char *base = kit_path_basename(path);
    size_t      len  = (size_t)(base - path);

    /* Drop the trailing separators, but keep a lone root "/". */
    while (len > 1 && kit__path_is_sep(path[len - 1])) len--;

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

char *kit_path_join(char *buf, size_t bufsz, const char *a, const char *b) {
    if (bufsz == 0) return buf;

    size_t a_len   = strlen(a);
    size_t written = a_len < bufsz - 1 ? a_len : bufsz - 1;
    memcpy(buf, a, written);

    while (kit__path_is_sep(*b)) b++;

    if (*b && written > 0 && written < bufsz - 1 && !kit__path_is_sep(buf[written - 1]))
        buf[written++] = KIT_PATH_SEP;

    size_t b_len = strlen(b);
    size_t avail = bufsz - 1 - written;
    size_t n     = b_len < avail ? b_len : avail;
    memcpy(buf + written, b, n);
    buf[written + n] = '\0';
    return buf;
}

bool kit_path_is_absolute(const char *path) {
#ifdef _WIN32
    return (path[0] && path[1] == ':') || kit__path_is_sep(path[0]);
#else
    return path[0] == '/';
#endif
}

#endif /* KIT_IMPLEMENTATION && !KIT__IMPLEMENTATION_DONE */

/*
 * ============================================================================
 * LICENSE
 * ============================================================================
 *
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this software, either in source code form or as a compiled binary, for any
 * purpose, commercial or non-commercial, and by any means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors of
 * this software dedicate any and all copyright interest in the software to
 * the public domain. We make this dedication for the benefit of the public at
 * large and to the detriment of our heirs and successors. We intend this
 * dedication to be an overt act of relinquishment in perpetuity of all
 * present and future rights to this software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <https://unlicense.org>
 */
