// Render resolution. The game keeps its PC version's display settings (CameraManagerExtended) but offers no menu for them on Switch:
// SetResolutionLevel picks (width, height) from its configuration's table — 2560x1440 at level 0, which the console clamps to its 1920x1080
// swapchain — and calls Screen.SetResolution(int, int, bool), whose only caller on Switch it is. rules.py hands that function to host call 0x80:
// DM_RENDER_HEIGHT=<lines> makes the game render that many lines, the width keeping the aspect ratio (720 → 1280x720), at most its window textures'
// size (the display, host call 0x81); the presenter scales the picture to the screen. The game sets its resolution once, when it loads its settings
// at startup; with the setting "Native" on a device whose window moves between screens (plan item 302, the iPhone Duo) it is set again from the
// main loop (step_gate) for the screen the window is on.
// (Its PostProcessingActivity setting was tried the same way and left out: turning it off changed neither the picture nor the GPU load.)
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dm.h"
#include "game.h"
#include "hle.h"
#include "native_size.h"

// The display's size the Switch player reports (main+0x90c990, vtable +0x180 of its screen manager: the constant 1920x1080; rules.py): the
// window textures and Screen.SetResolution follow it. DM_DISPLAY_SIZE=<w>x<h> reports another (plan item 281: the iOS setting "Native" gives the
// game view's pixels; plan item 302: the largest screen the device showed it on), so the game renders its own picture at that size with its own
// shaders and interface; unset, 1920x1080 as before. The game asks again after each change of its resolution
static void display_size(unsigned *w, unsigned *h) {
    static unsigned dw, dh; if (!dw) { unsigned a, b; const char *e = getenv("DM_DISPLAY_SIZE");
        if (e && sscanf(e, "%ux%u", &a, &b) == 2 && a >= 640 && b >= 360 && a <= 8192 && b <= 8192) { dw = a & ~1u; dh = b & ~1u; LOG("graphics: the display reported as %ux%u (DM_DISPLAY_SIZE)", dw, dh); }
        else { dw = 1920; dh = 1080; if (e) LOG("[red] graphics: DM_DISPLAY_SIZE=%s is not <w>x<h> from 640x360 to 8192x8192: 1920x1080", e); } }
    *w = dw; *h = dh;
}
static int hc_display_size(uint64_t *r) {
    unsigned dw, dh; display_size(&dw, &dh);
#ifdef DM_TEST_DISPLAY_CONST   // negative control of t_mano_display only: the constant, as before
    (void)dw; (void)dh; *(uint32_t *)(uintptr_t)r[1] = 1920; *(uint32_t *)(uintptr_t)r[2] = 1080;
#else
    *(uint32_t *)(uintptr_t)r[1] = dw; *(uint32_t *)(uintptr_t)r[2] = dh;
#endif
    return 1;
}

// Screen.SetResolution(int width, int height, bool fullscreen) at main+0x3327d00, taken over at its entry (svc ; ret). The original turns
// fullscreen into a FullScreenMode (1 FullScreenWindow, else 3 Windowed) and tail-calls the four-argument overload main+0x328c360 with
// RefreshRate {numerator 0, denominator 1} (x3 = 0x100000000); this does the same with the size scaled. The height is DM_RENDER_HEIGHT at boot
// (0: the game's own) and later the one the main loop applied (step_gate), so the game's own call keeps it
static void (*g_set_resolution4)(int width, int height, int mode, uint64_t refresh_rate);
static _Atomic unsigned g_render_h;                                             // the lines rendered at; 0 = the game's own
static unsigned g_game_w = 2560, g_game_h = 1440; static int g_game_mode = 1;  // what the game asked for last (main thread)
static _Atomic int g_game_set;                                                  // the game set its resolution (its settings are loaded)
static void scaled(unsigned w, unsigned h, unsigned th, unsigned *ow, unsigned *oh) {   // w x h at th lines (the width keeping the ratio, even), within the display
    unsigned dw, dh; display_size(&dw, &dh);
    if (th > dh) th = dh & ~1u;
    if (th > 0 && h && th != h) { unsigned nw = (unsigned)(((uint64_t)w * th + h / 2) / h) & ~1u;
        if (nw > dw) { th = (unsigned)((uint64_t)dw * h / w) & ~1u; nw = (unsigned)(((uint64_t)w * th + h / 2) / h) & ~1u; }
        *ow = nw; *oh = th; }
    else { *ow = w; *oh = h; }
}
static int hc_set_resolution(uint64_t *r) {
    unsigned w = (uint32_t)r[0], h = (uint32_t)r[1], th = atomic_load(&g_render_h), nw, nh;
#ifdef DM_TEST_FOLLOW_BOOT_HEIGHT   // negative control of t_mano_follow only: DM_RENDER_HEIGHT as at boot, whatever the main loop applied since
    { const char *e = getenv("DM_RENDER_HEIGHT"); th = e ? (unsigned)atoi(e) : 0; }
#endif
    g_game_w = w; g_game_h = h; g_game_mode = r[2] & 1 ? 1 : 3;
    scaled(w, h, th, &nw, &nh);
    if (nw != w || nh != h) LOG("graphics: the game sets its resolution %ux%u, rendering at %ux%u", w, h, nw, nh);
    else LOG("graphics: the game sets its resolution %ux%u", w, h);
    g_set_resolution4((int)nw, (int)nh, g_game_mode, 0x100000000ull);
    atomic_store(&g_game_set, 1);
    return 1;
}

