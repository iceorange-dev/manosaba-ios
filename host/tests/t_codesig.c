// The entitlements a signer left in a code signature (ios/Sources/codesig.h, read by the iOS app at boot): three copies of a binary, signed ad hoc
// with the increased memory limit true, with it false, and the linker's own signature without entitlements, read from their files.
// -DDM_TEST_CODESIG_ANY_VALUE (the key's presence taken as true): the false copy reads as true, 1 fails.
#include "../../ios/Sources/codesig.h"
#include <stdio.h>
#include <stdlib.h>

static int fail;
static void check(const char *what, int want, int got) { printf("%s %s: expected %d, got %d\n", want == got ? "✓" : "✗", what, want, got); fail += want != got; }
static int read_key(const char *path, const char *key) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n); if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) exit(2); fclose(f);
    int r = dm_codesig_bool(b, (size_t)n, key); free(b); return r;
}
int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: t_codesig <signed true> <signed false> <no entitlements>\n"); return 2; }
    const char *k = "com.apple.developer.kernel.increased-memory-limit";
    check("signed with the increased memory limit true: 1", 1, read_key(argv[1], k));
    check("... another key absent there: 0", 0, read_key(argv[1], "com.apple.developer.kernel.extended-virtual-addressing"));
    check("signed with it false: 0", 0, read_key(argv[2], k));
    check("the linker's signature, no entitlements: -1", -1, read_key(argv[3], k));
    printf("t_codesig: %s\n", fail ? "failed" : "all passed"); return fail != 0;
}
