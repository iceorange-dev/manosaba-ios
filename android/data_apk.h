// The game data an APK carries (android/data_apk.c; apk.py --data, plan item 301h)
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct { uint64_t size; char path[400]; } dm_data_ent;
// An index (apk.py's assets/<name>.idx: "size<TAB>path" a line) into out (at most max): how many, or -1 when a line is not that or its path is
// not a plain relative one (empty, absolute, "." or ".." components, a backslash, a control character)
int dm_data_idx_parse(const char *text, size_t n, dm_data_ent *out, int max);
#ifdef __ANDROID__
#include <android/asset_manager.h>
// The bytes of assets/<name>/ that dir does not hold yet (a file there of the same size counts as held): -1 when the APK carries no <name>
int64_t dm_data_need(AAssetManager *am, const char *name, const char *dir);
// Those files copied into dir (each through <file>.part, then renamed); progress(bytes done, bytes to do, ctx) along the way. 0, or -1 (reported)
int dm_data_out(AAssetManager *am, const char *name, const char *dir, void (*progress)(uint64_t done, uint64_t total, void *ctx), void *ctx);
#endif
