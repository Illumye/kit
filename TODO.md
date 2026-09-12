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

**No `extern "C"` guard.** The header cannot be included from C++. Adding the
guard is easy; making the body compile as C++ is not, because it relies on
implicit `void *` conversions throughout.

**The temporary allocator is global.** `temp_alloc` and friends share one
process-wide arena, so they are not thread-safe. A thread that needs scratch
space carries its own `Arena`.

**`sv_try_chop_by_delim` folds the trailing empty field.** `"a,b"` and `"a,b,"`
both yield two fields. Distinguishing them needs a state bit hidden inside the
`String_View`, which costs more than it is worth.

**`da_contains` needs GNU statement expressions.** It is compiled out on MSVC,
where `da_index_of` is the portable equivalent. An expression-valued macro
cannot be written without them.

**Separate stdout and stderr capture is not offered.** `cmd_capture` takes
stdout and `cmd_capture_merged` takes both interleaved. Two buffers would need
concurrent reads on two pipes, or the child blocks once one of them fills.

**The Windows paths are untested.** Every `_WIN32` branch is written but has
only ever been compiled and run on Linux.

## Queued work

**Continuous integration.** A workflow running `make test-all` under both gcc
and clang, so the guarantees the test suite provides are actually enforced.

**No README.** The header documents itself section by section, but there is no
entry point explaining what the library is or how to build the tests.