// The main loop's step that does a frame's work (main+0x71bcc0, observation gate 1 of rules.py; the loop at main+0x41367c calls its steps through a
// pointer): on the main thread, once a frame (its calls counted against the frames presented, plan item 302). The screen the front end reported last
// (dm_game_native_screen) is applied here, where Screen.SetResolution may be called: from the next frame the game renders at that screen's native
// size, at most its window textures' size (dm_native_fit), and sets its window's crop to it (nvnWindowSetCrop), which the presenter shows. Before the
// game set its resolution only the height is kept, and its own call takes it. DM_TEST_NATIVE_SCREEN=<step>:<w>x<h>[,...] reports those screens at
// those steps (a test without a second display)
static _Atomic uint64_t g_screen;   // the screen reported and not applied yet: h << 32 | w
void dm_game_native_screen(unsigned w, unsigned h) { atomic_store(&g_screen, (uint64_t)h << 32 | w); }
static void step_gate(const uint64_t *regs) {
    (void)regs;
    static const char *script; static int steps;
    if (!steps++) script = getenv("DM_TEST_NATIVE_SCREEN");
    for (const char *q = script; q && *q; q = strchr(q, ',') ? strchr(q, ',') + 1 : NULL) {
        int at; unsigned w, h; if (sscanf(q, "%d:%ux%u", &at, &w, &h) == 3 && at == steps) dm_game_native_screen(w, h);
    }
    uint64_t v = atomic_exchange(&g_screen, 0); if (!v) return;
    unsigned sw = (uint32_t)v, sh = (uint32_t)(v >> 32), dw, dh, W, H, fw, fh; display_size(&dw, &dh);
    if (!dm_native_fit(sw, sh, dw, dh, &W, &H)) return;
    if (dm_native_size(sw, sh, &fw, &fh) && fh > H)
        LOG("graphics: a screen of %ux%u holds %u lines, the window textures %u (made for %ux%u): rendering at %u; at the next launch its own size", sw, sh, fh, dh, dw, dh, H);
    if (H == atomic_load(&g_render_h)) return;
    atomic_store(&g_render_h, H);
    if (!atomic_load(&g_game_set)) { LOG("graphics: the window is on a screen of %ux%u: the game will render %u lines when it sets its resolution", sw, sh, H); return; }
    unsigned nw, nh; scaled(g_game_w, g_game_h, H, &nw, &nh);
    LOG("graphics: the window is on a screen of %ux%u: rendering at %ux%u from the next frame", sw, sh, nw, nh);
    g_set_resolution4((int)nw, (int)nh, g_game_mode, 0x100000000ull);
}

void dm_game_boot(void) {
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) g_set_resolution4 = (void (*)(int, int, int, uint64_t))(void *)(g_mod[i].base + 0x328c360);
    if (!g_set_resolution4) die("graphics: module main not loaded");
    { const char *e = getenv("DM_RENDER_HEIGHT"); int th = e ? atoi(e) : 0; atomic_store(&g_render_h, th > 0 ? (unsigned)th : 0u);
      if (th > 0) LOG("graphics: rendering %d lines (DM_RENDER_HEIGHT)", th); }
    dm_host_call_register(0x80, hc_set_resolution);
    dm_host_call_register(0x81, hc_display_size);
    gate_obs_set(1, step_gate);
}
