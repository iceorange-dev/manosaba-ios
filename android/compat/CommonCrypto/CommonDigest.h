// Android build (plan item 301c): the SHA-256 calls of CommonCrypto the host makes (mods.c, romfs_parts.c, romfs_validate.c), on sha256.c (FIPS 180-4,
// written here; checked against the standard's vectors by android/tests/t_sha256.c).
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef uint32_t CC_LONG;
#define CC_SHA256_DIGEST_LENGTH 32
typedef struct { uint32_t h[8]; uint64_t n; uint8_t buf[64]; unsigned used; } CC_SHA256_CTX;
int CC_SHA256_Init(CC_SHA256_CTX *c);
int CC_SHA256_Update(CC_SHA256_CTX *c, const void *data, CC_LONG len);
int CC_SHA256_Final(unsigned char *md, CC_SHA256_CTX *c);
