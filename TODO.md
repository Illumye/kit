# Known limitations and backlog

Findings from the audit of 2026-09-12 that were deliberately left alone, and
the work still queued. Everything listed here is a conscious choice, not an
oversight: the defects found during that audit are fixed and covered by tests.

A second audit on 2026-09-18 went over every section against the roadmap and
listed what was missing or only half built: eleven features that did not
exist and two that stopped half way. That work is done, and the section below
is what it turned into, kept as the record of why each thing looks the way it
does. The rest of this file is the earlier audit and stands as is.

## The audit of 2026-09-18, and what came of it

The five phases this was broken into are all done. What is left below is the
one entry that was only ever half a gap, and the note about the item that
turned out to exist already.

### Filled since the audit

**Typed assertions.** `KIT_ASSERT`, `KIT_ASSERT_MSG`, `KIT_ASSERT_CMP` and
`KIT_ASSERT_STR_EQ`, section 1b. Never compiled out, and the comparison
prints both sides through a `_Generic` that maps sixteen types onto six
renderers, with an overload set standing in for it in C++.

**Checked arithmetic.** `kit_num_add`, `kit_num_sub` and `kit_num_mul`,
section 16, dispatching on the destination. The compiler's overflow builtins
where they exist, a portable path elsewhere, and both are run by the suite:
`KIT_NO_OVERFLOW_BUILTINS` forces the second one on a machine whose compiler
would never take it.

**Human-readable formatting.** `kit_fmt_size` and `kit_fmt_duration`,
section 17, writing into a buffer the caller owns. `tree.c` carried its own
copy of the first one, which is the argument for having it.

**Hex dump.** `kit_hex_dump`, section 18, in `hexdump -C`'s layout so the two
can be read side by side. `examples/cli.c` became `examples/peek.c` to host
it: the one example that was a guided tour is now a tool that says what a
file is.

**Random numbers.** `kit_random_*`, section 19, xoshiro256** seeded through
splitmix64. Reproducible from a seed on every platform, unbiased over a
range, and a zeroed generator is the default sequence rather than an
infinite run of zeros.

**Micro-benchmark.** `kit_bench_run` and `kit_bench_report`, section 20.
Warm-up excluded, minimum reported first. No standard deviation on purpose:
the square root would drag in the maths library that `KIT_NO_VEC_MATH`
exists to avoid.

**Sorting.** `kit_array_sort`, in the array section, taking the element size
and the count from the array. Not stable, since `qsort` is not.

**Ring buffer.** `kit_ring_*`, section 21, over a struct of your own like the
arrays. A window over the most recent entries, not a queue that must not
drop: the oldest going out of the front is the point.

**Heap.** `kit_heap_push` and `kit_heap_pop`, section 22, over the array
macros and ordered by a `qsort` comparator. Only `items[0]` is ordered, which
is what makes "the N largest of something long" one pass instead of a sort.

**Checksums.** `kit_crc32` and the `kit_sha256_*` family, section 23, both
agreeing byte for byte with `gzip` and `sha256sum`. CRC-32 goes four bits at
a time from a sixteen-entry table: the usual table is twice as fast and a
kilobyte of data in every binary that includes the header, which is the wrong
trade for a file meant to be copied around. `build.c` decides on content
rather than on timestamps because of them.

**UTF-8.** `kit_utf8_*`, section 24. Decoding never fails and always
advances, so a loop over bytes another program produced terminates; overlong
encodings, surrogates and anything above U+10FFFF are refused rather than
tolerated. No case folding, no normalisation, no display width: those need
the Unicode tables, which are larger than this header and change every year.

**Glob.** `kit_glob_match`, section 25, over codepoints rather than bytes.
Checked against the system's `fnmatch` over five hundred pattern and text
pairs, which is where its corner cases come from. The backtracking is
iterative: no pattern makes it recurse.

Not done, deliberately: the inline overflow guards in `kit_array_reserve`
and in the integer parsers still stand on their own. Rewiring working code
that aborts anyway would be churn for no behaviour a caller can see.

### Partially developed

**Maths is geometry and interpolation only.** `kit_clampf`/`_d`/`_i`,
`kit_lerpf` and `kit_remapf` cover the vector-maths use case the section
grew from. Checked arithmetic now sits beside it as its own section, but
there is still no general numeric toolbox: no min/max beyond two values,
nothing for fixed-point or rational values. Left alone deliberately: none of
it has turned up as something an example wanted and could not write itself.

### Already covered, despite looking missing at first glance

**A test framework exists.** `tests/utest.h` is the "integrated test
framework" item from the roadmap. It stays a second header rather than a
section of `kit.h` on purpose: test assertions are dead weight in a binary
that ships, so a library meant to be copied whole should not carry them.

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

**Demonstration in the examples, exhaustiveness in the tests.**
`tests/check_coverage.sh` requires every public name to be exercised by an
example *or* by a test. It used to require an example, full stop, and that is
what turned the second half of `wordfreq.c` into a guided tour: half a dozen
calls whose only reason to exist was the check. Eleven names now rest on the
tests alone, among them `kit_map_entry_live`, which the old rule had shown in
an example and never tested.

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
