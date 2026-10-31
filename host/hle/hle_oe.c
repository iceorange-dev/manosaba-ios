// nn::oe (application operating environment): on the console the state comes from the am / oe services; the host answers with its own state.
// oe::Initialize is in hle_sdkinit.c.
// Environment: DM_OPMODE=0 handheld / 1 console (default 1; on iOS the profile's ios_mode); DM_LANG=language code (default: the game profile's language);
// DM_OE_SCRIPT="focus=3@10,focus=1@20,opmode=0@30" changes state at those seconds (counted from the game's first notification fetch), for testing without a window.
#include "hle.h"
#include "game.h"
#include <TargetConditionals.h>
#include <stdatomic.h>
#include <unistd.h>

// Operation mode: runtime state (the front end may change it), first DM_OPMODE when it is set; else on iOS handheld for a game whose profile says
// so (game.json "ios_mode": "handheld", plan item 62d: Tears of the Kingdom renders 1280x720 as the console in hand, not 1920x1080), else console
int dm_opmode_start(const char *env, int iphone, int ios_handheld) {
#ifndef DM_TEST_OPMODE_NO_IOS   // negative control of host/tests t_oe only: the profile's iOS mode not read
    if (!env && iphone && ios_handheld) return 0;
#endif
    return env ? atoi(env) : 1;
}
static _Atomic int g_opmode = -1;
int dm_opmode(void) {
    int m = g_opmode;
    if (m < 0) { int v = dm_opmode_start(getenv("DM_OPMODE"), TARGET_OS_IPHONE, dm_game.ios_handheld); int exp = -1; atomic_compare_exchange_strong(&g_opmode, &exp, v); m = g_opmode; }
    return m;
}

// ---------- Notification queue (AppletMessage): front end state changes, fetched by the game with TryPopNotificationMessage ----------
// 15 focus changed, 30 operation mode changed, 31 performance mode changed
enum { MSG_FOCUS = 15, MSG_OPMODE = 30, MSG_PERFMODE = 31 };
#define MQ 32
static pthread_mutex_t g_mq_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_mq_cond = PTHREAD_COND_INITIALIZER;   // PopNotificationMessage waits on it
static uint32_t g_mq[MQ]; static int g_mqh, g_mqn;
static _Atomic int g_ntf_opmode, g_ntf_perfmode;   // pushed only once the game enables them with Set*NotificationEnabled
void oe_msg_event_signal(void);
static void mq_push(uint32_t m) {
    pthread_mutex_lock(&g_mq_lock);
    // 15 / 30 / 31 mean "query the current state", they carry no value: while one of these kinds is still queued, another is not added (so any number
    // of focus changes cannot overflow the queue). Other messages are events and are queued every time
    if (m == MSG_FOCUS || m == MSG_OPMODE || m == MSG_PERFMODE)
        for (int i = 0; i < g_mqn; i++)
            if (g_mq[(g_mqh + i) % MQ] == m) { pthread_mutex_unlock(&g_mq_lock); return; }
    if (g_mqn == MQ) die("oe notification queue overflow (the game is not fetching?)");
    g_mq[(g_mqh + g_mqn++) % MQ] = m;
    pthread_cond_broadcast(&g_mq_cond);
#ifndef DM_TEST_OE_EVENT_UNLOCKED   // negative control of t_oe only: signalled after the lock, as before
    oe_msg_event_signal();   // under the queue lock with the clear (plan item 157)
    pthread_mutex_unlock(&g_mq_lock);
#else
    pthread_mutex_unlock(&g_mq_lock);
    oe_msg_event_signal();
#endif
}
void dm_set_opmode(int m) {   // front end: change the mode and push 30 / 31 as far as the game enabled them
    int old = dm_opmode(); if (old == m) return;
    g_opmode = m;
    LOG("oe operation mode %d -> %d", old, m);
    if (g_ntf_opmode) mq_push(MSG_OPMODE);
    if (g_ntf_perfmode) mq_push(MSG_PERFMODE);
}
// FocusState: 1 InFocus, 2 OutOfFocus, 3 Background. Set by the front end (iOS lifecycle, Mac window focus)
static _Atomic int g_focus = 1; static _Atomic uint64_t g_focus_in_ns;   // time of the latest entry into InFocus
int dm_focus_now(void) { return atomic_load(&g_focus); }
uint64_t dm_focus_in_since(void) { return atomic_load(&g_focus_in_ns); }   // the graphics layer counts display link stalls only while in focus (nvn_mtl.m)
void dm_focus_set(int st) {
    if (st < 1 || st > 3) die("dm_focus_set: invalid state %d", st);
    if (st == 1) atomic_store(&g_focus_in_ns, now_ns());
    if (atomic_exchange(&g_focus, st) == st) return;
    LOG("oe focus state -> %d", st);
    mq_push(MSG_FOCUS);
}

