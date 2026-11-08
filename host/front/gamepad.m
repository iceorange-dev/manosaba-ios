// Game controllers (GameController framework, shared by the Mac and iOS front ends) → the Switch controller input (dm_pad_set, dm_input.h).
// Face buttons: the system's buttonA / B / X / Y go to A / B / X / Y as they are (gamepad_map.h has why: the system reports controllers with
// printed letters by the letter, so on a Switch Pro or Joy-Con pair right A, bottom B, top X, left Y as on the Switch).
// L/R shoulders, ZL/ZR triggers, stick clicks, D-pad; menu button → +, options button → − (controllers without one have no −; the Home button is not
// used: the system may take it first). Sticks are scaled to ±32767, up positive on both sides; the game applies its own dead zone.
// Several controllers: each keeps its own state; buttons are ORed, each stick comes from the controller that pushes it furthest. Disconnecting one
// drops only its share. Focus loss (dm_gamepad_reset): all state is cleared, since a button released in the background never reports its release.
// Haptics (plan item 67c): the game's vibration (hle_motion.c, dm_vibration_take) every 15 ms on a queue of its own: each controller with haptics
// gets a CoreHaptics engine per handle when it has a left and a right one (else one, each band from the side that has it larger). GameController's
// engines take plain pattern players only (an advanced one fails: atone/nxnative f6dba3d on a Pro Controller, MIT; smbw-mobile / MacNativeGames on
// macOS 26), and so, as SDL and atone do, a change of the value (intensity in 1/50, sharpness in 1/10: haptics_map.h) plays a new endless continuous
// event at the new level and stops the previous one; 0 stops it. The engines play haptics only and do not shut down on their own; a reset starts
// them again. iOS without a controller: the phone's own haptics as single pulses (haptics_map.h's limits), none at a serious thermal state or with
// DM_PHONE_HAPTICS=0. Out of focus the game is silent (hle_motion.c). DM_PAD_LOG=1 logs each engine's changes.
#import <GameController/GameController.h>
#import <CoreHaptics/CoreHaptics.h>
#include "dm_input.h"
#include "gamepad_map.h"
#include "haptics_map.h"
#include "hle.h"

enum { NSLOT = 6 };   // slots 0 and 1 are the virtual controllers of dm_gamepad_inject, the rest are real controllers
static struct { const void *who; uint64_t b; int32_t ax[4]; } g_s[NSLOT];
static int slot_of(const void *who, int make) {
    intptr_t v = (intptr_t)who; int i0 = 2, i1 = NSLOT;
    if (v == 1 || v == 2) i0 = (int)v - 1, i1 = i0 + 1;
    int fr = -1;
    for (int i = i0; i < i1; i++) { if (g_s[i].who == who) return i; if (fr < 0 && !g_s[i].who) fr = i; }
    if (make && fr >= 0) { memset(&g_s[fr], 0, sizeof g_s[fr]); g_s[fr].who = who; return fr; }
    return -1;
}

static int32_t axis(float v) { if (v > 1) v = 1; if (v < -1) v = -1; return (int32_t)lroundf(v * DM_STICK_MAX); }

// Merge every controller and hand the result to the host; recomputed whenever one changes
static void apply(void) {
    uint64_t b = 0; int32_t ax[4] = {0}; double best[2] = {0};
    for (int i = 0; i < NSLOT; i++) {
        if (!g_s[i].who) continue;
        b |= g_s[i].b;
        for (int k = 0; k < 2; k++) {
            double m = hypot(g_s[i].ax[2 * k], g_s[i].ax[2 * k + 1]);
            if (m > best[k]) { best[k] = m; ax[2 * k] = g_s[i].ax[2 * k]; ax[2 * k + 1] = g_s[i].ax[2 * k + 1]; }
        }
    }
    dm_pad_set(b, ax[0], ax[1], ax[2], ax[3]);
}
static void put(const void *who, uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) {
    int i = slot_of(who, 1); if (i < 0) return;
    g_s[i].b = b; g_s[i].ax[0] = lx; g_s[i].ax[1] = ly; g_s[i].ax[2] = rx; g_s[i].ax[3] = ry; apply();
}
static void drop(const void *who) { int i = slot_of(who, 0); if (i < 0) return; memset(&g_s[i], 0, sizeof g_s[i]); apply(); }

