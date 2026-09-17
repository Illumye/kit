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

The code is a category that tells the caller what to do, `not_found`,
`permission` or `busy` among others, and the platform's own value stays in
`err.native`. Context is added outermost first as the error travels up. When
the chain outgrows the buffer, the middle is elided and both the outermost
context and the root cause are kept.

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
    if (!kit_cli_parse_arr(opts, &argc, &argv)) {
        kit_cli_usage_arr(stderr, prog, opts);
        return 1;
    }

    if (kit_fs_stale1(out, "main.c") != 0) {
        KitCommand cmd = KIT_ZEROED;
        kit_command_push_all(&cmd, "cc", "-o", out, "main.c", NULL);
        if (!kit_command_run(&cmd)) return 1;
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

The suite is 102 tests over seven files, and the header compiles warning-free
under gcc and clang with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow
-Wcast-qual -Wstrict-prototypes -Wwrite-strings`.

## Examples

`examples/build.c` is a build tool in one file. It compiles `examples/demo`
into an executable, skips any step whose output is newer than its inputs, and
rebuilds when a header changes. It uses the option parser, the filesystem
layer, the scratch allocator, the command runner and the logger together, so
it doubles as the integration test that `make check-examples` runs.

```sh
make examples
./examples/build -r      # build and run
./examples/build         # nothing to do
./examples/build --clean
```

`examples/cli.c` is a smaller one, showing only the option parsing.

## Fuzzing

`tests/fuzz` holds four libFuzzer targets, over the string parsers, the
path helpers, the option parser and the hash map. Two of them are differential:
the numeric parsers are compared against `strtoll` and `strtoull`, and the hash
map against a naive array applying the same operations. The path target
allocates every destination buffer at exactly the requested size, so a
one-byte overrun is a fault rather than silence.

```sh
make fuzz                 # 15 seconds per target
make FUZZ_SECS=600 fuzz   # ten minutes per target
```

The harness has been mutation tested: injecting a leading-space bug into
`kit_str_to_i64` and an off-by-one into `kit_path_join` is caught within
seconds, by the differential assertion and by AddressSanitizer respectively.

No corpus is committed, on purpose. These targets saturate their reachable
code almost immediately: a 180-second run reaches exactly the coverage a
20-second run does, 113 million executions finding nothing a first few million
did not. A stored corpus would be a couple of megabytes of blobs buying
nothing, so each run rediscovers what it needs.

## Configuration

Define these before including the header to change its defaults.

| Macro | Effect |
|---|---|
| `KIT_NO_VEC_MATH` | Drops the vector maths and the dependency on `-lm` |
| `KIT_ARRAY_INIT_CAP` | Initial capacity of a dynamic array, default 256 |
| `KIT_ARENA_REGION_SIZE` | Size of a new arena region, default 64 KiB |
| `KIT_READ_CHUNK` | Read buffer size, default 64 KiB |
| `KIT_NO_THREAD_LOCAL` | One shared scratch arena instead of one per thread |

On POSIX the header requests the C library's default feature set, which a
strict `-std=c11` hides, for `clock_gettime`, `dprintf` and `isatty`. Include it
before any other system header when building that way. It never narrows what
your own code sees, and leaves any feature macro you defined yourself alone.

## Status

Known limitations and queued work are recorded in [TODO.md](TODO.md). The
short version: Windows is covered by cross-compiling and running under wine
rather than on a real Windows machine.

## Requirements

C11, or C++17. Tested with gcc and clang on Linux, g++ and clang++ for the
C++ path, and mingw-w64 under wine for Windows.

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
