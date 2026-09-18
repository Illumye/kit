# kit.h

A single-header toolkit for C11 and C++17. Drop the file into a project, define
the implementation macro in exactly one translation unit, and use it.

```c
#define KIT_IMPLEMENTATION
#include "kit.h"
```

Every other file just includes it. There is nothing to build and nothing to
link, apart from `-lm` when the vector maths is in use.

## Naming

Everything the header exposes lives in one namespace, so it sits next to any
other library, or any project's own `read_file` and `LOG`, without a clash.

| Kind | Form | Example |
|---|---|---|
| Functions and function-like macros | `kit_module_action` | `kit_str_trim` |
| Types | `KitName` | `KitStr` |
| Constants, enumerators, other macros | `KIT_NAME` | `KIT_LOG_ERROR` |
| Internals | `kit__` or `KIT__` | `kit__log` |

`make check-namespace` enforces it: it fails the build if a macro, a symbol, a
type or an enumerator appears without the prefix.

Coming from `utils.h`? [MIGRATION.md](MIGRATION.md) maps every old name to its
new one.

## What it covers

| Module | Prefix | What it gives you |
|---|---|---|
| Logging | `kit_log`, `KIT_LOG` | Levels, configurable record fields, colour only on a terminal |
| Assertions | `KIT_ASSERT` | Contracts that abort and print both sides, typed |
| Errors | `kit_error`, `KitError` | Failures with a category, a native code and a context chain |
| Filesystem | `kit_fs` | Streamed reads, recursive mkdir, sorted listing, staleness checks |
| Arrays | `kit_array` | Growable arrays over any `{items, count, capacity}` struct |
| Strings | `kit_str` | Non-owning slices: search, splitting, strict numeric parsing |
| Arena | `kit_arena`, `kit_scratch` | Bump allocation over chained regions, and per-thread scratch |
| Timer | `kit_timer` | Monotonic timing |
| Vectors | `kit_vec2`, `kit_vec3` | 2D and 3D vector maths |
| Command line | `kit_cli` | Option parsing with grouped short flags, generated usage |
| Buffers | `kit_buf` | Growable text buffer with `printf` formatting |
| Processes | `kit_command`, `kit_process` | Run a child, wait for it, or capture either stream |
| Map | `kit_map` | String keys, open addressing |
| Paths | `kit_path` | `basename`, `dirname`, `join`, all bounded |
| Arithmetic | `kit_num` | Addition, subtraction and multiplication that refuse to wrap |
| Formatting | `kit_fmt` | Sizes and durations a person reads at a glance |
| Hex dump | `kit_hex_dump` | Bytes and their text, laid out like `hexdump -C` |

Each module is documented where it is declared. Read the header.

## Errors

A failure should say what went wrong, where, and what the caller can do about
it. The caller owns a `KitError`, usually on its stack, with the message inside
it, so recording one never allocates and never fails.

```c
KitError err = KIT_ZEROED;
if (!load_config("config.ini", &cfg, &err)) {
    kit_error_context(&err, "starting the server");
    KIT_ERROR("%s (%s)", err.message, kit_error_code_name(err.code));
}
```

```
starting the server: reading config.ini: line 12: expected an integer (invalid)
```

Every kit function that can fail takes a `KitError *` last. Pass one to receive
the failure, or `NULL` to have it logged instead: a kit function never both
logs a failure and reports it. The exceptions are the functions that answer a
question rather than fail, `kit_fs_is_file` and the `kit_str_to_*` parsers
among them: a no is an answer, not a failure.

The code is a category that tells the caller what to do, `not_found`,
`permission` or `busy` among others, and the platform's own value stays in
`err.native`. The filesystem normalises the codes across platforms: removing a
directory with `kit_fs_remove` is `wrong_kind` on Linux, macOS and Windows
alike, although each system reports it differently. A program that is not
installed is `not_found` with the reason the system gave, rather than a child
that mysteriously exited with code 127. Context is added outermost first as the error travels up. When
the chain outgrows the buffer, the middle is elided and both the outermost
context and the root cause are kept.

## Contracts, and arithmetic that refuses to wrap

A `KitError` is for a failure the caller can do something about. An assertion
is for something the code believes about itself, that no input can make false:
a violated one is a bug, so it aborts, and it prints both sides with the format
their type calls for.

```c
KIT_ASSERT_CMP(used, <=, capacity);
```

```
[PANIC] cache.c:88: assertion failed: used <= capacity
  left:  5000
  right: 4096
Aborting...
```

Any operator goes in the middle, so one macro covers the six comparisons.
`KIT_ASSERT` takes a plain condition, `KIT_ASSERT_MSG` adds the state behind
it, and `KIT_ASSERT_STR_EQ` compares strings, because `==` on two pointers
compares addresses. None of them is compiled out by `NDEBUG`: a release build
that skips its contracts is one whose bugs only appear where nobody is looking.

Arithmetic on values that came from outside the program has the same problem
one level down. `kit_num_add`, `kit_num_sub` and `kit_num_mul` report the
overflow instead of wrapping, and leave the destination alone when they do, so
a running total keeps the last value that was true.

```c
if (!kit_num_mul(lines, line_bytes, &planned))
    return kit_error_set(err, KIT_ERR_RANGE, "%d lines is more than this can count", lines);
```

The type of the operation is the type of the destination: `int`, `long`,
`long long` and their unsigned counterparts, which is also `size_t`,
`ptrdiff_t` and the fixed-width types, whichever of the six each one is here.

## A taste of it

