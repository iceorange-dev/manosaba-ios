// android/diag_android.c (plan item 301f): each of the NDK's thermal statuses → Apple's thermal state (none nominal; light and moderate fair; severe
// serious, where the frame rate is held at 30; critical, emergency and shutdown critical; an error no reading), and a reading on the phone (its
// thermal service answers a shell process too): one of the four states. -DDM_TEST_THERMAL_RAW (the status as it is): 5 fail.
#include <android/thermal.h>
#include <stdio.h>

int dm_and_thermal_level(int status); int dm_thermal_now(void);
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)

int main(void) {
    static const struct { int status, want; const char *name; } t[] = {{ATHERMAL_STATUS_ERROR, -1, "error"}, {ATHERMAL_STATUS_NONE, 0, "none"},
        {ATHERMAL_STATUS_LIGHT, 1, "light"}, {ATHERMAL_STATUS_MODERATE, 1, "moderate"}, {ATHERMAL_STATUS_SEVERE, 2, "severe"},
        {ATHERMAL_STATUS_CRITICAL, 3, "critical"}, {ATHERMAL_STATUS_EMERGENCY, 3, "emergency"}, {ATHERMAL_STATUS_SHUTDOWN, 3, "shutdown"}};
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) { int got = dm_and_thermal_level(t[i].status); CHECK(got == t[i].want, "status %s (%d) → state %d (%d)", t[i].name, t[i].status, t[i].want, got); }
    int now = dm_thermal_now(); CHECK(now >= 0 && now <= 3, "the phone's thermal state now: %d", now);
    printf("t_thermal: %d checks, %d failed\n", n, fail); return fail != 0;
}
