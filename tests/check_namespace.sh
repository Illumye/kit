#!/bin/sh
# Every name kit.h adds to a translation unit must carry the kit prefix. This
# checks the three places a name can leak from: macros, symbols with external
# linkage, and file-scope types and enumerators. Linux only, since it leans on
# nm and on the POSIX half of the header.

set -eu
export LC_ALL=C

CC=${CC:-cc}
HEADER=${1:-kit.h}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

prefixed='^(kit_|kit__|Kit|KIT_|KIT__)'
# Feature-test macros are requests to the C library, not names of ours. Each
# library has its own: glibc and musl read _DEFAULT_SOURCE, macOS
# _DARWIN_C_SOURCE.
allowed='^(_DEFAULT_SOURCE|_DARWIN_C_SOURCE|__USE_MINGW_ANSI_STDIO|_CRT_SECURE_NO_WARNINGS|WIN32_LEAN_AND_MEAN)$'
status=0

# --- macros ------------------------------------------------------------------
# The same system headers, with the same feature macro, minus kit.h. Whatever
# is left over was defined by the library.
grep -E '^#\s*include\s*<' "$HEADER" \
    | grep -vE '<(windows|io)\.h>' \
    | sed 's/^#\s*include/#include/' > "$WORK/system.h"

case $(uname -s) in
    Darwin) feature='#define _DARWIN_C_SOURCE';;
    *)      feature='#define _DEFAULT_SOURCE';;
esac
{ echo "$feature"; cat "$WORK/system.h"; } > "$WORK/base.c"
{ echo '#define KIT_IMPLEMENTATION'; echo "#include \"$PWD/$HEADER\""; } > "$WORK/kit.c"

$CC -std=c11 -dM -E "$WORK/base.c" | awk '{print $2}' | sed 's/(.*//' | sort -u > "$WORK/base.macros"
$CC -std=c11 -dM -E "$WORK/kit.c"  | awk '{print $2}' | sed 's/(.*//' | sort -u > "$WORK/kit.macros"

leaked=$(comm -13 "$WORK/base.macros" "$WORK/kit.macros" | grep -avE "$prefixed" | grep -avE "$allowed" || true)
if [ -n "$leaked" ]; then
    echo "unprefixed macros:"; echo "$leaked" | sed 's/^/  /'; status=1
fi

# --- external symbols ----------------------------------------------------------
# --defined-only is GNU and llvm; the BSD nm of an older macOS spells it -U.
# Mach-O also prefixes every symbol with an underscore, which is the platform's
# doing rather than the library's, so it is stripped before the check.
$CC -std=c11 -c "$WORK/kit.c" -o "$WORK/kit.o"
defined_symbols() {
    nm -g --defined-only "$WORK/kit.o" 2>/dev/null || nm -g -U "$WORK/kit.o"
}
leaked=$(defined_symbols | awk '{print $3}' | sed 's/^_//' | grep -avE "$prefixed" | grep -av '^$' || true)
if [ -n "$leaked" ]; then
    echo "unprefixed symbols:"; echo "$leaked" | sed 's/^/  /'; status=1
fi

# --- types and enumerators -----------------------------------------------------
# Relies on the header's own layout: one typedef or enumerator per line. Each
# extraction may legitimately match nothing, hence the "|| true": under set -e
# an empty grep would otherwise end the whole group.
decl=$(sed -n '1,/^#endif \/\* KIT_H \*\//p' "$HEADER")
extract() { printf '%s\n' "$decl" | grep -aoE "$1" || true; }
names=$(
    {
        extract '^\}\s*[A-Za-z_][A-Za-z0-9_]*;'                     | grep -aoE '[A-Za-z_][A-Za-z0-9_]*'
        extract '^typedef .*\}\s*[A-Za-z_][A-Za-z0-9_]*;'             | grep -aoE '[A-Za-z_][A-Za-z0-9_]*;$' | tr -d ';'
        extract '^typedef\s+[^;{]*\s\*?[A-Za-z_][A-Za-z0-9_]*;'    | grep -aoE '[A-Za-z_][A-Za-z0-9_]*;$' | tr -d ';'
        extract '^struct\s+[A-Za-z_][A-Za-z0-9_]*'                  | awk '{print $2}'
        extract '^\s+[A-Z][A-Z0-9_]+(\s*=[^,]*)?,?\s*(/\*.*)?$'     | grep -aoE '^\s+[A-Z][A-Z0-9_]+' | tr -d ' '
        extract '^static inline [^(]*[ *][a-z_][a-z0-9_]*\('         | grep -aoE '[a-z_][a-z0-9_]*\($' | tr -d '('
    } | grep -av '^$' | sort -u
)
leaked=$(printf '%s\n' "$names" | grep -avE "$prefixed" | grep -av '^$' || true)
if [ -n "$leaked" ]; then
    echo "unprefixed types, enumerators or inline functions:"; echo "$leaked" | sed 's/^/  /'; status=1
fi

[ $status -eq 0 ] && echo "namespace: ok ($(comm -13 "$WORK/base.macros" "$WORK/kit.macros" | wc -l) macros, $(defined_symbols | wc -l) symbols, $(echo "$names" | wc -l) types and constants)"
exit $status
