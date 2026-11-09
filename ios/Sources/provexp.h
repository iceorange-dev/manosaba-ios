// The signature's expiry (plan item 317b; S2Arm's ios/prov_expiry.m, Apache-2.0, written anew): the app's embedded.mobileprovision is a
// CMS-signed plist, and its ExpirationDate (<key>ExpirationDate</key><date>2026-11-15T10:20:30Z</date>) is read from the XML inside. A free
// account's signature lasts 7 days and the app then no longer opens; ios/tools/install_check.py warned only at install. main.m logs it at boot and
// the front end shows a line for 12 s when less than 48 hours are left. Header only: host/tests/t_provexp.c checks it on the Mac
#pragma once
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static inline const char *pe_find(const char *h, size_t n, const char *needle) {
    size_t k = strlen(needle); for (size_t i = 0; i + k <= n; i++) if (!memcmp(h + i, needle, k)) return h + i;
    return NULL;
}
// The profile's ExpirationDate as Unix seconds into *t (the date is UTC); 0 when it has none or it does not parse
static inline int dm_prov_expiry(const void *p, size_t n, long long *t) {
    const char *s = (const char *)p, *k = pe_find(s, n, "<key>ExpirationDate</key>"); if (!k) return 0;
    const char *d = pe_find(k, n - (size_t)(k - s), "<date>"); if (!d || d - k > 64) return 0;
    d += 6; if ((size_t)(d - s) + 20 > n) return 0;
    char b[21]; memcpy(b, d, 20); b[20] = 0;
    int Y, M, D, h, m, sec; char z = 0;
    if (sscanf(b, "%4d-%2d-%2dT%2d:%2d:%2d%c", &Y, &M, &D, &h, &m, &sec, &z) != 7 || z != 'Z') return 0;
    struct tm tm = {0}; tm.tm_year = Y - 1900; tm.tm_mon = M - 1; tm.tm_mday = D; tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = sec;
#ifndef DM_TEST_PROVEXP_LOCAL   // negative control of t_provexp only: the date read as local time
    *t = (long long)timegm(&tm);
#else
    *t = (long long)mktime(&tm);
#endif
    return 1;
}
// Whether to warn: less than 48 hours left (or past)
static inline int dm_prov_warn(long long expiry, long long now) { return expiry - now < 48 * 3600; }
