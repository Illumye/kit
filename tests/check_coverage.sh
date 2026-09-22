#!/bin/sh
# Every public name of kit.h must be exercised by something that runs: an
# example, or the test suite. The examples say what the library is for and the
# tests say what it does; a name found in neither is a name nobody has ever
# run. Keeping the examples alone responsible for that was what turned one of
# them into a guided tour, which is the opposite of what an example is.
#
# What is exempt, and why:
#   compiler plumbing      KIT_ALIGNOF, KIT_CAST_LIKE, KIT_LITERAL, KIT_NORETURN,
#                          KIT_PRINTF_FORMAT, KIT_THREAD_LOCAL, KIT_MAX_ALIGN,
#                          KitMaxAlign, KIT_H: the header's own machinery
#   build-time knobs       KIT_IMPLEMENTATION and the KIT_*_CAPACITY sizes are
#                          defined by the caller, never called
#   deliberate aborts      KIT_PANIC, KIT_TODO, KIT_UNREACHABLE end the process;
#                          a caller that runs them is a caller that crashes
#   KitArenaRegion         the arena's own bookkeeping, named in the struct so
#                          that it can be declared, never by a caller

set -eu
export LC_ALL=C

HEADER=${1:-kit.h}
EXAMPLES=${2:-examples}
TESTS=${3:-tests}

exempt='^(KIT_ALIGNOF|KIT_CAST_LIKE|KIT_LITERAL|KIT_NORETURN|KIT_PRINTF_FORMAT|KIT_THREAD_LOCAL|KIT_MAX_ALIGN|KitMaxAlign|KIT_H|KIT_IMPLEMENTATION|KIT_IMPLEMENTATION_DONE|KIT_ERROR_CAPACITY|KIT_ARENA_REGION_SIZE|KIT_ARRAY_INIT_CAP|KIT_READ_CHUNK|KIT_NO_VEC_MATH|KIT_NO_THREAD_LOCAL|KIT_PANIC|KIT_TODO|KIT_UNREACHABLE|KitArenaRegion)$'

decl=$(sed -n "1,/^#endif \/\* KIT_H \*\//p" "$HEADER")
extract() { printf '%s\n' "$decl" | grep -aoE "$1" || true; }

public=$(
    {
        extract '^[A-Za-z_][A-Za-z0-9_ *]*[ *]kit_[a-z0-9_]+\(' | grep -aoE 'kit_[a-z0-9_]+'
        extract '^#[[:space:]]*define[[:space:]]+(kit_[a-z0-9_]+|KIT_[A-Z0-9_]+)' | grep -aoE '(kit_[a-z0-9_]+|KIT_[A-Z0-9_]+)$'
        extract '^\}[[:space:]]*Kit[A-Za-z0-9_]*;' | grep -aoE 'Kit[A-Za-z0-9_]*'
        extract '^typedef .*\}[[:space:]]*Kit[A-Za-z0-9_]*;' | grep -aoE 'Kit[A-Za-z0-9_]*;' | tr -d ';'
        extract '^typedef[[:space:]]+[a-zA-Z ]*[ *]Kit[A-Za-z0-9_]*;' | grep -aoE 'Kit[A-Za-z0-9_]*;' | tr -d ';'
        extract '^[[:space:]]+KIT_[A-Z0-9_]+' | tr -d ' '
    } | grep -avE '^(kit__|KIT__)' | grep -avE "$exempt" | sort -u
)

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

names() { grep -haoE '\b(kit_[a-z0-9_]+|KIT_[A-Z0-9_]+|Kit[A-Za-z0-9_]*)\b' "$@" | sort -u; }

names "$EXAMPLES"/*.c                                        > "$WORK/examples"
names "$TESTS"/*.c "$TESTS"/*.cpp "$TESTS"/fuzz/*.c          > "$WORK/tests"
sort -u "$WORK/examples" "$WORK/tests"                       > "$WORK/used"

missing=$(printf '%s\n' "$public" | grep -avxF -f "$WORK/used" || true)

total=$(printf '%s\n' "$public"   | grep -c . || true)
absent=$(printf '%s\n' "$missing" | grep -c . || true)
shown=$(printf '%s\n' "$public" | grep -axF -f "$WORK/examples" | grep -c . || true)

if [ "$absent" -gt 0 ]; then
    echo "$absent of $total public names are in no example and in no test:"
    printf '%s\n' "$missing" | sed 's/^/  /'
    exit 1
fi
echo "coverage: ok ($total public names, $shown of them in an example)"