```c
#define KIT_IMPLEMENTATION
#include "kit.h"

int main(int argc, char **argv) {
    const char *prog = kit_cli_shift(&argc, &argv);

    bool verbose = false;
    const char *out = "a.out";
    KitCliOpt opts[] = {
        KIT_CLI_FLAG('v', "verbose", "Say more",    &verbose),
        KIT_CLI_STR ('o', "output",  "FILE", "Where to write", &out),
    };
    if (!kit_cli_parse_arr(opts, &argc, &argv, NULL)) {
        kit_cli_usage_arr(stderr, prog, opts);
        return 1;
    }

    if (kit_fs_stale1(out, "main.c", NULL) != 0) {
        KitCommand cmd = KIT_ZEROED;
        kit_command_push_all(&cmd, "cc", "-o", out, "main.c", NULL);
        if (!kit_command_run(&cmd, NULL)) return 1;
        kit_command_free(&cmd);
    }

    KIT_INFO("%s is up to date", out);
    return 0;
}
```

## Building the tests

```sh
make test            # native build
make test-asan       # AddressSanitizer and UndefinedBehaviorSanitizer, leaks on
make check-c11       # strict -std=c11 -Werror, with and without vector maths
make check-namespace # nothing unprefixed leaks, and common names still coexist
make check-cxx       # the same header from C++17, implementation included
make check-examples  # build and drive the example programs
make test-all        # all of the above
make check-windows   # cross-compile with mingw-w64 and run under wine
make fuzz            # libFuzzer over the parsers, FUZZ_SECS=600 to go deeper
```

The suite is 125 tests over nine files, and the header compiles warning-free
under gcc and clang with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow
-Wcast-qual -Wstrict-prototypes -Wwrite-strings`.

## Examples

Eight programs under `examples/`, each one a small tool rather than a tour of
the API. Between them they use every public name in the header, which
`make check-examples` enforces: add a function without showing it anywhere and
the build stops.

| Example | What it does | What it leans on |
|---|---|---|
| `build.c` | Compiles `examples/demo` like a small make | filesystem, staleness, processes, scratch |
| `config.c` | Reads an INI file into a map | strings, map, arena, errors with context |
| `journal.c` | Rotates a log file when it grows | filesystem writes, logger configuration, error codes |
| `orbit.c` | Steps a few bodies around a centre | vectors, scalar maths, arena, timer |
| `runner.c` | Reports on other programs and runs them | commands, processes, captured output |
| `tree.c` | Walks a directory and measures it | filesystem, paths, scratch, timer |
| `wordfreq.c` | Counts words in a text | string views, map, arrays, buffers, hashing |
| `peek.c` | Says what a file is and shows its bytes | option parsing, file kinds, readable sizes, hex dump |

```sh
make examples
./examples/config examples/demo/app.conf --list
./examples/peek -n 32 examples/demo/app.conf
./examples/wordfreq --top 5 examples/demo/prose.txt
./examples/tree --depth 2 examples
./examples/runner --check cc make git
./examples/orbit --bodies 5 --steps 500
./examples/journal --dir build/journal --limit 2048 --lines 200
```

The one that reads best as documentation is `config.c`: a parse failure there
carries the line number, the file name and what was expected, which is the
whole point of the error module.

## Configuration

Define these before including the header to change its defaults.

| Macro | Effect |
|---|---|
| `KIT_NO_VEC_MATH` | Drops the vector maths and the dependency on `-lm` |
| `KIT_ARRAY_INIT_CAP` | Initial capacity of a dynamic array, default 256 |
| `KIT_ARENA_REGION_SIZE` | Size of a new arena region, default 64 KiB |
| `KIT_READ_CHUNK` | Read buffer size, default 64 KiB |
| `KIT_NO_THREAD_LOCAL` | One shared scratch arena instead of one per thread |
| `KIT_NO_OVERFLOW_BUILTINS` | Checked arithmetic without the compiler's builtins, which is how the suite exercises the portable path |

On POSIX the header requests the C library's default feature set, which a
strict `-std=c11` hides, for `clock_gettime`, `dprintf` and `isatty`. Include it
before any other system header when building that way. It never narrows what
your own code sees, and leaves any feature macro you defined yourself alone.

## Status

Known limitations and queued work are recorded in [TODO.md](TODO.md). The
short version: the platforms above are what has actually been run.

## Requirements

C11, or C++17.

| Verified here | How |
|---|---|
| Linux x86-64 | gcc and clang, the sanitizers, and C++17 with g++ and clang++ |
| Linux arm64 | the suite under emulation |
| Linux s390x | the suite under emulation, which is where big-endian is covered |
| Windows | cross-compiled with mingw-w64 and run under wine |

Continuous integration additionally builds on macOS, and on Windows with MSVC
and the UCRT runtime, neither of which can be reached from a Linux machine.

## Using it from C++

The header compiles as C++17 with the implementation included, so a C++ project
needs no separate C translation unit. Two spellings differ between the
languages and the header provides one that works in both:

```cpp
KitCommand cmd = KIT_ZEROED;     // {0} in C warns in C++, {} in C++ is not C
KitVec2    v   = KIT_VEC2(3, 4); // a compound literal in C, brace init in C++
```

The one thing ISO C++ does not allow is the flexible array member the arena
uses for its regions. Every compiler accepts it; only `-pedantic` complains.

## License

Public domain, under [the Unlicense](LICENSE). Copy the header into your
project, change it, ship it, sell it. No attribution required.

The full licence text is repeated at the end of `kit.h`, because a header
meant to be copied on its own has to carry its own terms.