void dm_gamepad_reset(void) {
    int any = 0;
    for (int i = 0; i < NSLOT; i++) if (g_s[i].who) {
        any |= g_s[i].b != 0 || g_s[i].ax[0] || g_s[i].ax[1] || g_s[i].ax[2] || g_s[i].ax[3];
        g_s[i].b = 0; memset(g_s[i].ax, 0, sizeof g_s[i].ax);
    }
    if (any) { LOG("gamepad: focus lost, buttons and sticks cleared"); apply(); }
}
void dm_gamepad_inject(int n, uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) { put((const void *)(intptr_t)(n == 2 ? 2 : 1), b, lx, ly, rx, ry); }
void dm_gamepad_unplug(int n) { LOG("gamepad: virtual controller %d unplugged", n == 2 ? 2 : 1); drop((const void *)(intptr_t)(n == 2 ? 2 : 1)); }

// ---------- Haptics ----------
enum { NHAP = 4 };
typedef struct { __weak GCController *c; const void *who; int n, pro; __strong CHHapticEngine *eng[2]; __strong id<CHHapticPatternPlayer> pl[2]; int dead[2], qi[2], qs[2], started[2]; } hap_t;
void (*dm_pad_hd_sink)(const float v[2][4]);   // a Switch Pro Controller's HD rumble over its HID device (procon_mac.m) while it is open
static hap_t g_hap[NHAP];
static dispatch_queue_t g_hq; static dispatch_source_t g_htimer;
#if TARGET_OS_IPHONE
static __strong CHHapticEngine *g_phone; static dm_pulse_lim g_plim; static int g_phone_off = -1; static dm_phone_eng g_peng;
int dm_focus_now(void);   // hle_oe.c: 1 in focus
#endif
static int g_hlog = -1;
static int hap_start(hap_t *h, int k) {   // the engine running (once; again after a stop or a reset)
    if (h->started[k]) return 1;
    if (h->dead[k] || !h->eng[k]) return 0;
    NSError *err = nil;
    if (![h->eng[k] startAndReturnError:&err]) { h->dead[k] = 1; LOG("[red] haptics: engine %d did not start (%s): this controller stays still", k, err.localizedDescription.UTF8String ?: "?"); return 0; }
    h->started[k] = 1; return 1;
}
static void hap_set(hap_t *h, int k, const float v[4]) {
    float in, sh; dm_haptic_level(v, &in, &sh);
    int qi = (int)lrintf(in * 50), qs = (int)lrintf(sh * 10);
    if (qi == h->qi[k] && (qi == 0 || qs == h->qs[k])) return;
    h->qi[k] = qi; h->qs[k] = qs;
    NSError *err = nil; id<CHHapticPatternPlayer> pl = nil;
    if (qi > 0 && hap_start(h, k)) {
        CHHapticEvent *e = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous parameters:@[
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:qi / 50.f],
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness value:qs / 10.f]] relativeTime:0 duration:GCHapticDurationInfinite];
        CHHapticPattern *p = [[CHHapticPattern alloc] initWithEvents:@[e] parameters:@[] error:&err];
        pl = p ? [h->eng[k] createPlayerWithPattern:p error:&err] : nil;
        if (!pl || ![pl startAtTime:CHHapticTimeImmediate error:&err]) { static int nf; if (nf++ < 3) LOG("[red] haptics: engine %d did not play (%s)", k, err.localizedDescription.UTF8String ?: "?"); pl = nil; }
    }
    [h->pl[k] stopAtTime:CHHapticTimeImmediate error:nil];
    h->pl[k] = pl;
    if (g_hlog > 0) LOG("haptics: engine %d intensity %.2f sharpness %.1f%s", k, qi / 50.f, qs / 10.f, qi > 0 && !pl ? " (failed)" : "");
}
static void hap_phone(float peak, uint64_t now) {
#if TARGET_OS_IPHONE
    if (g_phone_off < 0) {
        const char *e = getenv("DM_PHONE_HAPTICS"); g_phone_off = 1;
        if (e && !strcmp(e, "0")) LOG("haptics: the phone's off (DM_PHONE_HAPTICS=0)");
        else if (!CHHapticEngine.capabilitiesForHardware.supportsHaptics) LOG("haptics: this device has no haptics");
        else { g_phone_off = 0; LOG("haptics: the phone's own, as pulses (no controller with haptics); its engine starts at the first pulse in focus"); }
    }
    dm_pulse_note(&g_plim, peak, now);
    float p = dm_pulse_take(&g_plim, now);
    if (!(p > 0) || g_phone_off || NSProcessInfo.processInfo.thermalState >= NSProcessInfoThermalStateSerious) return;
    if (!g_phone) {   // made here, in focus (haptics_map.h dm_phone_engine_due): iOS stops it in the background and refuses a start there
        if (!dm_phone_engine_due(&g_peng, p, dm_focus_now(), now)) return;
        // haptics only, as the controllers' engines (plan item 314e): the phone's engine plays no audio events, and S2Arm 4c0bec5 measured a
        // 2,049 ms stall of the game's sound when CoreHaptics started an engine without it (none seen in ours: no pulse in the runs looked at)
        NSError *err = nil; CHHapticEngine *eng = [[CHHapticEngine alloc] initAndReturnError:&err]; eng.playsHapticsOnly = YES;
        if (!eng || ![eng startAndReturnError:&err]) {
            dm_phone_engine_failed(&g_peng, now);
            if (g_peng.fails <= 3) LOG("[red] haptics: the phone's engine did not start in focus (%s): tried again in %.0f s", err.localizedDescription.UTF8String ?: "?", (g_peng.retry_at - now) / 1e9);
            return;
        }
        __weak CHHapticEngine *we = eng;
        void (^lost)(const char *) = ^(const char *why) { dispatch_async(g_hq, ^{ if (g_phone != we) return; g_phone = nil; if (g_hlog > 0) LOG("haptics: the phone's engine %s", why); }); };
        eng.stoppedHandler = ^(CHHapticEngineStoppedReason r) { (void)r; lost("stopped: made again at the next pulse in focus"); };
        eng.resetHandler = ^{ lost("reset: made again at the next pulse in focus"); };
        g_phone = eng; dm_phone_engine_started(&g_peng);
        if (g_hlog > 0) LOG("haptics: the phone's engine started");
    }
    CHHapticEvent *ev = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticTransient parameters:@[
        [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:dm_haptic_gain(p)],
        [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness value:0.4f]] relativeTime:0];
    NSError *err = nil; CHHapticPattern *pat = [[CHHapticPattern alloc] initWithEvents:@[ev] parameters:@[] error:&err];
    id<CHHapticPatternPlayer> pl = pat ? [g_phone createPlayerWithPattern:pat error:&err] : nil;
    [pl startAtTime:CHHapticTimeImmediate error:&err];
