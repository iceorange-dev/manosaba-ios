// nn::time: a real device gets the network-calibrated user clock through the time service; the host reads the system clock directly (single machine, offline).
// Layout follows the public Horizon ABI: PosixTime = int64 seconds; CalendarTime = {s16 year, s8 month/day/hour/minute/second};
// CalendarAdditionalInfo = {u32 day of week, u32 day of year, char time zone name[8], u32 DST, s32 UTC offset in seconds}.
#include "hle.h"
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

typedef struct { int16_t year; int8_t month, day, hour, minute, second; int8_t pad; } CalendarTime;
typedef struct { uint32_t wday, yday; char tzname[8]; uint32_t dst; int32_t offset; } CalendarAdditionalInfo;

// DM_FIXED_TIME=seconds (deterministic test runs): the user clock always returns this value (main.c then also pins TZ to UTC).
static int64_t fixed_time(void) {
    static int64_t v = -2;
    if (v == -2) { const char *e = getenv("DM_FIXED_TIME"); v = e ? strtoll(e, 0, 0) : -1; if (v >= 0) LOG("DM_FIXED_TIME=%lld: the user clock is fixed", (long long)v); }
    return v;
}
HLE_FORCE("_ZN2nn4time10InitializeEv", "connects to the time service; clocks come from the host system time", Result, h_time_Initialize, void) { return RES_OK; }
HLE_FORCE("_ZN2nn4time13IsInitializedEv", "the time service is taken over: always initialized", int, h_time_IsInitialized, void) { return 1; }
// DM_TEST_SDK_TIME_SKEW=1: the negative control of host/diag/sdktest.c's time checks only: the user clock an hour ahead
static int64_t skew(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_TEST_SDK_TIME_SKEW"); v = e && !strcmp(e, "1"); if (v) LOG("DM_TEST_SDK_TIME_SKEW=1: the user clock an hour ahead (a test)"); } return v ? 3600 : 0; }
HLE_FORCE("_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE", "user clock → host time()", Result, h_time_GetCurrentTime, int64_t *out) {
    int64_t ft = fixed_time(); *out = (ft >= 0 ? ft : time(NULL)) + skew(); return RES_OK;
}
HLE_FORCE("_ZN2nn4time23StandardUserSystemClock3nowEv", "user clock → host time()", int64_t, h_time_now, void) {
    int64_t ft = fixed_time(); return ft >= 0 ? ft : time(NULL); }
HLE_FORCE("_ZN2nn4time23StandardUserSystemClock9to_time_tERKNSt3__16chrono10time_pointINS0_17SystemClockTraitsENS3_8durationIlNS2_5ratioILl1ELl1EEEEEEE",
          "pairs with now(), taken over with it", int64_t, h_time_to_time_t, const int64_t *tp) { return *tp; }
HLE_FORCE("_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeE",
          "time zone rules of the time service → host localtime_r", Result, h_time_ToCalendarTime, CalendarTime *ct, CalendarAdditionalInfo *ai, const int64_t *pt) {
    time_t t = *pt; struct tm tm; localtime_r(&t, &tm);
    static int once; if (!once++) LOG("ToCalendarTime: first conversion %lld → %04d-%02d-%02d %02d:%02d (zone %s, offset %ld s)", (long long)t, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_zone ? tm.tm_zone : "?", (long)tm.tm_gmtoff);
    if (ct) *ct = (CalendarTime){(int16_t)(tm.tm_year + 1900), (int8_t)(tm.tm_mon + 1), (int8_t)tm.tm_mday, (int8_t)tm.tm_hour, (int8_t)tm.tm_min, (int8_t)tm.tm_sec, 0};
    if (ai) { memset(ai, 0, sizeof *ai); ai->wday = tm.tm_wday; ai->yday = tm.tm_yday; strlcpy(ai->tzname, tm.tm_zone ? tm.tm_zone : "UTC", sizeof ai->tzname);
              ai->dst = tm.tm_isdst > 0; ai->offset = (int32_t)tm.tm_gmtoff; }
    return RES_OK;
}
// nn::time::ToPosixTime (device time zone overload): local calendar time → absolute times, by the host time zone (same source as ToCalendarTime above).
// The SDK one needs the time service and fails; Unity's IL2CPP time zone lookup (manosaba main+0x394e30, behind TimeZoneInfo.Local) then reports no
// offset, so DateTime.Now came out in UTC. SDK docs: 0–2 results; 2 when the local time occurs twice (offset change turning the clock back), ascending;
// 0 for a skipped local time or a date that does not exist (e.g. February 31). Each mktime result is checked by converting it back.
HLE_FORCE_OPT("_ZN2nn4time11ToPosixTimeEPiPNS0_9PosixTimeEiRKNS0_12CalendarTimeE", "device time zone conversion → host mktime / localtime_r",
              Result, h_time_ToPosixTime, int32_t *count, int64_t *out, int32_t max, const CalendarTime *ct) {
    int n = 0; int64_t r[2];
#ifndef DM_TEST_TOPOSIX_ONE   // negative control of t_time only: standard time only (an hour repeated at the fall change gives one result)
    for (int dst = 0; dst <= 1; dst++) {
#else
    for (int dst = 0; dst <= 0; dst++) {
#endif
        struct tm tm = {.tm_year = ct->year - 1900, .tm_mon = ct->month - 1, .tm_mday = ct->day, .tm_hour = ct->hour, .tm_min = ct->minute,
                        .tm_sec = ct->second, .tm_isdst = dst};
        time_t t = mktime(&tm); struct tm back;
        if (t == (time_t)-1 || !localtime_r(&t, &back)) continue;
        if (back.tm_year != ct->year - 1900 || back.tm_mon != ct->month - 1 || back.tm_mday != ct->day || back.tm_hour != ct->hour ||
            back.tm_min != ct->minute || back.tm_sec != ct->second) continue;
        if (n && r[0] == (int64_t)t) continue;
        r[n++] = (int64_t)t;
    }
    if (n == 2 && r[0] > r[1]) { int64_t x = r[0]; r[0] = r[1]; r[1] = x; }
    if (n > max) n = max;
    for (int i = 0; i < n; i++) out[i] = r[i];
    *count = n;
    return RES_OK;
}
// Local time conversion goes to the host libc: the SDK's own localtime / localtime_r / mktime have no time zone data and always use UTC (a game's
// localtime + strftime came out hours off). On a real Switch they follow the system time zone, so here they follow the host's; TZ is pinned to UTC
// only with DM_FIXED_TIME (main.c). struct tm has the same layout on both sides (9 ints, long offset, zone name pointer).
// gmtime / gmtime_r / timegm are UTC anyway and are not taken over. The same-named pass-through entries in hle_table.c only take effect when the symbol has no definition (the SDK defines them), hence HLE_FORCE
HLE_FORCE("localtime", "local time → host localtime (system time zone)", struct tm *, h_localtime, const int64_t *t) { time_t v = (time_t)*t; return localtime(&v); }
HLE_FORCE("localtime_r", "local time → host localtime_r (system time zone)", struct tm *, h_localtime_r, const int64_t *t, struct tm *out) { time_t v = (time_t)*t; return localtime_r(&v, out); }
HLE_FORCE("mktime", "local calendar → seconds, host mktime (system time zone)", int64_t, h_mktime, struct tm *tm) { return (int64_t)mktime(tm); }

// The clock sources of libc clock_gettime (this SDK, 22.x): nntimeStartup (sdk+0x439130), once nn::time::Initialize succeeds, switches
// CLOCK_REALTIME to GetStandardUserSystemClockCurrentTimespec and CLOCK_MONOTONIC to GetStandardSteadyClockCurrentTimespec
// (sources set at sdk+0x9e39c / +0x9e464; clock_gettime at sdk+0xa4350 calls through a function pointer and reports EAGAIN on a non-zero return). The native implementations need the time service, which the host does not have,
// so clock_gettime failed and std::chrono::system_clock::now threw system_error and terminated the process. Here they are wired to host clocks:
//   user clocks (standard / adjustable) = same source as StandardUserSystemClock::GetCurrentTime (wall-clock seconds, a fixed value under DM_FIXED_TIME) + wall-clock nanoseconds;
//   steady clock = the guest clock guest_ns (same source as svc GetSystemTick; before the switch the SDK's default monotonic clock was converted from GetSystemTick).
// timespec = {int64 seconds, int64 nanoseconds} (as written by the SDK's default source sdk+0x9e410); return 0 = success.
static int user_timespec(int64_t *ts) {
    struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
    int64_t ft = fixed_time();
    ts[0] = ft >= 0 ? ft : (int64_t)t.tv_sec; ts[1] = ft >= 0 ? 0 : (int64_t)t.tv_nsec;
    return 0;
}
HLE_FORCE_OPT("_ZN2nn4time41GetStandardUserSystemClockCurrentTimespecEP8timespec", "libc CLOCK_REALTIME source: host wall clock", int, h_time_UserTs, int64_t *ts) { return user_timespec(ts); }
HLE_FORCE_OPT("_ZN2nn4time43GetAdjustableUserSystemClockCurrentTimespecEP8timespec", "libc adjustable user clock source: host wall clock", int, h_time_AdjTs, int64_t *ts) { return user_timespec(ts); }
HLE_FORCE_OPT("_ZN2nn4time37GetStandardSteadyClockCurrentTimespecEP8timespec", "libc CLOCK_MONOTONIC source: guest clock", int, h_time_SteadyTs, int64_t *ts) {
    uint64_t ns = guest_ns(); ts[0] = (int64_t)(ns / 1000000000ull); ts[1] = (int64_t)(ns % 1000000000ull); return 0;
}

// ---------- Clock snapshots and the steady / network clocks (TotK). The SDK reads them from the time service or its shared memory, neither of which the
// host has (CreateWithStandardSystemClock aborts at sdk 0x4418b4; StandardSteadyClock::now and the network clock read shared memory that is never set up).
// ClockSnapshot is 0xd0 bytes, laid out as Ryujinx's ClockSnapshot.cs (the SDK's constructor sdk 0x4417e0 clears +0xab and +0xcd, inside that layout).
// Filled like the time service's GetClockSnapshot (Ryujinx IStaticServiceForPsc): both contexts are an offset in seconds over the steady clock, user time =
// steady time point + offset, calendars from ToCalendarTime above, the device's location name, automatic correction on, type = the byte the client passes.
// Steady clock = the guest clock in seconds (as CLOCK_MONOTONIC above), one clock source (ID 0). The network clock is the user clock (offline, as if synced).
#include <CoreFoundation/CoreFoundation.h>
typedef struct { int64_t timepoint; uint8_t source[16]; } SteadyClockTimePoint;
typedef struct { int64_t offset; SteadyClockTimePoint steady; } SystemClockContext;
typedef struct {
    SystemClockContext user_ctx, net_ctx;           // +0x00, +0x20
    int64_t user_time, net_time;                    // +0x40, +0x48
    CalendarTime user_cal, net_cal;                 // +0x50, +0x58
    CalendarAdditionalInfo user_ai, net_ai;         // +0x60, +0x78
    SteadyClockTimePoint steady;                    // +0x90
    char location[0x24];                            // +0xa8
    uint8_t auto_correct, type; uint16_t unknown;   // +0xcc
} ClockSnapshot;
_Static_assert(sizeof(CalendarTime) == 8 && sizeof(CalendarAdditionalInfo) == 0x18 && sizeof(ClockSnapshot) == 0xd0, "time layouts");
static int64_t steady_s(void) { return (int64_t)(guest_ns() / 1000000000ull); }
static int64_t user_now(void) { int64_t ft = fixed_time(); return (ft >= 0 ? ft : time(NULL)) + skew(); }
static void location_name(char *out, size_t n) {
    memset(out, 0, n);
    if (fixed_time() >= 0) { strlcpy(out, "UTC", n); return; }   // main.c pins TZ to UTC then
    CFTimeZoneRef tz = CFTimeZoneCopyDefault();
    if (!tz || !CFStringGetCString(CFTimeZoneGetName(tz), out, (CFIndex)n, kCFStringEncodingASCII)) strlcpy(out, "UTC", n);
    if (tz) CFRelease(tz);
}
static void fill_snapshot(ClockSnapshot *s, uint8_t type) {
    memset(s, 0, sizeof *s);
    int64_t st = steady_s(), now = user_now();
    s->steady.timepoint = st;
    s->user_ctx = (SystemClockContext){now - st, {st, {0}}}; s->net_ctx = s->user_ctx;
    s->user_time = s->net_time = now;
    h_time_ToCalendarTime(&s->user_cal, &s->user_ai, &now); s->net_cal = s->user_cal; s->net_ai = s->user_ai;
    location_name(s->location, sizeof s->location);
    s->auto_correct = 1; s->type = type;
}
// The real GetSystemClockContext asks the time service, which the host does not have, and fails. Unity's IL2CPP clock (manosaba main+0x394ba0 / +0x394c40,
// behind DateTime.UtcNow / Now) only marks itself initialized once this succeeds and returns 0 ticks until then, so DateTime.Now was 0001-01-01 and every
// save slot showed that date (Naninovel GameStateMap.SaveDateTime). That clock caches GetCurrentTime and extrapolates with GetSystemTick, re-reading only when
// the context changes (SDK docs: the context changes when the clock is corrected, adjusted or reset).
// Context in Horizon's layout (plan item 158, after nxnative a35e964, MIT; it was wall-clock minus guest-clock nanoseconds in the first 8 bytes): the
// offset in seconds and the steady clock time point it was made at with its source (user time = offset + a time point of that source), as the
// snapshot below fills its user context. The game compares all 0x20 bytes, so it is made again only when the host clock moves against the guest
// clock by more than 2 s (the guest clock stopped while wall time went on: iOS background, the system keyboard; or the wall clock set); under
// DM_FIXED_TIME it is made once. Checked by host/tests/t_time.c
HLE_FORCE_OPT("_ZN2nn4time23StandardUserSystemClock21GetSystemClockContextEPNS0_18SystemClockContextE", "user clock context: the host's user clock over its steady clock",
              Result, h_time_GetSystemClockContext, uint8_t *out) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; static SystemClockContext ctx; static int64_t drift; static int made;
    int64_t st = steady_s(), now = user_now(), d = now - st;
    pthread_mutex_lock(&mu);
#ifndef DM_TEST_CTX_OLD   // negative control of t_time only: the old layout
    if (!made || (fixed_time() < 0 && llabs(d - drift) > 2)) {
        if (made) LOG("time: the host clock moved %+lld s against the guest clock, a new user clock context", (long long)(d - drift));
        ctx = (SystemClockContext){d, {st, {0}}}; drift = d; made = 1;
    }
    memcpy(out, &ctx, sizeof ctx);
#else
    (void)d; (void)drift; (void)made; (void)ctx; static int64_t reported = INT64_MIN; int64_t v = 0;
    if (fixed_time() < 0) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); int64_t off = (int64_t)t.tv_sec * 1000000000ll + t.tv_nsec - (int64_t)guest_ns();
        if (reported == INT64_MIN || llabs(off - reported) > 2000000000ll) reported = off; v = reported; }
    memset(out, 0, 0x20); memcpy(out, &v, 8);
