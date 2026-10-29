// Time zone conversions (hle_time.c ToCalendarTime / ToPosixTime by the host's zone, plan item 128): acnh-mobile's dm/host/tests/t_timezone.c cases
// (MIT, as data) with TZ set per case: the calendar, weekday, day of the year and offset of instants in Shanghai, UTC, Tokyo, Kathmandu (+5:45), New
// York and Sydney in winter and summer, and back to the same instant; New York's 01:30 on 2026-11-01 occurring twice gives both instants in order,
// with room for one only the earlier, with none none; 02:30 on 2026-03-08, skipped, gives none; February 30 gives none with success (the SDK's
// documented result; acnh-mobile refuses it). Standard time only (-DDM_TEST_TOPOSIX_ONE: an hour repeated gives one, summer instants do not come back): 4 fail. The user clock context
// (plan item 158): Horizon's layout, kept while both clocks move together, made again on a jump; the old layout (-DDM_TEST_CTX_OLD): 2 fail.
// CalculateSpanBetween's bool (plan item 288); the old Result answers (-DDM_TEST_SPAN_RESULT): 3 fail.
#include "../hle/hle_time.c"
#include <stdio.h>
struct dm_flags g_flag; uint64_t g_t0_ns;
module_t g_mod[MAX_MOD]; int g_nmod;
static uint64_t g_guest;
uint64_t guest_ns(void) { return g_guest; }
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static void zone(const char *z) { setenv("TZ", z, 1); tzset(); }
int main(void) {
    static const struct { const char *z; int64_t t; int y, mo, d, h, mi, off, wday, yday; } c[] = {
        {"Asia/Shanghai", 1792367400, 2026, 10, 19, 7, 50, 28800, 1, 291}, {"UTC", 1792368600, 2026, 10, 19, 0, 10, 0, 1, 291},
        {"Asia/Tokyo", 1767297600, 2026, 1, 2, 5, 0, 32400, 5, 1}, {"Asia/Kathmandu", 1780281000, 2026, 6, 1, 8, 15, 20700, 1, 151},
        {"America/New_York", 1767268800, 2026, 1, 1, 7, 0, -18000, 4, 0}, {"America/New_York", 1782907200, 2026, 7, 1, 8, 0, -14400, 3, 181},
        {"Australia/Sydney", 1767268800, 2026, 1, 1, 23, 0, 39600, 4, 0}, {"Australia/Sydney", 1782907200, 2026, 7, 1, 22, 0, 36000, 3, 181}};
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        zone(c[i].z); CalendarTime ct; CalendarAdditionalInfo ai; int64_t out[2] = {0}; int32_t cnt = -1;
        Result r1 = h_time_ToCalendarTime(&ct, &ai, &c[i].t), r2 = h_time_ToPosixTime(&cnt, out, 2, &ct);
        CHECK(!r1 && ct.year == c[i].y && ct.month == c[i].mo && ct.day == c[i].d && ct.hour == c[i].h && ct.minute == c[i].mi && ai.offset == c[i].off && ai.wday == c[i].wday && ai.yday == c[i].yday
              && !r2 && cnt == 1 && out[0] == c[i].t, "%-16s %lld: %04d-%02d-%02d %02d:%02d, offset %d, weekday %d, day %d, and back", c[i].z, (long long)c[i].t, ct.year, ct.month, ct.day, ct.hour, ct.minute, ai.offset, ai.wday, ai.yday);
    }
    zone("America/New_York"); int64_t out[2] = {0}; int32_t cnt = -1;
    CalendarTime fall = {2026, 11, 1, 1, 30, 0, 0}, spring = {2026, 3, 8, 2, 30, 0, 0}, feb30 = {2026, 2, 30, 12, 0, 0, 0};
    CHECK(!h_time_ToPosixTime(&cnt, out, 2, &fall) && cnt == 2 && out[0] == 1793511000LL && out[1] == 1793514600LL, "New York 2026-11-01 01:30, twice: both instants in order (%d)", cnt);
    CHECK(!h_time_ToPosixTime(&cnt, out, 1, &fall) && cnt == 1 && out[0] == 1793511000LL, "... room for one: the earlier");
    CHECK(!h_time_ToPosixTime(&cnt, NULL, 0, &fall) && cnt == 0, "... room for none: none");
    CHECK(!h_time_ToPosixTime(&cnt, out, 2, &spring) && cnt == 0, "New York 2026-03-08 02:30, skipped: none");
    CHECK(!h_time_ToPosixTime(&cnt, out, 2, &feb30) && cnt == 0, "February 30: none, with success (the SDK)");
    // The user clock context in Horizon's layout (plan item 158): offset + steady time point = user time, the point the guest clock in seconds,
    // source 0; the same bytes while the host and guest clocks move together; made again when the guest clock jumps 60 s against the host clock
    {   uint8_t a[0x20], b[0x20], c[0x20]; g_guest = 100000000000ull; h_time_GetSystemClockContext(a); int64_t now = time(NULL);
        SystemClockContext ca; memcpy(&ca, a, sizeof ca); int src0 = 1; for (int i = 0; i < 16; i++) src0 &= !ca.steady.source[i];
        CHECK(llabs(ca.offset + ca.steady.timepoint - now) <= 1 && ca.steady.timepoint == 100 && src0, "context: offset %lld + time point %lld = the user time %lld, source 0",
              (long long)ca.offset, (long long)ca.steady.timepoint, (long long)now);
        g_guest += 1000000000ull; h_time_GetSystemClockContext(b);
        CHECK(!memcmp(a, b, sizeof a), "... the same bytes a second later on both clocks (a game compares all 0x20)");
        g_guest += 60000000000ull; h_time_GetSystemClockContext(c); SystemClockContext cc; memcpy(&cc, c, sizeof cc); now = time(NULL);
        CHECK(memcmp(a, c, sizeof a) && cc.steady.timepoint == 161 && llabs(cc.offset + cc.steady.timepoint - now) <= 1, "... made again when the guest clock jumps 60 s (time point %lld)", (long long)cc.steady.timepoint); }

    // CalculateSpanBetween returns bool (plan item 288): true with the span when both snapshots have the same steady clock source or both have a
    // network time, false otherwise
    {   ClockSnapshot a = {0}, b = {0}; int64_t span = 0; a.steady.timepoint = 100; b.steady.timepoint = 130;
        CHECK(h_time_SpanBetween(&span, &a, &b) == true && span == 30000000000ll, "span between: the same steady source, true and 30 s (%lld ns)", (long long)span);
        b.steady.source[0] = 1; a.net_time = 1000; b.net_time = 1045; span = 0;
        CHECK(h_time_SpanBetween(&span, &a, &b) == true && span == 45000000000ll, "... other sources and both network times: true and 45 s (%lld ns)", (long long)span);
        b.net_time = 0;
        CHECK(h_time_SpanBetween(&span, &a, &b) == false, "... other sources and one network time missing: false");
    }

    printf("t_time: %d checks, %d failed\n", n, fail); return fail != 0;
}
