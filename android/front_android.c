// The Android app's front end (host/front.h; plan item 301d), with app_android.c: the window, the activity's lifecycle and input.
//   Window: kept for the graphics backend (front_android_window); until the Vulkan one (301e) nothing draws, so the window is filled black once.
//   Lifecycle → host, as the iPhone's scene events (ios/Sources/front_ios.m front_ios_focus_event): onPause = resign active (touches and the
//   controller's held state released, out of focus), onStop = the background (suspended, sound paused, the guest clock and graphics held),
//   onStart = back in front, onResume = active (in focus). onDestroy (the activity finished) stops the host in order (hle_lifecycle.c).
//   Input, on a thread of its own with the input queue on its looper (android/input_map.h has the mapping): touches → the game's touch screen,
//   normalised to the game picture; game controllers (KEYCODE_BUTTON_*, sticks, hat, triggers) → the controller, every one merged as on Apple
//   (gamepad.m: buttons ORed, each stick from the one pushed furthest); keys that are not the game's (volume, Back's system meaning aside) stay
//   the system's. Back is taken and dropped, so that it does not close the game.
// Not yet: the software keyboard (front_text_input says nothing is shown: hle_swkbd.c keeps the game's text), the on-screen controls, motion and
// vibration (with 301e's drawing and 301f).
#include <android/input.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <stdatomic.h>
#include <string.h>
#include "dm.h"
#include "dm_input.h"
#include "front.h"
#include "game.h"
#include "hle.h"
#include "input_map.h"

void dm_dialog(const char *title, const char *msg);   // app_android.c
void dm_main_post(void (*fn)(void *), void *arg);
void dm_bars_hide(void *unused);
uint64_t dm_audio_render_calls(void);

// ---------- the host's side of front.h ----------
static void *guest_main(void *g) { ((void (*)(void))g)(); return NULL; }
// The guest's main thread with a fixed 16 MB stack (front_headless.c has why), joined: dm_main returns when the game does
void front_run(void (*guest)(void)) {
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 16u << 20); pthread_t th;
    if (pthread_create(&th, &a, guest_main, (void *)guest)) die("cannot start the guest main thread");
    pthread_attr_destroy(&a); pthread_join(th, NULL);
}
void *front_metal_layer(void) { return NULL; }
int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap) {
    (void)sub; (void)guide; (void)initial; (void)max_chars; (void)out; (void)out_cap;
    static int said; if (!said++) LOG("[red] front: the game asked for text (\"%s\"): no software keyboard on Android yet, the game keeps its text", title ? title : "");
    return -1;
}
void front_save_failed(int err) {
    LOG("[red] front: a save transaction failed (errno %d); the committed save is unchanged", err);
    dm_dialog("Not enough storage", "The game could not save its progress.");
}
void dm_front_controller_key(void) {}

// ---------- window ----------
static pthread_mutex_t g_win_mu = PTHREAD_MUTEX_INITIALIZER;
static ANativeWindow *g_win; static _Atomic int g_win_w, g_win_h;
ANativeWindow *front_android_window(void) { return g_win; }   // (for 301e's swapchain; under g_win_mu's rules then)
static void fill_black(ANativeWindow *w) {
    ANativeWindow_setBuffersGeometry(w, 0, 0, WINDOW_FORMAT_RGBX_8888); ANativeWindow_Buffer b;
    if (ANativeWindow_lock(w, &b, NULL)) { LOG("[red] front: the window cannot be drawn by the CPU"); return; }
    for (int y = 0; y < b.height; y++) memset((uint8_t *)b.bits + (size_t)y * b.stride * 4, 0, (size_t)b.width * 4);
    ANativeWindow_unlockAndPost(w);
}
static void on_window(ANativeActivity *a, ANativeWindow *w) {
    (void)a; pthread_mutex_lock(&g_win_mu); g_win = w;
    atomic_store(&g_win_w, ANativeWindow_getWidth(w)); atomic_store(&g_win_h, ANativeWindow_getHeight(w));
    fill_black(w); pthread_mutex_unlock(&g_win_mu);
    LOG("front: window %d x %d", atomic_load(&g_win_w), atomic_load(&g_win_h));
}
static void on_window_gone(ANativeActivity *a, ANativeWindow *w) { (void)a; (void)w; pthread_mutex_lock(&g_win_mu); g_win = NULL; pthread_mutex_unlock(&g_win_mu); LOG("front: window gone"); }
static void on_window_resized(ANativeActivity *a, ANativeWindow *w) {
    (void)a; atomic_store(&g_win_w, ANativeWindow_getWidth(w)); atomic_store(&g_win_h, ANativeWindow_getHeight(w));
    LOG("front: window resized to %d x %d", atomic_load(&g_win_w), atomic_load(&g_win_h));
}
static void on_redraw(ANativeActivity *a, ANativeWindow *w) { (void)a; pthread_mutex_lock(&g_win_mu); if (g_win == w) fill_black(w); pthread_mutex_unlock(&g_win_mu); }

