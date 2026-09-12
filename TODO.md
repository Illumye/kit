# Known limitations and backlog

Findings from the audit of 2026-09-12 that were deliberately left alone, and
the work still queued. Everything listed here is a conscious choice, not an
oversight: the defects found during that audit are fixed and covered by tests.

## Accepted by design

**Single header, no modules.** `utils.h` is past 1700 lines and will keep
growing. Splitting it would destroy the one property that makes it useful,
namely that it drops into any project as a single file.

**Hash map keys are not copied.** `hm_set` stores the pointer it is given, so
the caller owns the key and must keep it alive for as long as the entry. This
is what keeps the map allocation-free on insert. A copying variant would need
an owner for the copies, which means an arena parameter or a free hook.

**`read_file` loads the whole file into memory.** Fine for configuration,
source files and manifests; wrong for anything large. A streaming reader would
be a different API, not a change to this one.

**`PANIC` on allocation failure.** The dynamic arrays, the arena and the string
builder abort rather than propagate an error. For the command-line tools this
library targets, an out-of-memory condition is not recoverable and threading
the error through every macro would poison the ergonomics.

## Known limitations

**`file_size` returns `long`.** That caps it at 2 GB on Windows and on 32-bit
platforms. `int64_t` is the correct type, but changing it breaks a published
signature, so it waits for a deliberate API break.

**No `extern "C"` guard.** The header cannot be included from C++. Adding the
guard is easy; making the body compile as C++ is not, because it relies on
implicit `void *` conversions throughout.

**The arena aligns to `sizeof(void *)`.** Over-aligned types, SIMD vectors and
`long double` among them, are not served correctly. An `arena_alloc_aligned`
would fix this.

**The arena is one fixed block.** It aborts when full instead of chaining a new
region. Queued as part of the comfort work below.

**`opts_parse` rejects grouped short flags.** `-vn` fails where `-v -n` works,
although grouping is the universal POSIX convention. Queued below.

**`da_contains` needs GNU statement expressions.** It is compiled out on MSVC.
Every other macro in the library is portable.

**`cmd_capture` captures stdout only.** stderr passes through to the parent.
Capturing both, or merging them, needs a second pipe and a select loop.

**Logging is thread-safe per call, not atomic.** The timestamp now uses
`localtime_r`, but two threads logging at once can still interleave their
output, because each record is written with several `fprintf` calls.

**The Windows paths are untested.** Every `_WIN32` branch is written but has
only ever been compiled and run on Linux.

## Queued work

**Comfort layer.** Chained arena regions, a temporary allocator along the lines
of `temp_sprintf`, grouped short flags, and a fuller `String_View`: `sv_split`,
`sv_index_of`, and numeric conversions.

**Continuous integration.** A workflow running `make test-all` under both gcc
and clang, so the guarantees the test suite provides are actually enforced.

**`bit_fields.c` is an orphan.** It prototypes a bit-flag logging scheme that
overlaps section 1 without being integrated. Either fold the idea into the
logger or drop the file.

**No README.** The header documents itself section by section, but there is no
entry point explaining what the library is or how to build the tests.