#endif
    pthread_mutex_unlock(&mu);
    return RES_OK;
}
HLE_FORCE_OPT("_ZN2nn4time13ClockSnapshot29CreateWithStandardSystemClockEPS1_", "clock snapshot from the host clocks (time service GetClockSnapshot)", Result, h_time_SnapStd, ClockSnapshot *s) {
    fill_snapshot(s, 1); return RES_OK;   // type 1: what the SDK passes the service (sdk 0x441890)
}
HLE_FORCE_OPT("_ZN2nn4time19StandardSteadyClock3nowEv", "steady clock time point (seconds): the guest clock", int64_t, h_time_SteadyNow, void) { return steady_s(); }
HLE_FORCE_OPT("_ZN2nn4time26StandardNetworkSystemClock14GetCurrentTimeEPNS0_9PosixTimeE", "network clock → the user clock (offline, as if synced)", Result, h_time_NetNow, int64_t *out) {
    *out = user_now(); return RES_OK;
}
// TimeSpan in nanoseconds. Span between: from the steady time points when they come from the same clock source, else from the network times when both are set
// (Ryujinx CalculateSpanBetween); the host has one source. Difference by user: the user contexts' offsets, 0 when the sources differ or both have automatic
// correction on (Ryujinx CalculateStandardUserSystemClockDifferenceByUser)
// It returns bool, true when the span was computed (the SDK reference; plan item 288, after seedds/nxnative 971d336, MIT): a Result's 0 read as false
// told the game every span had failed. -DDM_TEST_SPAN_RESULT: the old Result answers (0 on success, TimeNotFound otherwise)
#ifdef DM_TEST_SPAN_RESULT
#define SPAN_OK 0
#define SPAN_FAILED (116u | 200u << 9)   // time module 116, description 200 (Ryujinx Time ResultCode.TimeNotFound)
#else
#define SPAN_OK true
#define SPAN_FAILED false
#endif
HLE_FORCE_OPT("_ZN2nn4time20CalculateSpanBetweenEPNS_8TimeSpanERKNS0_13ClockSnapshotES5_", "span between two snapshots", bool, h_time_SpanBetween, int64_t *out, const ClockSnapshot *a, const ClockSnapshot *b) {
    if (!memcmp(a->steady.source, b->steady.source, 16)) { *out = (b->steady.timepoint - a->steady.timepoint) * 1000000000ll; return SPAN_OK; }
    if (a->net_time && b->net_time) { *out = (b->net_time - a->net_time) * 1000000000ll; return SPAN_OK; }
    return SPAN_FAILED;
}
HLE_FORCE_OPT("_ZN2nn4time48CalculateStandardUserSystemClockDifferenceByUserERKNS0_13ClockSnapshotES3_", "user clock difference between two snapshots", int64_t, h_time_UserDiff, const ClockSnapshot *a, const ClockSnapshot *b) {
    if (memcmp(a->user_ctx.steady.source, b->user_ctx.steady.source, 16) || (a->auto_correct && b->auto_correct)) return 0;
    return (b->user_ctx.offset - a->user_ctx.offset) * 1000000000ll;
}
HLE_FORCE_OPT("_ZN2nn4time8FinalizeEv", "the time service is taken over: nothing to close", Result, h_time_Finalize, void) { return RES_OK; }