static void *oe_script(void *arg) {
    char *buf = arg; uint64_t t0 = now_ns();
    for (char *save, *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char key[16]; int v; double at;
        if (sscanf(tok, "%15[a-z]=%d@%lf", key, &v, &at) != 3) die("DM_OE_SCRIPT item \"%s\": expected key=value@seconds", tok);
        uint64_t due = t0 + (uint64_t)(at * 1e9), now = now_ns();
        if (due > now) usleep((useconds_t)((due - now) / 1000));
        if (!strcmp(key, "focus")) dm_focus_set(v);
        else if (!strcmp(key, "opmode")) dm_set_opmode(v);
        else die("DM_OE_SCRIPT: unknown key \"%s\" (focus / opmode)", key);
    }
    return NULL;
}
static void oe_script_start(void) {
    const char *e = getenv("DM_OE_SCRIPT"); if (!e) return;
    pthread_t th; if (pthread_create(&th, NULL, oe_script, strdup(e))) die("DM_OE_SCRIPT: cannot start its thread");
    pthread_detach(th);
}
static pthread_once_t g_oe_once = PTHREAD_ONCE_INIT;

// Screenshot / recording switches: the host has no system capture service
HLE_FORCE("_ZN2nn2oe15EnableRecordingEv", "no system recording", void, h_oe_EnableRec, void) {}
HLE_FORCE("_ZN2nn2oe16DisableRecordingEv", "no system recording", void, h_oe_DisableRec, void) {}
HLE_FORCE("_ZN2nn2oe16EnableScreenShotEv", "no system screenshots", void, h_oe_EnableShot, void) {}
HLE_FORCE("_ZN2nn2oe17DisableScreenShotEv", "no system screenshots", void, h_oe_DisableShot, void) {}

// OperationMode: 0 Handheld, 1 Console; PerformanceMode: 0 Normal, 1 Boost (follows the operation mode)
HLE_FORCE("_ZN2nn2oe16GetOperationModeEv", "operation mode from DM_OPMODE / the front end", int, h_oe_GetOpMode, void) { return dm_opmode(); }
HLE_FORCE("_ZN2nn2oe18GetPerformanceModeEv", "performance mode = operation mode", int, h_oe_GetPerfMode, void) { return dm_opmode(); }
HLE_FORCE("_ZN2nn2oe27SetPerformanceConfigurationENS0_15PerformanceModeEi", "CPU / GPU clock profile: the host does not change clocks", void, h_oe_SetPerfCfg, int mode, int cfg) {
    LOG("oe::SetPerformanceConfiguration mode=%d cfg=%#x (ignored)", mode, cfg);
}
HLE_FORCE("_ZN2nn2oe20GetCurrentFocusStateEv", "focus state from the front end", int, h_oe_GetFocus, void) { return g_focus; }
HLE_FORCE("_ZN2nn2oe20SetFocusHandlingModeENS0_17FocusHandlingModeE", "focus handling: focus notifications are always sent", void, h_oe_SetFocusMode, int m) {
    LOG("oe::SetFocusHandlingMode %d", m);
}
HLE_FORCE("_ZN2nn2oe25TryPopNotificationMessageEPj", "notification queue fed by front end state changes", unsigned, h_oe_TryPopMsg, uint32_t *msg) {
    pthread_once(&g_oe_once, oe_script_start);
    pthread_mutex_lock(&g_mq_lock);
    int ok = g_mqn > 0;
    if (ok) { *msg = g_mq[g_mqh]; g_mqh = (g_mqh + 1) % MQ; g_mqn--; }
    pthread_mutex_unlock(&g_mq_lock);
    if (ok) LOG("oe notification taken by the game: %u", *msg);
    void oe_msg_event_clear_if_empty(void); oe_msg_event_clear_if_empty();
    return ok;
}
// Blocking form (TotK main imports it): waits for the host queue. After nxnative's h_oe_PopMsg (seedds, MIT)
HLE_FORCE_OPT("_ZN2nn2oe22PopNotificationMessageEv", "notification queue, waiting until a message is pushed", uint32_t, h_oe_PopMsg, void) {
    uint32_t m = 0;
    for (;;) {
        pthread_mutex_lock(&g_mq_lock);
        while (!g_mqn) pthread_cond_wait(&g_mq_cond, &g_mq_lock);
        pthread_mutex_unlock(&g_mq_lock);
        if (h_oe_TryPopMsg(&m)) return m;   // another thread may have taken it in between: wait again
    }
}