// ---------- input ----------
#define NPAD 4
static struct { int used; int32_t dev; uint64_t keys, axbits; int32_t ax[4]; } g_pad[NPAD];   // per controller: keys held, hat / trigger buttons, sticks
static void pad_apply(void) {
    uint64_t b = 0; int32_t ax[4] = {0}; double best[2] = {0};
    for (int i = 0; i < NPAD; i++) {
        if (!g_pad[i].used) continue;
        b |= g_pad[i].keys | g_pad[i].axbits;
        for (int k = 0; k < 2; k++) { double m = hypot(g_pad[i].ax[2 * k], g_pad[i].ax[2 * k + 1]);
            if (m > best[k]) { best[k] = m; ax[2 * k] = g_pad[i].ax[2 * k]; ax[2 * k + 1] = g_pad[i].ax[2 * k + 1]; } }
    }
    dm_pad_set(b, ax[0], ax[1], ax[2], ax[3]);
}
static int pad_slot(int32_t dev) {
    int fr = -1;
    for (int i = 0; i < NPAD; i++) { if (g_pad[i].used && g_pad[i].dev == dev) return i; if (!g_pad[i].used && fr < 0) fr = i; }
    if (fr >= 0) { memset(&g_pad[fr], 0, sizeof g_pad[fr]); g_pad[fr].used = 1; g_pad[fr].dev = dev; LOG("front: controller (input device %d) in slot %d", dev, fr); }
    return fr;
}
static void pad_release(void) {   // focus lost: what is held never reports its release
    int any = 0; for (int i = 0; i < NPAD; i++) { any |= g_pad[i].keys || g_pad[i].axbits; g_pad[i].keys = g_pad[i].axbits = 0; memset(g_pad[i].ax, 0, sizeof g_pad[i].ax); }
    if (any) LOG("front: focus lost, controller buttons and sticks cleared");
    pad_apply();
}
// Touches: each pointer id keeps the lowest free finger slot from its down to its up; one that went down outside the picture is not the game's
static struct { int on; int32_t pid; float x, y; } g_fing[16];
static void touch_push(void) {
    dm_touch t[16]; int n = 0;
    for (int i = 0; i < 16; i++) if (g_fing[i].on) t[n++] = (dm_touch){(uint32_t)i, g_fing[i].x, g_fing[i].y};
    dm_touch_set(t, n);
}
static void touch_lift_all(void) { memset(g_fing, 0, sizeof g_fing); dm_touch_set(NULL, 0); }
static int touch_event(const AInputEvent *e) {
    float r[4]; dm_and_letterbox((float)atomic_load(&g_win_w), (float)atomic_load(&g_win_h), dm_game.screen_w > 0 ? dm_game.screen_w : 1280, dm_game.screen_h > 0 ? dm_game.screen_h : 720, r);
    int32_t act = AMotionEvent_getAction(e), kind = act & AMOTION_EVENT_ACTION_MASK;
    size_t idx = (size_t)(act & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    if (kind == AMOTION_EVENT_ACTION_CANCEL) { touch_lift_all(); return 1; }
    if (kind == AMOTION_EVENT_ACTION_DOWN || kind == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        float nx, ny; int32_t pid = AMotionEvent_getPointerId(e, idx);
        if (dm_and_touch_norm(r, AMotionEvent_getX(e, idx), AMotionEvent_getY(e, idx), &nx, &ny))
            for (int i = 0; i < 16; i++) if (!g_fing[i].on) { g_fing[i].on = 1; g_fing[i].pid = pid; g_fing[i].x = nx; g_fing[i].y = ny; break; }
    }
    for (size_t p = 0; p < AMotionEvent_getPointerCount(e); p++) {   // every pointer's place, then the one that lifted goes
        int32_t pid = AMotionEvent_getPointerId(e, p);
        for (int i = 0; i < 16; i++) if (g_fing[i].on && g_fing[i].pid == pid) dm_and_touch_norm(r, AMotionEvent_getX(e, p), AMotionEvent_getY(e, p), &g_fing[i].x, &g_fing[i].y);
    }
    if (kind == AMOTION_EVENT_ACTION_UP || kind == AMOTION_EVENT_ACTION_POINTER_UP) {
        int32_t pid = AMotionEvent_getPointerId(e, idx);
        for (int i = 0; i < 16; i++) if (g_fing[i].on && g_fing[i].pid == pid) g_fing[i].on = 0;
        if (kind == AMOTION_EVENT_ACTION_UP) memset(g_fing, 0, sizeof g_fing);   // the last pointer: none left
    }
    touch_push(); return 1;
}
static int joy_event(const AInputEvent *e) {
    int i = pad_slot(AInputEvent_getDeviceId(e)); if (i < 0) return 0;
    float lt = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_LTRIGGER, 0), rt = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_RTRIGGER, 0);
    float br = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_BRAKE, 0), gas = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_GAS, 0);
    g_pad[i].axbits = dm_and_hat_bits(AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_HAT_X, 0), AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_HAT_Y, 0)) |
                      dm_and_trigger_bits(lt > br ? lt : br, rt > gas ? rt : gas);
    g_pad[i].ax[0] = dm_and_axis(AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_X, 0), 0); g_pad[i].ax[1] = dm_and_axis(AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_Y, 0), 1);
    g_pad[i].ax[2] = dm_and_axis(AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_Z, 0), 0); g_pad[i].ax[3] = dm_and_axis(AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_RZ, 0), 1);
    pad_apply(); return 1;
}
static int key_event(const AInputEvent *e) {
    int32_t k = AKeyEvent_getKeyCode(e), act = AKeyEvent_getAction(e);
    if (k == AKEYCODE_BACK) return 1;   // taken: Back would finish the activity, and the game with it
    static int by_position = -1; if (by_position < 0) by_position = dm_face_by_position(getenv("DM_FACE_BUTTONS"), NULL, NULL);   // (after env.txt)
    uint64_t bit = dm_and_key_bit(k, by_position); if (!bit) return 0;
    int i = pad_slot(AInputEvent_getDeviceId(e)); if (i < 0) return 0;
    if (act == AKEY_EVENT_ACTION_DOWN) g_pad[i].keys |= bit; else if (act == AKEY_EVENT_ACTION_UP) g_pad[i].keys &= ~bit;
    pad_apply(); return 1;
}
static int handle(const AInputEvent *e) {
    int32_t type = AInputEvent_getType(e), src = AInputEvent_getSource(e);
    if (type == AINPUT_EVENT_TYPE_KEY) return key_event(e);
    if (type != AINPUT_EVENT_TYPE_MOTION) return 0;
    if ((src & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK) return joy_event(e);
    if ((src & AINPUT_SOURCE_TOUCHSCREEN) == AINPUT_SOURCE_TOUCHSCREEN) return touch_event(e);
    return 0;
}
static ALooper *g_in_looper; static _Atomic int g_in_ready;
static int on_input(int fd, int events, void *data) {
    (void)fd; (void)events; AInputQueue *q = data; AInputEvent *e;
    while (AInputQueue_getEvent(q, &e) >= 0) { if (AInputQueue_preDispatchEvent(q, e)) continue; AInputQueue_finishEvent(q, e, handle(e)); }
    return 1;
}
static void *input_main(void *a) {
    (void)a; dm_setname_self("dm input");
    g_in_looper = ALooper_prepare(0); ALooper_acquire(g_in_looper); atomic_store(&g_in_ready, 1);
    for (;;) ALooper_pollOnce(-1, NULL, NULL, NULL);
    return NULL;
}
static void on_queue(ANativeActivity *a, AInputQueue *q) { (void)a; while (!atomic_load(&g_in_ready)) usleep(1000); AInputQueue_attachLooper(q, g_in_looper, 1, on_input, q); }
static void on_queue_gone(ANativeActivity *a, AInputQueue *q) { (void)a; AInputQueue_detachLooper(q); }

// ---------- lifecycle ----------
// The iPhone's four scene events (front_ios.m), from the activity's: 0 resign active, 1 the background, 2 back in front, 3 active
static int g_bg, g_audio_bg;
static void focus_event(int ev) {
    static const char *k[] = {"onPause", "onStop", "onStart", "onResume"};
    LOG("front: %s (guest clock %.3f s, audio callbacks %llu)", k[ev], guest_ns() / 1e9, (unsigned long long)dm_audio_render_calls());
    switch (ev) {
    case 0: touch_lift_all(); pad_release(); dm_focus_set(2); break;
    case 1: g_bg = 1; dm_focus_set(3); g_audio_bg = 1; dm_audio_suspend(1); guest_clock_freeze_for(1, 1); dm_gfx_suspend(1); break;
    case 2: if (!g_bg) break; g_bg = 0; dm_gfx_suspend(0); guest_clock_freeze_for(1, 0); g_audio_bg = 0; dm_audio_suspend(0); dm_focus_set(2); break;
    case 3: dm_focus_set(1); dm_main_post(dm_bars_hide, NULL); break;
    }
}
static _Atomic int g_started;   // the first onStart / onResume come before the host is up: it starts in focus
static void on_start(ANativeActivity *a) { (void)a; if (atomic_load(&g_started)) focus_event(2); }
static void on_resume(ANativeActivity *a) { (void)a; if (atomic_exchange(&g_started, 1)) focus_event(3); }
static void on_pause(ANativeActivity *a) { (void)a; focus_event(0); }
static void on_stop(ANativeActivity *a) { (void)a; focus_event(1); }
static void on_destroy(ANativeActivity *a) { (void)a; LOG("front: onDestroy (the activity finished)"); dm_host_shutdown("the activity finished"); }
static void *on_save(ANativeActivity *a, size_t *n) { (void)a; *n = 0; return NULL; }
static void on_focus(ANativeActivity *a, int focused) { (void)a; LOG("front: window focus %d", focused); if (focused) dm_main_post(dm_bars_hide, NULL); }

void front_android_install(ANativeActivity *a) {
    ANativeActivityCallbacks *c = a->callbacks;
    c->onStart = on_start; c->onResume = on_resume; c->onPause = on_pause; c->onStop = on_stop; c->onDestroy = on_destroy;
    c->onSaveInstanceState = on_save; c->onWindowFocusChanged = on_focus;
    c->onNativeWindowCreated = on_window; c->onNativeWindowDestroyed = on_window_gone; c->onNativeWindowResized = on_window_resized;
    c->onNativeWindowRedrawNeeded = on_redraw; c->onInputQueueCreated = on_queue; c->onInputQueueDestroyed = on_queue_gone;
    static int once; if (once++) return;
    pthread_t th; if (pthread_create(&th, NULL, input_main, NULL)) LOG("[red] front: cannot start the input thread"); else pthread_detach(th);
}