// Named time-zone rules (plan item 64k; acnh-mobile converts by name with CFTimeZone, MIT, written anew): LoadTimeZoneRule keeps the location name in
// the caller's TimeZoneRule (a large opaque block on the console; the first 48 bytes are used here), and the rule overloads convert by that zone's
// offsets: ToCalendarTime as the device one; ToPosixTime tries the offsets a day before and a day after the local time and keeps each instant that
// converts back, so a local time repeated by a change gives both (ascending), a skipped one or a date that does not exist none, with success (the SDK
// reference). An unknown name breaks the SDK's precondition: reported red, the rule is UTC. GetDeviceLocationName: the host's zone (UTC under
// DM_FIXED_TIME, as the clock snapshots)
typedef struct { char magic[8]; char name[0x24]; } tz_rule;
static CFTimeZoneRef rule_zone(const void *rule) {
    const tz_rule *r = rule; char nm[0x25] = {0};
    if (!memcmp(r->magic, "DMTZRULE", 8)) memcpy(nm, r->name, sizeof r->name);
    CFStringRef s = CFStringCreateWithCString(NULL, nm[0] ? nm : "UTC", kCFStringEncodingASCII);
    CFTimeZoneRef tz = s ? CFTimeZoneCreateWithName(NULL, s, true) : NULL;
    if (s) CFRelease(s);
    return tz ? tz : CFTimeZoneCreateWithTimeIntervalFromGMT(NULL, 0);
}
static int32_t zone_offset(CFTimeZoneRef tz, int64_t t) { return (int32_t)CFTimeZoneGetSecondsFromGMT(tz, (CFAbsoluteTime)t - kCFAbsoluteTimeIntervalSince1970); }
HLE_FORCE_OPT("_ZN2nn4time21GetDeviceLocationNameEPNS0_12LocationNameE", "the host's time zone name", void, h_time_DevLoc, char *out) { location_name(out, 0x24); }
HLE_FORCE_OPT("_ZN2nn4time16LoadTimeZoneRuleEPNS0_12TimeZoneRuleERKNS0_12LocationNameE", "a time zone rule by location name (host CFTimeZone)", Result, h_time_LoadRule, void *rule, const char *name) {
    tz_rule *r = rule; memset(r, 0, sizeof *r); memcpy(r->magic, "DMTZRULE", 8); strlcpy(r->name, name, sizeof r->name);
    CFStringRef s = CFStringCreateWithCString(NULL, r->name, kCFStringEncodingASCII); CFTimeZoneRef tz = s ? CFTimeZoneCreateWithName(NULL, s, true) : NULL;
    if (s) CFRelease(s);
    if (tz) CFRelease(tz); else { LOG("[red] LoadTimeZoneRule: location \"%s\" is not a time zone the host knows (the SDK's precondition): converted as UTC", r->name); strlcpy(r->name, "UTC", sizeof r->name); }
    return RES_OK;
}
HLE_FORCE_OPT("_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeERKNS0_12TimeZoneRuleE", "absolute → local time by a loaded rule",
              Result, h_time_ToCalRule, CalendarTime *ct, CalendarAdditionalInfo *ai, const int64_t *pt, const void *rule) {
    CFTimeZoneRef tz = rule_zone(rule);
#ifndef DM_TEST_TZRULE_DEVICE   // negative control of t_tzrule only: the rule ignored, the device zone used
    int32_t off = zone_offset(tz, *pt); time_t lt = (time_t)(*pt + off); struct tm tm; gmtime_r(&lt, &tm);
    CFAbsoluteTime at = (CFAbsoluteTime)*pt - kCFAbsoluteTimeIntervalSince1970;
    int dst = CFTimeZoneIsDaylightSavingTime(tz, at); CFStringRef ab = CFTimeZoneCopyAbbreviation(tz, at); char abbr[8] = "UTC";
    if (ab) { CFStringGetCString(ab, abbr, sizeof abbr, kCFStringEncodingASCII); CFRelease(ab); }
#else
    time_t t0 = (time_t)*pt; struct tm tm; localtime_r(&t0, &tm); int32_t off = (int32_t)tm.tm_gmtoff; int dst = tm.tm_isdst > 0; char abbr[8] = "UTC";
#endif
    CFRelease(tz);
    if (ct) *ct = (CalendarTime){(int16_t)(tm.tm_year + 1900), (int8_t)(tm.tm_mon + 1), (int8_t)tm.tm_mday, (int8_t)tm.tm_hour, (int8_t)tm.tm_min, (int8_t)tm.tm_sec, 0};
    if (ai) { memset(ai, 0, sizeof *ai); ai->wday = tm.tm_wday; ai->yday = tm.tm_yday; strlcpy(ai->tzname, abbr, sizeof ai->tzname); ai->dst = dst != 0; ai->offset = off; }
    return RES_OK;
}
HLE_FORCE_OPT("_ZN2nn4time11ToPosixTimeEPiPNS0_9PosixTimeEiRKNS0_12CalendarTimeERKNS0_12TimeZoneRuleE", "local → absolute times by a loaded rule (0-2 results)",
              Result, h_time_ToPosixRule, int32_t *count, int64_t *out, int32_t max, const CalendarTime *ct, const void *rule) {
    struct tm in = {.tm_year = ct->year - 1900, .tm_mon = ct->month - 1, .tm_mday = ct->day, .tm_hour = ct->hour, .tm_min = ct->minute, .tm_sec = ct->second};
    time_t local = timegm(&in); struct tm back; int n = 0; int64_t r[2];
    if (local != (time_t)-1 && gmtime_r(&local, &back) && back.tm_year == ct->year - 1900 && back.tm_mon == ct->month - 1 && back.tm_mday == ct->day &&
        back.tm_hour == ct->hour && back.tm_min == ct->minute && back.tm_sec == ct->second) {   // a date that does not exist (February 30) is normalized by timegm: none
        CFTimeZoneRef tz = rule_zone(rule);
        for (int k = -1; k <= 1; k += 2) {
            int64_t t = (int64_t)local - zone_offset(tz, (int64_t)local + k * 86400);
            if (t + zone_offset(tz, t) != (int64_t)local || (n && r[0] == t)) continue;
            r[n++] = t;
        }
        CFRelease(tz);
    }
    if (n == 2 && r[0] > r[1]) { int64_t x = r[0]; r[0] = r[1]; r[1] = x; }
    if (n > max) n = max;
    for (int i = 0; i < n; i++) out[i] = r[i];
    *count = n;
    return RES_OK;
}
