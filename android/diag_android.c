// Diagnostics of the device for the Android host (plan item 301c). dm_thermal_now answers Apple's thermal states (0 nominal ... 3 critical: diag.c's
// performance lines, the timing summaries, and the hold of 30 fps when hot, front_android.c) from Android's thermal status (AThermal, API 30; plan
// item 301f), kept up to date by its listener. The NDK's words for each status decide the state: none "not under throttling" → nominal; light and
// moderate, throttling "where UX is not impacted" / "not largely impacted" → fair; severe "where UX is largely impacted" → serious, where iOS
// reduces performance (nvn_fps_policy holds 30 from there, as on the iPhone); critical, emergency and shutdown → critical; an error -1 (no reading).
// DM_THERMAL_STATUS=<status> takes that status instead of the system's (tests: the frame rate held when hot)
#include "dm.h"
#include <android/thermal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

int dm_and_thermal_level(int status) {   // Android's AThermalStatus → Apple's thermal state
#ifdef DM_TEST_THERMAL_RAW   // negative control of tests/t_thermal.c only: the status as it is
    return status;
#endif
    if (status < ATHERMAL_STATUS_NONE) return -1;
    return status == ATHERMAL_STATUS_NONE ? 0 : status <= ATHERMAL_STATUS_MODERATE ? 1 : status == ATHERMAL_STATUS_SEVERE ? 2 : 3;
}
static _Atomic int g_status = ATHERMAL_STATUS_ERROR;
static void (*_Atomic g_watch)(void);
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static void on_status(void *data, AThermalStatus s) {   // (a binder thread)
    (void)data; int was = atomic_exchange(&g_status, (int)s);
    if (was != (int)s) { LOG("thermal status %d → %d (state %d)", was, (int)s, dm_and_thermal_level((int)s)); void (*w)(void) = atomic_load(&g_watch); if (w) w(); }
}
static void init(void) {
    const char *e = getenv("DM_THERMAL_STATUS");
    if (e && *e) { atomic_store(&g_status, atoi(e)); LOG("thermal status %d (DM_THERMAL_STATUS)", atoi(e)); return; }
    AThermalManager *m = AThermal_acquireManager(); if (!m) { LOG("[red] no thermal manager: the thermal state is not known"); return; }
    atomic_store(&g_status, (int)AThermal_getCurrentThermalStatus(m));
    if (AThermal_registerThermalStatusListener(m, on_status, NULL)) LOG("[red] the thermal status listener was refused: the state read at the start is kept");
    LOG("thermal status %d (state %d), followed", atomic_load(&g_status), dm_and_thermal_level(atomic_load(&g_status)));
}
int dm_thermal_now(void) { pthread_once(&g_once, init); return dm_and_thermal_level(atomic_load(&g_status)); }
void dm_thermal_watch(void (*fn)(void)) { atomic_store(&g_watch, fn); pthread_once(&g_once, init); }   // fn on each change (any thread)
