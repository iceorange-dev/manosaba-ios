// The signature's expiry from embedded.mobileprovision (ios/Sources/provexp.h, plan item 317b): a profile's XML between CMS bytes gives
// 2026-11-15T10:20:30Z as 1794738030 (UTC, whatever the time zone: Asia/Taipei here); a profile without the key, and one whose date does not parse,
// give none; 47 hours left warns, 49 hours do not, a past date warns. The date read as local time (-DDM_TEST_PROVEXP_LOCAL): 1 fails.
#include <stdio.h>
#include <stdlib.h>
#include "../../ios/Sources/provexp.h"
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("  %s %s\n", ok ? "✓" : "✗", what); }
int main(void) {
    setenv("TZ", "Asia/Taipei", 1); tzset();
    static const char prof[] = "\x30\x82\x0f\x00\x06\x09junk\0\x01<?xml version=\"1.0\"?><plist><dict><key>AppIDName</key><string>x</string>"
                               "<key>ExpirationDate</key>\n\t<date>2026-11-15T10:20:30Z</date><key>Name</key><string>y</string></dict></plist>\x01\x02";
    long long t = 0; int ok = dm_prov_expiry(prof, sizeof prof - 1, &t);
    check("ExpirationDate 2026-11-15T10:20:30Z read as 1794738030, UTC [-DDM_TEST_PROVEXP_LOCAL: 8 hours off]", ok && t == 1794738030LL);
    static const char none[] = "<plist><dict><key>Name</key><string>y</string></dict></plist>";
    static const char bad[] = "<plist><dict><key>ExpirationDate</key><date>tomorrow</date></dict></plist>";
    long long u = 0; check("no ExpirationDate: none", !dm_prov_expiry(none, sizeof none - 1, &u));
    check("a date that does not parse: none", !dm_prov_expiry(bad, sizeof bad - 1, &u));
    check("the key at the very end with no date after it: none", !dm_prov_expiry(prof, (size_t)(pe_find(prof, sizeof prof - 1, "<date>") - prof), &u));
    check("47 hours left warns, 49 do not, past warns", dm_prov_warn(t, t - 47 * 3600) && !dm_prov_warn(t, t - 49 * 3600) && dm_prov_warn(t, t + 60));
    printf("t_provexp: %d checks, %d failed\n", n, fail); return fail != 0;
}
