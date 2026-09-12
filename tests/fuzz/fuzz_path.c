/*
 * Path helpers write into a caller's buffer, so the interesting question is
 * whether they ever write outside it. Every destination is heap allocated at
 * exactly the requested size, which turns a one-byte overrun into a fault
 * rather than into silence.
 */

#define UTILS_IMPLEMENTATION
#include "../../utils.h"
#include "fuzz_input.h"

#include <assert.h>
#include <stdlib.h>

/* The result must be NUL-terminated within the buffer, and never longer. */
static void check_terminated(const char *buf, size_t bufsz) {
    if (bufsz == 0) return;                 /* nothing may be written at all */
    assert(memchr(buf, '\0', bufsz) != NULL);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 4096) return 0;
    FuzzInput in = fuzz_input(data, size);

    char a[256], b[256];
    fuzz_cstr(&in, a, sizeof(a));
    fuzz_cstr(&in, b, sizeof(b));

    /* Sizes worth probing: 0 and 1 are where the arithmetic used to wrap. */
    size_t bufsz = fuzz_below(&in, 40);
    char  *buf   = bufsz ? malloc(bufsz) : malloc(1);

    path_dirname(a, buf, bufsz);
    check_terminated(buf, bufsz);

    path_join(buf, bufsz, a, b);
    check_terminated(buf, bufsz);

    /* Joining must never invent a byte the inputs did not have, beyond one
     * separator, so the result cannot be longer than both plus one. */
    if (bufsz > 0) {
        size_t len = strlen(buf);
        assert(len < bufsz);
        assert(len <= strlen(a) + strlen(b) + 1);
    }

    /* The read-only helpers return pointers into their argument. */
    const char *base = path_basename(a);
    assert(base >= a && base <= a + strlen(a));

    const char *ext = path_ext(a);
    assert(ext >= a && ext <= a + strlen(a));
    assert(*ext == '\0' || *ext == '.');

    (void)path_is_absolute(a);

    /* Joining onto its own output must stay bounded too, which is how a
     * caller builds a path one component at a time. */
    if (bufsz > 1) {
        char *acc = malloc(bufsz);
        snprintf(acc, bufsz, "%s", a);
        for (int i = 0; i < 4; i++) {
            char comp[16];
            fuzz_cstr(&in, comp, sizeof(comp));
            char *tmp = malloc(bufsz);
            path_join(tmp, bufsz, acc, comp);
            check_terminated(tmp, bufsz);
            memcpy(acc, tmp, bufsz);
            free(tmp);
        }
        free(acc);
    }

    free(buf);
    return 0;
}