#else
    (void)peak; (void)now;
#endif
}
static void hap_tick(void) {
    float v[2][4]; dm_vibration_take(v); uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW); int any_pad = 0;
    void (*hd)(const float v[2][4]) = dm_pad_hd_sink;
    if (hd) { hd(v); any_pad = 1; }
    for (int i = 0; i < NHAP; i++) {
        hap_t *h = &g_hap[i]; if (!h->who) continue;
        if (!h->c) { *h = (hap_t){0}; continue; }   // gone (ARC releases its engines)
        any_pad = 1;
        if (h->pro && hd) continue;   // its HD rumble goes out over its HID device
        if (h->n == 2) { hap_set(h, 0, v[0]); hap_set(h, 1, v[1]); }
        else { int lo = v[1][0] > v[0][0], hi = v[1][2] > v[0][2];   // each band from the side that has it larger
               float m[4] = {v[lo][0], v[lo][1], v[hi][2], v[hi][3]}; hap_set(h, 0, m); }
    }
    if (!any_pad) { float pk = fmaxf(fmaxf(v[0][0], v[0][2]), fmaxf(v[1][0], v[1][2])); hap_phone(pk, now); }
}
static void hap_start_timer(void) {   // on hq
    if (g_htimer) return;
    g_htimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, g_hq);
    dispatch_source_set_timer(g_htimer, dispatch_time(DISPATCH_TIME_NOW, 0), 15 * NSEC_PER_MSEC, 2 * NSEC_PER_MSEC);
    dispatch_source_set_event_handler(g_htimer, ^{ hap_tick(); });
    dispatch_resume(g_htimer);
}
static void hap_add(GCController *c) {   // on hq
    if (@available(macOS 11.0, iOS 14.0, *)) {
        GCDeviceHaptics *hp = c.haptics; if (!hp) return;
        int slot = -1; for (int i = 0; i < NHAP; i++) { if (g_hap[i].who == (__bridge const void *)c) return; if (slot < 0 && !g_hap[i].who) slot = i; }
        if (slot < 0) return;
        hap_t *h = &g_hap[slot]; *h = (hap_t){0}; h->c = c; h->who = (__bridge const void *)c;
        h->pro = c.productCategory && [c.productCategory rangeOfString:@"pro controller" options:NSCaseInsensitiveSearch].location != NSNotFound;   // as macOS names a Switch Pro Controller (atone)
        NSSet<GCHapticsLocality> *loc = hp.supportedLocalities;
        if ([loc containsObject:GCHapticsLocalityLeftHandle] && [loc containsObject:GCHapticsLocalityRightHandle]) {
            h->eng[0] = [hp createEngineWithLocality:GCHapticsLocalityLeftHandle]; h->eng[1] = [hp createEngineWithLocality:GCHapticsLocalityRightHandle]; h->n = 2;
        } else { h->eng[0] = [hp createEngineWithLocality:GCHapticsLocalityDefault]; h->n = 1; }
        for (int k = 0; k < h->n; k++) if (h->eng[k]) {
            __weak CHHapticEngine *we = h->eng[k]; int kk = k; hap_t *hh = h;
            h->eng[k].playsHapticsOnly = YES; h->eng[k].autoShutdownEnabled = NO;
            void (^lost)(const char *) = ^(const char *why) { dispatch_async(g_hq, ^{ if (hh->eng[kk] != we) return;
                hh->started[kk] = 0; hh->pl[kk] = nil; hh->qi[kk] = hh->qs[kk] = 0; if (g_hlog > 0) LOG("haptics: engine %d %s", kk, why); }); };
            h->eng[k].stoppedHandler = ^(CHHapticEngineStoppedReason r) { (void)r; lost("stopped"); };
            h->eng[k].resetHandler = ^{ lost("reset: started again at the next value"); };
        }
        LOG("haptics: %s, %s", c.vendorName.UTF8String ?: "?", h->n == 2 ? "left and right handles" : "one engine (the two sides' larger value)");
        hap_start_timer();
    }
}
void dm_gamepad_haptics_on(void) { dispatch_async(g_hq, ^{ hap_start_timer(); }); }   // the tick runs (procon_mac.m: HD rumble without a GameController engine)
static void hap_init(void) {
    g_hq = dispatch_queue_create("dm.haptics", DISPATCH_QUEUE_SERIAL);
    const char *e = getenv("DM_PAD_LOG"); g_hlog = e && strcmp(e, "0");
#if TARGET_OS_IPHONE
    dispatch_async(g_hq, ^{ hap_start_timer(); });   // the phone's pulses while no controller has haptics
#endif
}