// ---------- Notification message event (games that wait for messages instead of polling, e.g. Unity) ----------
// On the console the event stays signaled while messages are pending. Here: manual clear, signaled on every push, cleared by TryPop once the queue is empty.
// Both under the queue lock (plan item 157, after nxnative ee51071, MIT): cleared after unlocking, a push in between was signalled and then cleared,
// leaving a message queued with the event clear, and a game waiting on it never woke. Checked by host/tests/t_oe.c
static uint8_t g_msg_ev[0x80] __attribute__((aligned(16)));   // nn::os::SystemEvent handed to the game
static _Atomic int g_msg_ev_bound;
void oe_msg_event_signal(void) { if (atomic_load(&g_msg_ev_bound)) hle_signal_system_event(g_msg_ev); }
#ifdef DM_TEST_HOOKS
void (*g_oe_test_clear_gap)(void);   // t_oe: runs between the queue check and the clear
#endif
void oe_msg_event_clear_if_empty(void) {
    if (!atomic_load(&g_msg_ev_bound)) return;
    static void (*clear)(void *); if (!clear) clear = guest_sym("_ZN2nn2os16ClearSystemEventEPNS0_15SystemEventTypeE");
    pthread_mutex_lock(&g_mq_lock); int n = g_mqn;
#ifdef DM_TEST_OE_EVENT_UNLOCKED
    pthread_mutex_unlock(&g_mq_lock);
#endif
#ifdef DM_TEST_HOOKS
    if (g_oe_test_clear_gap) g_oe_test_clear_gap();
#endif
    if (!n) clear(g_msg_ev);   // TryPop runs on a guest thread
#ifndef DM_TEST_OE_EVENT_UNLOCKED
    pthread_mutex_unlock(&g_mq_lock);
#endif
}
HLE_FORCE_OPT("_ZN2nn2oe27GetNotificationMessageEventEv", "notification event: signaled while the queue is not empty", void *, h_oe_GetMsgEv, void) {
    pthread_once(&g_oe_once, oe_script_start);
    if (!atomic_load(&g_msg_ev_bound)) { hle_bind_system_event(g_msg_ev, 0); atomic_store(&g_msg_ev_bound, 1); }
    pthread_mutex_lock(&g_mq_lock); int n = g_mqn; pthread_mutex_unlock(&g_mq_lock);
    if (n) hle_signal_system_event(g_msg_ev);
    return g_msg_ev;
}
// The health and safety notice the system shows before a game: the host has none, so its "disappeared" event is signaled from the start
static uint8_t g_health_ev[0x80] __attribute__((aligned(16)));
HLE_FORCE_OPT("_ZN2nn2oe38GetHealthWarningDisappearedSystemEventEv", "no health warning: the event is signaled from the start", void *, h_oe_HealthEv, void) {
    static int once; if (!once++) { hle_bind_system_event(g_health_ev, 0); hle_signal_system_event(g_health_ev); }
    return g_health_ev;
}
HLE_FORCE_OPT("_ZN2nn2oe15SetCpuBoostModeENS0_12CpuBoostModeE", "the host does not change clocks", void, h_oe_CpuBoost, int mode) { (void)mode; }
HLE_FORCE_OPT("_ZN2nn2oe27SetPerformanceConfigurationEi", "the host does not change clocks", void, h_oe_SetPerfCfg1, int cfg) { (void)cfg; }
HLE_FORCE_OPT("_ZN2nn2oe17EnableCrashReportEv", "no system crash reports", void, h_oe_EnableCrash, void) {}
HLE_FORCE_OPT("_ZN2nn2oe18DisableCrashReportEv", "no system crash reports", void, h_oe_DisableCrash, void) {}
// The exit-request section: the game brackets what an exit must not cut (manosaba its saves); a quit waits while it is inside one (hle_lifecycle.c,
// after GirlNet 291a9cc, which counts the depth the same way). Unit test tests/t_quitwait.c
static _Atomic int g_exit_sec;
int dm_oe_exit_section(void) { return atomic_load(&g_exit_sec) > 0; }
HLE_FORCE_OPT("_ZN2nn2oe31EnterExitRequestHandlingSectionEv", "exit-request section: counted, a quit waits for it", void, h_oe_EnterExitSec, void) { atomic_fetch_add(&g_exit_sec, 1); }
HLE_FORCE_OPT("_ZN2nn2oe31LeaveExitRequestHandlingSectionEv", "exit-request section: counted, a quit waits for it", void, h_oe_LeaveExitSec, void) {
    int d = atomic_load(&g_exit_sec);
    while (d > 0 && !atomic_compare_exchange_weak(&g_exit_sec, &d, d - 1)) ;   // never below 0 (a Leave without an Enter)
}
HLE_FORCE_OPT("_ZN2nn2oe27GetDefaultDisplayResolutionEPiS1_", "default display resolution: handheld 1280x720 / console 1920x1080", void, h_oe_DefRes, int *w, int *h) {
    if (dm_opmode()) { *w = 1920; *h = 1080; } else { *w = 1280; *h = 720; }
}
HLE_FORCE_OPT("_ZN2nn2oe38GetDefaultDisplayResolutionChangeEventEPNS_2os11SystemEventE", "display resolution change event (never signaled)", void, h_oe_DefResEv, void *ev) {
    hle_bind_system_event(ev, 0);
}
// Multi-program applications (am IApplicationFunctions 123, an s32): -1 = no program of this application launched this one (switchbrew
// "Applet Manager services"). The SDK reads it through the am application proxy, which the host does not have (TotK main calls it in nnMain)
HLE_FORCE_OPT("_ZN2nn2oe23GetPreviousProgramIndexEv", "no previous program: -1", int, h_oe_PrevProgram, void) { return -1; }
// Launch parameter (present only when another program launched this one with one; returns bool): the host never passes one. From nxnative
HLE_FORCE_OPT("_ZN2nn2oe21TryPopLaunchParameterEPmPvm", "no launch parameter", unsigned, h_oe_PopLaunchParam, size_t *out, void *buf, size_t n) {
    (void)buf; (void)n; if (out) *out = 0; return 0;
}
HLE_FORCE_OPT("_ZN2nn2oe17FinishStartupLogoEv", "the host shows no startup logo", void, h_oe_FinishLogo, void) {}
// The host does not dim the screen. A game may hold its frame rate meanwhile (plan item 130): the game hook dm_game_inactivity_fps gets the call and the
// guest's return address and gives the rate (0 = none): Tears of the Kingdom extends the time for every cutscene and holds only its pre-rendered
// movies at 30 fps (games/totk/movie_fps.c). DM_INACTIVITY_FPS replaces a rate the hook gives (0 = no hold)
void nvn_fps_hold(int fps);   // gfx/nvn/hle_nvn.c
static void oe_inactivity(unsigned on, uint64_t ra) {   // a bool: only the low 8 bits count (below)
#ifdef DM_TEST_INACTIVITY_NOOP   // negative control of t_oe only: the old stub, which ignored it
    (void)on; (void)ra; return;
#endif
    on &= 0xff;
    int fps = dm_game_inactivity_fps(on != 0, ra); const char *e = getenv("DM_INACTIVITY_FPS");
    if (fps > 0 && e) fps = atoi(e);
    module_t *m = module_of((const void *)ra);
    LOG("oe::SetUserInactivityDetectionTimeExtended(%d) from %s+%#llx%s", on != 0, m ? m->name : "?", m ? (unsigned long long)(ra - (uint64_t)m->base) : (unsigned long long)ra,
        fps > 0 ? ": frame rate held" : "");
    nvn_fps_hold(fps > 0 ? fps : 0);
}
HLE_FORCE_OPT("_ZN2nn2oe38SetUserInactivityDetectionTimeExtendedEb", "the host does not dim the screen; the game hook may hold the frame rate meanwhile", void, h_oe_Inactivity, unsigned on) { oe_inactivity(on, DM_GUEST_RA()); }
HLE_FORCE_OPT("_ZN2nn2oe44SetUserInactivityDetectionTimeExtendedUnsafeEb", "the host does not dim the screen; the game hook may hold the frame rate meanwhile", void, h_oe_InactivityU, unsigned on) { oe_inactivity(on, DM_GUEST_RA()); }
HLE_FORCE_OPT("_ZN2nn2oe24GetExpectedVolumeBalanceEPfS1_", "system / application volume balance: always 1:1", void, h_oe_GetVolBal, float *sys, float *app) { *sys = 1.0f; *app = 1.0f; }
HLE_FORCE_OPT("_ZN2nn2oe24SetExpectedVolumeBalanceEff", "volume balance: ignored", void, h_oe_SetVolBal, float sys, float app) { (void)sys; (void)app; }
// Applet resource user ID (the number am gives this process): the SDK's own getter returns 0 without am, and SDK 20's OpenAudioRenderer aborts on 0
// (nxnative, The Legend of Zelda: Breath of the Wild 1.9.0, same sdk module as TotK). A fixed non-zero ID, used only as an identifier
HLE_FORCE_OPT("_ZN2nn6applet23GetAppletResourceUserIdEv", "no am: a fixed non-zero ID", uint64_t, h_applet_Aruid, void) { return 0x80; }
HLE_FORCE_OPT("_ZN2nn2oe25GetProgramTotalActiveTimeEv", "total active time (nn::TimeSpan, ns) from the guest clock", int64_t, h_oe_ActiveTime, void) { return (int64_t)guest_ns(); }
// bool arguments: Switch callers do not extend them, only the low 8 bits count
HLE_FORCE("_ZN2nn2oe28SetResumeNotificationEnabledEb", "resume is expressed by the focus notification", void, h_oe_SetResumeNtf, unsigned on) { (void)on; }
HLE_FORCE("_ZN2nn2oe42SetOperationModeChangedNotificationEnabledEb", "operation mode notification switch", void, h_oe_SetOpModeNtf, unsigned on) {
    g_ntf_opmode = on & 0xff; LOG("oe::SetOperationModeChangedNotificationEnabled %d", g_ntf_opmode);
}
HLE_FORCE("_ZN2nn2oe44SetPerformanceModeChangedNotificationEnabledEb", "performance mode notification switch", void, h_oe_SetPerfModeNtf, unsigned on) {
    g_ntf_perfmode = on & 0xff; LOG("oe::SetPerformanceModeChangedNotificationEnabled %d", g_ntf_perfmode);
}

// nn::settings::LanguageCode = struct { char string[8]; }, returned by value in x0
HLE_FORCE("_ZN2nn2oe18GetDesiredLanguageEv", "system language from DM_LANG / the game profile", uint64_t, h_oe_GetLang, void) {
    const char *e = getenv("DM_LANG");
    char code[8] = {0};
    strncpy(code, e ? e : dm_game.language, sizeof code - 1);
    uint64_t v; memcpy(&v, code, 8);
    return v;
}
// nn::util::Uuid: a fixed value (it only tells devices apart, and networking is off)
HLE_FORCE("_ZN2nn2oe17GetPseudoDeviceIdEPNS_4util4UuidE", "pseudo device id: a fixed value", void, h_oe_GetDevId, uint8_t *uuid) {
    static const uint8_t k[16] = {0x78, 0x62, 0x6c, 0x00, 0x53, 0x57, 0x4e, 0x41, 0x54, 0x49, 0x56, 0x45, 0x31, 0x30, 0x30, 0x31};
    memcpy(uuid, k, 16);
}
