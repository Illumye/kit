/*
 * KitStr: the numeric parsers are checked against the standard library,
 * the rest against their own invariants. A view never owns its bytes, so any
 * pointer that walks off the slice shows up as a read overflow.
 */

#define KIT_IMPLEMENTATION
#include "../../kit.h"
#include "fuzz_input.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

/* What kit_str_to_i64 must agree with: base 10, whole string consumed, no leading
 * space, no overflow. strtoll skips whitespace and we do not, so that case is
 * excluded rather than compared. */
static bool ref_i64(const char *s, size_t n, int64_t *out) {
    if (n == 0 || memchr(s, '\0', n)) return false;
    if (isspace((unsigned char)s[0])) return false;

    char *end;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (end != s + n || errno == ERANGE) return false;
    *out = (int64_t)v;
    return true;
}

static bool ref_u64(const char *s, size_t n, uint64_t *out) {
    if (n == 0 || memchr(s, '\0', n)) return false;
    if (isspace((unsigned char)s[0])) return false;
    if (s[0] == '-') return false;              /* strtoull would wrap this */

    char *end;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (end != s + n || errno == ERANGE) return false;
    *out = (uint64_t)v;
    return true;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 4096) return 0;
    FuzzInput in = fuzz_input(data, size);

    /* A heap slice, so a read one byte past either end is a fault. */
    size_t         n    = 0;
    const uint8_t *seed = fuzz_bytes(&in, 256, &n);
    char          *text = malloc(n ? n : 1);
    memcpy(text, seed, n);
    KitStr sv = kit_str_from_parts(text, n);

    /* --- numeric parsers, against the standard library --------------------- */
    {
        char *terminated = malloc(n + 1);
        memcpy(terminated, text, n);
        terminated[n] = '\0';

        int64_t  mine_i = 0, ref = 0;
        bool     ok_mine = kit_str_to_i64(sv, &mine_i);
        bool     ok_ref  = ref_i64(terminated, n, &ref);
        assert(ok_mine == ok_ref);
        if (ok_mine) assert(mine_i == ref);

        uint64_t mine_u = 0, ref_uv = 0;
        ok_mine = kit_str_to_u64(sv, &mine_u);
        ok_ref  = ref_u64(terminated, n, &ref_uv);
        assert(ok_mine == ok_ref);
        if (ok_mine) assert(mine_u == ref_uv);

        /* A failed parse must leave the destination alone. */
        int64_t sentinel = 0x5eed;
        if (!kit_str_to_i64(sv, &sentinel)) assert(sentinel == 0x5eed);

        double d = 0;
        (void)kit_str_to_double(sv, &d);   /* no reference: rounding is not ours */
        free(terminated);
    }

    /* --- slicing keeps every result inside the original view --------------- */
    {
        KitStr rest = sv, field;
        size_t      total = 0, rounds = 0;
        while (kit_str_next(&rest, (char)fuzz_u8(&in), &field)) {
            assert(field.data >= sv.data && field.data + field.count <= sv.data + sv.count);
            total += field.count;
            assert(total <= sv.count);
            if (++rounds > sv.count + 1) assert(0 && "chop loop does not terminate");
        }
        assert(rest.count == 0);
    }

    /* --- search agrees with itself ----------------------------------------- */
    {
        size_t         nn = 0;
        const uint8_t *nb = fuzz_bytes(&in, 16, &nn);
        KitStr needle = kit_str_from_parts((const char *)nb, nn);

        size_t at = kit_str_find(sv, needle);
        assert(kit_str_contains(sv, needle) == (at != KIT_NPOS));
        if (at != KIT_NPOS) {
            assert(at + needle.count <= sv.count);
            assert(memcmp(sv.data + at, needle.data, needle.count) == 0);
            /* Nothing was skipped: the first match really is the first. */
            for (size_t i = 0; i < at; ++i)
                assert(memcmp(sv.data + i, needle.data, needle.count) != 0);
        }

        char c = (char)fuzz_u8(&in);
        size_t ic = kit_str_find_char(sv, c);
        if (ic != KIT_NPOS) {
            assert(ic < sv.count && sv.data[ic] == c);
            assert(memchr(sv.data, c, ic) == NULL);
        } else {
            assert(memchr(sv.data, c, sv.count) == NULL);
        }
    }

    /* --- trimming and chopping stay within bounds -------------------------- */
    {
        KitStr t = kit_str_trim(sv);
        assert(t.count <= sv.count);
        assert(t.data >= sv.data && t.data + t.count <= sv.data + sv.count);

        KitStr left = sv, right = sv;
        KitStr a = kit_str_take(&left, fuzz_below(&in, sv.count + 2));
        KitStr b = kit_str_take_right(&right, fuzz_below(&in, sv.count + 2));
        assert(a.count + left.count == sv.count);
        assert(b.count + right.count == sv.count);
    }

    free(text);
    return 0;
}
