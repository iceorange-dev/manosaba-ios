// android/compat/sha256.c against FIPS 180-4's vectors ("abc", the 448-bit message, a million 'a's) and against itself fed in uneven pieces.
// Built on the Mac as well as for the phone (android/Makefile test). -DDM_TEST_SHA256_ROUND63 (the last round left out): 4 fail.
#include "CommonCrypto/CommonDigest.h"
#include <stdio.h>
#include <string.h>
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static void hex(const unsigned char *d, char *out) { for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", d[i]); }
static void sum(const void *p, size_t len, char *out) { CC_SHA256_CTX c; unsigned char d[32]; CC_SHA256_Init(&c); CC_SHA256_Update(&c, p, (CC_LONG)len); CC_SHA256_Final(d, &c); hex(d, out); }
int main(void) {
    char h[65];
    sum("abc", 3, h); CHECK(!strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "\"abc\"");
    const char *m2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sum(m2, strlen(m2), h); CHECK(!strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"), "the 448-bit message");
    static char a[1000000]; memset(a, 'a', sizeof a);
    sum(a, sizeof a, h); CHECK(!strcmp(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"), "a million 'a's");
    CC_SHA256_CTX c; unsigned char d[32]; char h2[65]; CC_SHA256_Init(&c);
    for (size_t off = 0, k = 1; off < sizeof a; off += k, k = k * 3 % 997 + 1) CC_SHA256_Update(&c, a + off, (CC_LONG)(off + k > sizeof a ? sizeof a - off : k));
    CC_SHA256_Final(d, &c); hex(d, h2); CHECK(!strcmp(h2, h), "the same in pieces of 1 to 997 bytes");
    sum("", 0, h); CHECK(!strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "the empty message");
    printf("t_sha256: %d checks, %d failed\n", n, fail); return fail != 0;
}
