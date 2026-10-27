// Plan item 271: 8-byte keys apart only in their high half are two entries (an 8-byte key compared by its low half, -DDM_TEST_HM_KEQ_HALF: 1 fails).
// Hash table unit tests: random insert / overwrite / delete / clear checked against a naive array model; small key space, key lengths 8 and 20 (including a tail that is not a multiple of 8), to force long probe chains and backward-shift deletion.
#include "../nvn/nvn_hmap.h"
#include <stdio.h>

#define KS 3000
static uint64_t rng = 88172645463325252ull;
static uint64_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static int run(uint32_t klen, int ops) {
    static void *ref[KS]; memset(ref, 0, sizeof ref);
    nvn_hmap m; nvn_hm_init(&m, klen);
    uint8_t k[32]; int bad = 0, live = 0;
    for (int t = 0; t < ops && !bad; t++) {
        uint32_t x = (uint32_t)(rnd() % KS); memset(k, 0xab, sizeof k); memcpy(k, &x, 4);
        int op = (int)(rnd() % 10);
        if (op < 5) { void *v = (void *)(uintptr_t)(t + 1), *old = nvn_hm_put(&m, k, v); bad |= old != ref[x]; live += !ref[x]; ref[x] = v; }
        else if (op < 8) { void *old = nvn_hm_del(&m, k); bad |= old != ref[x]; live -= !!ref[x]; ref[x] = NULL; }
        else bad |= nvn_hm_get(&m, k) != ref[x];
        if (t % 50000 == 49999) { nvn_hm_clear(&m, NULL); memset(ref, 0, sizeof ref); live = 0; }
        bad |= (int)m.n != live;
    }
    for (uint32_t x = 0; x < KS && !bad; x++) { memset(k, 0xab, sizeof k); memcpy(k, &x, 4); bad |= nvn_hm_get(&m, k) != ref[x]; }   // full comparison of the final state
    printf("key length %2u: %d random operations + full comparison of %d keys in the final state, expected all equal → got %s (final state %u entries, capacity %u)\n", klen, ops, KS, bad ? "not equal ✗" : "all equal ✓", m.n, m.cap);
    free(m.keys); free(m.vals);
    return bad;
}
// Plan item 271: 8-byte keys compared as one word; two that share their low half (addresses apart by 4 GB) stay two entries
static int high_half(void) {
    nvn_hmap m; nvn_hm_init(&m, 8); enum { N = 2000 };   // enough that their probes meet
    for (uint64_t i = 0; i < N; i++) { uint64_t k = i << 32 | 0x1234; nvn_hm_put(&m, &k, (void *)(uintptr_t)(i + 1)); }
    int ok = m.n == N; for (uint64_t i = 0; i < N && ok; i++) { uint64_t k = i << 32 | 0x1234; ok = nvn_hm_get(&m, &k) == (void *)(uintptr_t)(i + 1); }
    printf("2000 8-byte keys equal in their low half, apart in the high: 2000 entries, each found → %s [-DDM_TEST_HM_KEQ_HALF]\n", ok ? "✓" : "✗");
    free(m.keys); free(m.vals); return !ok;
}
int main(void) {
    int fail = run(8, 190000) + run(20, 190000) + high_half();
    printf("== 3 checks, %d failed ==\n", fail);
    return fail != 0;
}
