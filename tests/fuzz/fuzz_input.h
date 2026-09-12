/*
 * A cursor over the fuzzer's byte string, so a target can ask for a number, a
 * choice or a slice without reimplementing the bounds checks each time.
 *
 * Every accessor is total: once the input runs out it keeps returning zeroes
 * rather than failing, which keeps the targets branch-free at the edges.
 */

#ifndef FUZZ_INPUT_H
#define FUZZ_INPUT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#    define FUZZ__UNUSED __attribute__((unused))   /* not every target uses all */
#else
#    define FUZZ__UNUSED
#endif

typedef struct {
    const uint8_t *data;
    size_t         size;
    size_t         at;
} FuzzInput;

FUZZ__UNUSED static FuzzInput fuzz_input(const uint8_t *data, size_t size) {
    FuzzInput in = { data, size, 0 };
    return in;
}

FUZZ__UNUSED static size_t fuzz_left(const FuzzInput *in) {
    return in->size - in->at;
}

FUZZ__UNUSED static uint8_t fuzz_u8(FuzzInput *in) {
    return fuzz_left(in) ? in->data[in->at++] : 0;
}

/* Uniform enough for choosing a branch; not for statistics. */
FUZZ__UNUSED static size_t fuzz_below(FuzzInput *in, size_t bound) {
    return bound ? (size_t)fuzz_u8(in) % bound : 0;
}

/* Hands back a slice of the remaining input, at most max bytes. */
FUZZ__UNUSED static const uint8_t *fuzz_bytes(FuzzInput *in, size_t max, size_t *out_len) {
    size_t want = fuzz_left(in);
    if (want > max) want = max;
    const uint8_t *p = in->data + in->at;
    in->at += want;
    *out_len = want;
    return p;
}

/* A NUL-terminated copy of up to max bytes, for the library functions that
 * take a C string. Embedded NULs simply end it early, as they would anywhere. */
FUZZ__UNUSED static const char *fuzz_cstr(FuzzInput *in, char *buf, size_t bufsz) {
    size_t n = 0;
    const uint8_t *p = fuzz_bytes(in, bufsz - 1, &n);
    memcpy(buf, p, n);
    buf[n] = '\0';
    return buf;
}

#endif /* FUZZ_INPUT_H */
