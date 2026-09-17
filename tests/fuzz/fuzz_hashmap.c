/*
 * A differential test: the same sequence of operations is applied to the hash
 * map and to a naive array, and the two must always agree. Tombstones, the
 * rehash and the probe sequence are exactly the kind of state a hand-written
 * test explores three cases of and a fuzzer explores thousands.
 */

#define KIT_IMPLEMENTATION
#include "../../kit.h"
#include "fuzz_input.h"

#include <assert.h>
#include <stdlib.h>

/* The map stores key pointers without copying, so the keys live in a pool
 * that outlives it. A small pool means collisions and reuse, which is the
 * interesting region. */
#define POOL 64

typedef struct {
    const char *key;
    void       *value;
    bool        live;
} RefEntry;

static RefEntry ref[POOL];

static int ref_find(const char *key) {
    for (int i = 0; i < POOL; i++)
        if (ref[i].live && strcmp(ref[i].key, key) == 0) return i;
    return -1;
}

static size_t ref_count(void) {
    size_t n = 0;
    for (int i = 0; i < POOL; i++) if (ref[i].live) n++;
    return n;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 4096) return 0;
    FuzzInput in = fuzz_input(data, size);

    static char keys[POOL][8];
    for (int i = 0; i < POOL; i++) snprintf(keys[i], sizeof(keys[i]), "k%d", i);
    memset(ref, 0, sizeof(ref));

    KitMap hm = {0};

    while (fuzz_left(&in) >= 2) {
        unsigned op  = fuzz_u8(&in) % 5;
        size_t   idx = fuzz_below(&in, POOL);
        const char *key = keys[idx];

        switch (op) {
        case 0: case 1: {   /* set, weighted so the map actually fills */
            void *value = (void *)(uintptr_t)(idx + 1);
            bool  is_new_mine = kit_map_set(&hm, key, value);
            bool  is_new_ref  = (ref_find(key) < 0);
            assert(is_new_mine == is_new_ref);
            ref[idx].key = key; ref[idx].value = value; ref[idx].live = true;
            break;
        }
        case 2: {           /* delete */
            bool gone_mine = kit_map_delete(&hm, key);
            bool gone_ref  = (ref_find(key) >= 0);
            assert(gone_mine == gone_ref);
            ref[idx].live = false;
            break;
        }
        case 3: {           /* lookup */
            int at = ref_find(key);
            assert(kit_map_has(&hm, key) == (at >= 0));
            assert(kit_map_get(&hm, key) == (at >= 0 ? ref[at].value : NULL));
            break;
        }
        case 4:             /* reset, so the tombstone state is revisited */
            kit_map_reset(&hm);
            for (int i = 0; i < POOL; i++) ref[i].live = false;
            break;
        default: break;
        }

        assert(hm.count == ref_count());
        assert(hm.used >= hm.count);
        assert(hm.capacity == 0 || hm.used <= hm.capacity);
    }

    /* Every live key is reachable, and iteration visits each exactly once. */
    size_t visited = 0;
    kit_map_each(&hm, e) {
        int at = ref_find(e->key);
        assert(at >= 0);
        assert(e->value == ref[at].value);
        visited++;
    }
    assert(visited == ref_count());

    for (int i = 0; i < POOL; i++)
        if (ref[i].live) assert(kit_map_get(&hm, keys[i]) == ref[i].value);

    kit_map_free(&hm);
    return 0;
}
