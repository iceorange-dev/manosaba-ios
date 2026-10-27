// The entitlements a signer actually left in an executable's code signature (ios/Sources/main.m reads its own at boot): a signing tool decides
// them from its profile, and some drop the increased memory limit (docs/sources.md, the signing notes of 2026-10-27), so the boot log says what
// the app was signed with instead of what the IPA asked for. Header only, so host/tests/t_codesig.c checks it on the Mac.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline uint32_t cs_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline uint32_t cs_le32(const uint8_t *p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }
// The entitlements plist (XML) of a Mach-O image f[0..n) (a fat file: its arm64 slice): *len its length; NULL when unsigned or without entitlements
static inline const char *dm_codesig_entitlements(const uint8_t *f, size_t n, size_t *len) {
    if (n >= 8 && cs_be32(f) == 0xcafebabeu) {   // fat: { magic, nfat } then { cputype, cpusubtype, offset, size, align } each, big-endian
        uint32_t na = cs_be32(f + 4);
        for (uint32_t i = 0; i < na && 8 + 20 * (size_t)(i + 1) <= n; i++) {
            const uint8_t *a = f + 8 + 20 * (size_t)i; uint32_t off = cs_be32(a + 8), sz = cs_be32(a + 12);
            if (cs_be32(a) == 0x0100000cu && (size_t)off + sz <= n) return dm_codesig_entitlements(f + off, sz, len);   // CPU_TYPE_ARM64
        }
        return NULL;
    }
    if (n < 32 || cs_le32(f) != 0xfeedfacfu) return NULL;   // MH_MAGIC_64
    uint32_t ncmds = cs_le32(f + 16); size_t at = 32;
    for (uint32_t i = 0; i < ncmds && at + 8 <= n; i++) {
        uint32_t cmd = cs_le32(f + at), csz = cs_le32(f + at + 4);
        if (csz < 8 || at + csz > n) return NULL;
        if (cmd == 0x1d && csz >= 16) {   // LC_CODE_SIGNATURE: { cmd, cmdsize, dataoff, datasize }
            uint32_t doff = cs_le32(f + at + 8), dsz = cs_le32(f + at + 12);
            if ((size_t)doff + dsz > n || dsz < 12 || cs_be32(f + doff) != 0xfade0cc0u) return NULL;   // the embedded signature superblob
            const uint8_t *sb = f + doff; uint32_t cnt = cs_be32(sb + 8);
            for (uint32_t k = 0; k < cnt && 12 + 8 * (size_t)(k + 1) <= dsz; k++) {
                uint32_t type = cs_be32(sb + 12 + 8 * k), boff = cs_be32(sb + 16 + 8 * k);
                if (type != 5 || (size_t)boff + 8 > dsz) continue;   // CSSLOT_ENTITLEMENTS
                const uint8_t *b = sb + boff; uint32_t bl = cs_be32(b + 4);
                if (cs_be32(b) != 0xfade7171u || bl < 8 || (size_t)boff + bl > dsz) return NULL;
                *len = bl - 8; return (const char *)b + 8;
            }
            return NULL;
        }
        at += csz;
    }
    return NULL;
}
// key in those entitlements: 1 with <true/>, 0 with another value or absent, -1 when the image carries no entitlements
static inline int dm_codesig_bool(const uint8_t *f, size_t n, const char *key) {
    size_t len = 0; const char *x = dm_codesig_entitlements(f, n, &len);
    if (!x) return -1;
    size_t kl = strlen(key);
    for (size_t i = 0; i + kl + 11 <= len; i++) {
        if (memcmp(x + i, "<key>", 5) || memcmp(x + i + 5, key, kl) || memcmp(x + i + 5 + kl, "</key>", 6)) continue;
        size_t j = i + 11 + kl; while (j < len && (x[j] == ' ' || x[j] == '\t' || x[j] == '\n' || x[j] == '\r')) j++;
#ifdef DM_TEST_CODESIG_ANY_VALUE   // negative control of t_codesig only: the key's presence taken as true
        return 1;
#endif
        return j + 7 <= len && !memcmp(x + j, "<true/>", 7);
    }
    return 0;
}
