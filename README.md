# utils.h

A single-header C11 utility library. Drop the file into a project, define the
implementation macro in exactly one translation unit, and use it.

```c
#define UTILS_IMPLEMENTATION
#include "utils.h"
```

Every other file just includes it. There is nothing to build and nothing to
link, apart from `-lm` when the vector maths is in use.

## What it covers

| Section | What it gives you |
|---|---|
| Logging | Levels, configurable record fields, colour only on a terminal |
| Files | Streamed reads, `mkdir_p`, `read_dir`, `copy_file`, `needs_rebuild` |
| Dynamic arrays | `da_append`, `da_foreach` and friends over any `{items, count, capacity}` struct |
| String views | Non-owning slices: search, splitting, strict numeric parsing |
| Arena | Bump allocation over a chain of regions, plus a process-wide scratch arena |
| Stopwatch | Monotonic timing |
| Vector maths | `Vec2` and `Vec3` |
| CLI | Option parsing with grouped short flags, and generated usage text |
| String builder | Growable text buffer with `printf` formatting |
| Commands | Run a child process, synchronously, asynchronously, or capturing its output |
| Hash map | String keys, open addressing |
| Paths | `basename`, `dirname`, `join`, all bounded |

Each section is documented where it is declared. Read the header.

## A taste of it

```c
#define UTILS_IMPLEMENTATION
#include "utils.h"

int main(int argc, char **argv) {
    const char *prog = args_shift(&argc, &argv);

    bool verbose = false;
    const char *out = "a.out";
    Opt opts[] = {
        OPT_FLAG('v', "verbose", "Say more",    &verbose),
        OPT_STR ('o', "output",  "FILE", "Where to write", &out),
    };
    if (!opts_parse_arr(opts, &argc, &argv)) {
        opts_usage_arr(stderr, prog, opts);
        return 1;
    }

    if (needs_rebuild1(out, "main.c") != 0) {
        Cmd cmd = {0};
        cmd_extend(&cmd, "cc", "-o", out, "main.c", NULL);
        if (!cmd_run(&cmd)) return 1;
        cmd_free(&cmd);
    }

    LOG(LOG_INFO, "%s is up to date", out);
    return 0;
}
```

## Building the tests

```sh
make test        # native build
make test-asan   # AddressSanitizer and UndefinedBehaviorSanitizer, leaks on
make check-c11   # strict -std=c11 -Werror, with and without vector maths
make test-all    # all of the above
make examples    # the example programs
```

The suite is 73 tests over five files, and the header compiles warning-free
under gcc and clang with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow
-Wcast-qual -Wstrict-prototypes -Wwrite-strings`.

## Configuration

Define these before including the header to change its defaults.

| Macro | Effect |
|---|---|
| `UTILS_NO_VEC_MATH` | Drops the vector maths and the dependency on `-lm` |
| `DA_INIT_CAP` | Initial capacity of a dynamic array, default 256 |
| `ARENA_REGION_SIZE` | Size of a new arena region, default 64 KiB |
| `UTILS_READ_CHUNK` | Read buffer size, default 64 KiB |

On POSIX the header requests `_POSIX_C_SOURCE` for `clock_gettime`, `dprintf`
and `isatty`, so include it before any other system header when building with
a strict `-std=c11` rather than `-std=gnu11`.

## Status

Known limitations and queued work are recorded in [TODO.md](TODO.md). The
short version: the Windows branches are written but have only ever run on
Linux, and the header does not compile as C++.

## Requirements

C11. Tested with gcc and clang on Linux.