__attribute__((weak)) void dm_front_controller_key(void) {}
static void publish(GCController *c, GCExtendedGamepad *g, int by_position) {
    uint64_t b = dm_gamepad_face_by(g.buttonA.pressed, g.buttonB.pressed, g.buttonX.pressed, g.buttonY.pressed, by_position);
    if (g.leftShoulder.pressed) b |= DM_BTN_L; if (g.rightShoulder.pressed) b |= DM_BTN_R;
    if (g.leftTrigger.pressed) b |= DM_BTN_ZL; if (g.rightTrigger.pressed) b |= DM_BTN_ZR;
    if (g.leftThumbstickButton.pressed) b |= DM_BTN_LS; if (g.rightThumbstickButton.pressed) b |= DM_BTN_RS;
    if (g.buttonMenu.pressed) b |= DM_BTN_PLUS; if (g.buttonOptions.pressed) b |= DM_BTN_MINUS;
    if (g.dpad.left.pressed) b |= DM_BTN_LEFT; if (g.dpad.up.pressed) b |= DM_BTN_UP;
    if (g.dpad.right.pressed) b |= DM_BTN_RIGHT; if (g.dpad.down.pressed) b |= DM_BTN_DOWN;
    { int i = slot_of((__bridge const void *)c, 0); uint64_t prev = i >= 0 ? g_s[i].b : 0;   // a new key, or a stick newly past half way (plan item 171)
      int far = hypotf(g.leftThumbstick.xAxis.value, g.leftThumbstick.yAxis.value) > 0.5f || hypotf(g.rightThumbstick.xAxis.value, g.rightThumbstick.yAxis.value) > 0.5f;
      int was = i >= 0 && (hypot(g_s[i].ax[0], g_s[i].ax[1]) > 0.5 * DM_STICK_MAX || hypot(g_s[i].ax[2], g_s[i].ax[3]) > 0.5 * DM_STICK_MAX);
      if ((b & ~prev) || (far && !was)) dm_front_controller_key(); }
    put((__bridge const void *)c, b, axis(g.leftThumbstick.xAxis.value), axis(g.leftThumbstick.yAxis.value),
        axis(g.rightThumbstick.xAxis.value), axis(g.rightThumbstick.yAxis.value));
}
static void attach(GCController *c) {
    GCExtendedGamepad *g = c.extendedGamepad;
    if (!g) { LOG("gamepad: %s has no extended gamepad profile, ignored", c.vendorName.UTF8String ?: "?"); return; }
    int bp = dm_face_by_position(getenv("DM_FACE_BUTTONS"), c.productCategory.UTF8String, c.vendorName.UTF8String);
    LOG("gamepad: connected %s (category %s), face buttons %s", c.vendorName.UTF8String ?: "?", c.productCategory.UTF8String ?: "?",
        bp ? "by position (right A, bottom B, top X, left Y: DM_FACE_BUTTONS=position)" : "by the system's buttonA / B / X / Y");
    __weak GCController *wc = c;
    g.valueChangedHandler = ^(GCExtendedGamepad *gp, GCControllerElement *el) { (void)el; GCController *sc = wc; if (sc) publish(sc, gp, bp); };
    dispatch_async(g_hq, ^{ GCController *sc = wc; if (sc) hap_add(sc); });
    // DM_PAD_MOTION_LOG=1: the controller's motion as GameController reports it, twice a second (plan item 67: GameController documents its axes
    // for an iPhone only, so which way a controller's axes point in nn::hid's frame is measured first: flat, tilted forward, turned left)
    const char *ml = getenv("DM_PAD_MOTION_LOG");
    if (ml && strcmp(ml, "0") && c.motion) {
        GCMotion *m = c.motion; if (m.sensorsRequireManualActivation) m.sensorsActive = YES;
        LOG("gamepad motion: %s: gravity and user acceleration %d, rotation rate %d, attitude %d, sensors active %d", c.vendorName.UTF8String ?: "?",
            m.hasGravityAndUserAcceleration, m.hasRotationRate, m.hasAttitude, m.sensorsActive);
        __block uint64_t last = 0;
        m.valueChangedHandler = ^(GCMotion *mm) {
            uint64_t t = clock_gettime_nsec_np(CLOCK_UPTIME_RAW); if (t - last < 500000000ull) return; last = t;
            GCAcceleration g = mm.gravity, u = mm.userAcceleration, a = mm.acceleration; GCRotationRate r = mm.rotationRate;
            LOG("gamepad motion: gravity %+.3f %+.3f %+.3f, user %+.3f %+.3f %+.3f, acceleration %+.3f %+.3f %+.3f G; rotation %+.3f %+.3f %+.3f rad/s",
                g.x, g.y, g.z, u.x, u.y, u.z, a.x, a.y, a.z, r.x, r.y, r.z);
        };
    }
}
void dm_gamepad_start(void) {
    hap_init();
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    [nc addObserverForName:GCControllerDidConnectNotification object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification *n) { attach(n.object); }];
    [nc addObserverForName:GCControllerDidDisconnectNotification object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification *n) {
        LOG("gamepad: disconnected %s", ((GCController *)n.object).vendorName.UTF8String ?: "?"); drop((__bridge const void *)n.object); }];
    for (GCController *c in GCController.controllers) attach(c);
}
