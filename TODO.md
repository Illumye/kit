# Known limitations and backlog

Findings from the audit of 2026-09-12 that were deliberately left alone, and
the work still queued. Everything listed here is a conscious choice, not an
oversight: the defects found during that audit are fixed and covered by tests.

## Accepted by design

**Single header, no modules.** `kit.h` is past 3400 lines and will keep
growing. Splitting it would destroy the one property that makes it useful,
namely that it drops into any project as a single file.

**Hash map keys are not copied.** `kit_map_set` stores the pointer it is given, so
the caller owns the key and must keep it alive for as long as the entry. This
is what keeps the map allocation-free on insert. A copying variant would need
an owner for the copies, which means an arena parameter or a free hook.

**`kit_fs_read` loads the whole file into memory.** Fine for configuration,
source files and manifests; wrong for anything large. A streaming reader would
be a different API, not a change to this one.

**Each thread owns its scratch regions.** The temporary allocator is
thread-local, so a thread that allocates scratch and exits without calling
`kit_scratch_free` leaves its regions behind. That is the price of never
synchronising, and it is one call to avoid.

**The string parsers report no error.** `kit_str_to_i64` and its siblings
return a bool and never log: "is this a number?" is a question, and a no is an
answer rather than a failure. Only the caller knows whether a no means the file
is malformed, and it has the context the message would need.

**`kit_command_run_args` always logs.** Varargs must come last, so the
shorthand cannot take a `KitError`. Build a `KitCommand` when the failure
matters.

**`KIT_PANIC` on allocation failure.** The dynamic arrays, the arena and the string
builder abort rather than propagate an error. For the command-line tools this
library targets, an out-of-memory condition is not recoverable and threading
the error through every macro would poison the ergonomics.

## Known limitations

**C++ needs a flexible array member.** The header compiles as C++17, but the
arena regions carry their payload in one, which ISO C++ forbids and every real
compiler accepts. `-pedantic` says so; nothing else does.

**`kit_str_next` folds the trailing empty field.** `"a,b"` and `"a,b,"`
both yield two fields. Distinguishing them needs a state bit hidden inside the
`KitStr`, which costs more than it is worth.

**`kit_array_contains` needs GNU statement expressions.** It is compiled out on MSVC,
where `kit_array_find` is the portable equivalent. An expression-valued macro
cannot be written without them.

**Windows is exercised through wine locally.** `make check-windows`
cross-compiles the suite with mingw-w64 and runs it under wine, so every
`_WIN32` branch is compiled and executed on this machine. That is not a real
Windows box: wine reimplements the API, the toolchain is mingw rather than
MSVC, and only the msvcrt runtime is covered, not UCRT. Continuous integration
covers the rest, on `windows-latest` with MSVC.

**macOS and MSVC have never been compiled here.** Their branches are reasoned
about rather than tested, and only continuous integration exercises them. Until
it has run at least once, treat both as unverified.
