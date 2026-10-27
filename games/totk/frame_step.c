// Real-time frame step. Tears of the Kingdom advances its world every frame by a step it derives from the present interval its frame-rate
// controller chose (ratio = 30 / fps: 1 at 30 fps, 0.5 at 60), which is right only while every frame takes exactly that interval. rules.py
// (HOSTREFS) makes the two producers of the step read it from main's host page instead, and this writes the measured frame time there at every
// present, so the game keeps its speed at 60, 90 or 120 fps (DM_PRESENT_HZ) and when a frame misses its interval.
// Method and limits from MacNativeGames' game_patches_v143.c (runtime_frame_step_update; ly05010419/MacNativeGames, MIT): the mean of the last five
// present intervals, each clamped to 1/120 .. 1/15 s; an interval above 0.25 s or below 1 ms (loading, a pause) restarts the mean and keeps the
// step. Intervals are on the guest clock, the one behind the game's own system tick.
// Host page: +0 ratio = seconds × 30, +4 ticks = seconds × 60, written with one 64-bit store so the game never reads the two from different frames;
// +8 seconds, for the cloth and hair clock (rules.py CAVES, plan item 163), written just before (that clock reads +0 and +8 within a few instructions).
// DM_FRAME_STEP_MS=<ms> pins the step (diagnostics: a pinned 16.667 at 90 fps makes the game run 1.5 times too fast).
// +16 / +20: the scene render size as floats (rules.py, plan item 52: main 0x585e48-0x585e60 store it to the renderer instead of the graphics
// parameter's): DM_RENDER_RES=<w>x<h> (64 up to 1920x1080, the output size; 2560x1440 and 3200x1800 ran the game's texture heap out at main+0x1c2f20
// in its first seconds), else the parameter's own size for the operation mode, which a probe at main 0x585e4c read once at boot (docked 1600x900,
// handheld 1280x720), so the game looks as before unless asked. Above 1920x1080 with the RomFS overlay games/totk/heapdef.py writes (plan item 95):
// its HeapDef grows the heap for a scene size it notes beside it (.dm_scene), up to which DM_RENDER_RES is then taken. Without DM_RENDER_RES,
// DM_RENDER_HEIGHT=<lines> (the iOS app's render resolution setting, Settings → the app; manosaba's graphics.c reads it too) asks for that many
// lines at 16:9 (540 → 960x540), so the 60 fps iPhone build can trade resolution for GPU time (plan item 62l), at most the largest 16:9 the heap
// holds (the setting "Native" asks for the screen's lines, plan item 281: an iPhone Air's 1260 become 1920x1080 without the heap overlay).
// +24 / +32: the swapchain and the canvas size, each width | height << 32 (rules.py option output, plan item 63: main 0xbdb578 / 0xbdb584 read the
// swapchain's, the code cave at 0x3dcb28 the canvas's, which the game composes its UI and the scene's final upscale in). DM_OUTPUT_RES=<w>x<h>
// (640x360 up to 1920x1080, or up to the size the heap overlay notes, .dm_output: games/totk/heapdef.py --output) makes both that size; without
// it DM_DISPLAY_SIZE (the iOS setting "Native": the screen's 16:9 picture, plan item 281) does, at most the largest 16:9 the heap holds; else the
// swapchain 1920x1080 (the game's constant) and the canvas 0, which the cave leaves as the game laid it out, so the game looks as before.
// +48 / +56: a live scene size (plan item 240, scene_latch_step below): the renderer the game picked a size for, and the flags asking for a pick.
// +40: the factor on the cameras' aspect ratio (rules.py option aspect, plan item 154: four code caves multiply by it, by 1 while it is 0):
// the wide picture's ratio (host/game/aspect.c: DM_ASPECT, the Mac's View → Wide Picture) over the game's 16:9 while it is on, 0 off; written at
// every present, so a switch reaches the next frame's cameras.
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dm.h"
#include "game.h"
#include "aspect.h"
int dm_opmode(void);   // hle_oe.c
void dm_set_opmode(int m);   // hle_oe.c
int dm_overlay_file(const char *rel, char *out, size_t n);   // hle_fs.c: the file in the RomFS overlay layers

typedef struct { uint64_t last_ns; double dt[5]; unsigned n, next; } frame_mean;
// Feeds one present time; returns the step in seconds, or 0 when the step stays as it is. The mean of the last five intervals, then kept between
// 1/120 and 1/15 s (plan item 250, after MacNativeGames' Breath of the Wild path, MIT): each interval clamped first made a run of short and long
// ones (6 and 11 ms, 8.5 on average) count 9.7 ms, and the game ran fast (MacNativeGames: 6 % at about 95 fps under a 120 Hz clock)
static double frame_mean_add(frame_mean *m, uint64_t now_ns) {
    if (!m->last_ns || now_ns <= m->last_ns) { m->last_ns = now_ns; return 0; }
    double dt = (now_ns - m->last_ns) * 1e-9; m->last_ns = now_ns;
    if (dt > 0.25 || dt < 0.001) { m->n = m->next = 0; return 0; }
#ifdef DM_TEST_FSTEP_CLAMP_EACH   // negative control of t_frame_step only: each interval clamped, as before
    if (dt < 1.0 / 120) dt = 1.0 / 120;
    if (dt > 1.0 / 15) dt = 1.0 / 15;
#endif
    m->dt[m->next++ % 5] = dt; if (m->n < 5) m->n++;
    double sum = 0; for (unsigned i = 0; i < m->n; i++) sum += m->dt[i];
    double mean = sum / m->n;
    return mean < 1.0 / 120 ? 1.0 / 120 : mean > 1.0 / 15 ? 1.0 / 15 : mean;
}
// The largest size the heap holds: the heap overlay's note "<w>x<h>" (1920x1080 up to 3840x2160), else 1920x1080
static void heap_max(const char *max, unsigned *mw, unsigned *mh) {
    char extra; *mw = 1920; *mh = 1080;
    if (max && (sscanf(max, "%ux%u%c", mw, mh, &extra) != 2 || *mw < 1920 || *mh < 1080 || *mw > 3840 || *mh > 2160)) { *mw = 1920; *mh = 1080; }
}
// The largest even 16:9 size of at most h lines within mw x mh (the width from the height, rounded to even as the setting's lines always were)
static void fit_16_9(unsigned h, unsigned mw, unsigned mh, unsigned *ow, unsigned *oh) {
    if (h > mh) h = mh;
    if ((uint64_t)h * 16 > (uint64_t)mw * 9) h = mw * 9 / 16;
    *oh = h & ~1u; *ow = (*oh * 16 / 9 + 1) & ~1u;
}
// The size asked for: DM_RENDER_RES as it is, else DM_RENDER_HEIGHT's lines at 16:9 (width rounded to even) written into buf, else NULL. The lines
// are at most the largest 16:9 the heap holds (max as scene_size's): the iOS setting "Native" asks for the screen's (an iPhone Air 1260, plan item
// 281), which past 1080 the game's own heap cannot hold, and the size was then refused and the game's own 1280x720 drawn
static const char *scene_env(const char *res, const char *height, const char *max, char *buf, size_t n) {
    if (res && *res) return res;
#ifndef DM_TEST_SCENE_NO_HEIGHT   // negative control of t_frame_step only: DM_RENDER_HEIGHT ignored, as before
    char extra; unsigned h, w, mw, mh;
    if (height && sscanf(height, "%u%c", &h, &extra) == 1 && h > 0) {
#ifndef DM_TEST_SCENE_HEIGHT_NO_FIT   // negative control of t_frame_step only: the lines asked as they are (and above the heap's refused)
        heap_max(max, &mw, &mh);
#else
        mw = mh = 65535;
#endif
        fit_16_9(h, mw, mh, &w, &h); snprintf(buf, n, "%ux%u", w, h); return buf;
    }
#else
    (void)height; (void)max; (void)buf; (void)n;
#endif
    return NULL;
}
// The scene size for the page; returns 1 when it comes from env (DM_RENDER_RES), 0 for the game's own, -1 for a value it refused (the game's own).
// max: the largest the heap allows, "<w>x<h>" (the overlay's note), NULL for the output size 1920x1080
static int scene_size(const char *env, int opmode, const char *max, float out[2]) {
    out[0] = opmode ? 1600 : 1280; out[1] = opmode ? 900 : 720;
    if (!env || !*env || !strcmp(env, "0")) return 0;
    unsigned w, h, mw = 1920, mh = 1080; char extra;
#ifndef DM_TEST_SCENE_NO_HEAP   // negative control of t_frame_step only: the overlay's note ignored
    heap_max(max, &mw, &mh);
#endif
    if (sscanf(env, "%ux%u%c", &w, &h, &extra) != 2 || w < 64 || h < 64 || w > mw || h > mh) return -1;
    out[0] = (float)w; out[1] = (float)h; return 1;
}
// The output asked for: DM_OUTPUT_RES as it is, else the display the iOS setting "Native" reports (DM_DISPLAY_SIZE, the screen's 16:9 picture,
// plan item 281: the UI composed at the screen's size, as manosaba's is) at most the largest 16:9 the heap holds (max as output_size's), written
// into buf, else NULL
static const char *output_env(const char *res, const char *display, const char *max, char *buf, size_t n) {
    if (res && *res) return res;
#ifndef DM_TEST_OUTPUT_NO_DISPLAY   // negative control of t_frame_step only: DM_DISPLAY_SIZE ignored, as before
    unsigned w, h, mw, mh; char extra;
    if (display && sscanf(display, "%ux%u%c", &w, &h, &extra) == 2 && w >= 640 && h >= 360) {
        heap_max(max, &mw, &mh); fit_16_9(h, w < mw ? w : mw, mh, &w, &h); snprintf(buf, n, "%ux%u", w, h); return buf;
    }
#else
    (void)display; (void)max; (void)buf; (void)n;
#endif
    return NULL;
}
// The output size for the page; returns 1 when it comes from env (DM_OUTPUT_RES), 0 for the game's own, -1 for a value it refused (the game's own).
// max: the largest the heap allows, "<w>x<h>" (the overlay's .dm_output note), NULL for the game's own 1920x1080. out: {swapchain, canvas} packed
static int output_size(const char *env, const char *max, uint64_t out[2]) {
    out[0] = (uint64_t)1080 << 32 | 1920; out[1] = 0;
    if (!env || !*env || !strcmp(env, "0")) return 0;
    unsigned w, h, mw, mh; char extra;
    heap_max(max, &mw, &mh);
    if (sscanf(env, "%ux%u%c", &w, &h, &extra) != 2 || w < 640 || h < 360 || w > mw || h > mh) return -1;
    out[0] = (uint64_t)h << 32 | w;
#ifndef DM_TEST_OUTPUT_NO_CANVAS   // negative control of t_frame_step only: the canvas left to the game (only the swapchain grows)
    out[1] = out[0];
#endif
    return 1;
}
// Live scene size (plan item 240, after NativeTOTK 1723fed, BennettStaley/NativeTOTK-Apple-Silicon, MIT, written anew). The game reads the size on
// the page only when it picks one (main 0x585da0), which it does when its operation mode changes, so a size written while it runs waits for the next
// mode change. It is sent down the game's own mode-change path once instead: the per-frame graphics update takes it (main 0x580b28, 0x585bd0 with
// force 1: the pick, and the scene's render targets made again at the new size) when the flags at its display state +8 have 0x280, and the code cave
// at main 0x580920 (rules.py, option scene) ORs the page +56 into the flags it loads; the cave at 0x585e48 keeps the renderer (x19 of the pick) at
// +48. At a present with a new size asked: the size to +16 / +20, the renderer's cached mode index (+0xcd0, a signed byte the pick stores; only
// compared, never an index) to -1 and +56 to 0x80. Once the index is no longer -1 the pick has run and +56 goes back to 0; after 600 presents
// without a pick (a pause, a load) +56 is cleared all the same and the size waits for the game's next pick. NativeTOTK found the page alone, the
// cached index alone and nn::oe's display-config message all leaving the scene as it was. A live size above what the heap was shown to hold (the
// game's 1600x900 beside its 1080p canvas, or the size it booted with, or the heap overlay's note) is refused: the targets come from a heap sized
// once at boot (main+0x1c2f20 ran out at 2560x1440).
typedef struct { uint64_t latched; uintptr_t forced, picked; unsigned waited; } scene_latch;
enum { SCENE_PICKED = 1, SCENE_GAVE_UP = 2, SCENE_WROTE = 4, SCENE_NO_RENDERER = 8 };
// One present; want = w | h << 32, the size the page is to carry. Returns what it did (SCENE_* bits); s->picked = the renderer that picked
static int scene_latch_step(scene_latch *s, uint8_t *page, uint64_t want) {
    int r = 0;
    if (s->forced) {
        if (atomic_load_explicit((_Atomic int8_t *)(s->forced + 0xcd0), memory_order_relaxed) != -1) { r = SCENE_PICKED; s->picked = s->forced; }
        else if (++s->waited >= 600) r = SCENE_GAVE_UP;
#ifndef DM_TEST_SCENE_LATCH_NO_CLEAR   // negative control of t_frame_step only: +56 left set after the pick
        if (r) { atomic_store_explicit((_Atomic uint32_t *)(void *)(page + 56), 0, memory_order_relaxed); s->forced = 0; }
#else
        if (r) s->forced = 0;
#endif
    }
    if (want == s->latched) return r;
    float sz[2] = {(float)(uint32_t)want, (float)(uint32_t)(want >> 32)}; uint64_t bits; memcpy(&bits, sz, sizeof bits);
    atomic_store_explicit((_Atomic uint64_t *)(void *)(page + 16), bits, memory_order_relaxed);   // one store: the game's ldp reads both
    s->latched = want;
    uintptr_t rend = (uintptr_t)atomic_load_explicit((_Atomic uint64_t *)(void *)(page + 48), memory_order_relaxed);
    if (!rend) return r | SCENE_NO_RENDERER;   // the game has not picked a size yet: its first pick takes the page's
    atomic_store_explicit((_Atomic int8_t *)(rend + 0xcd0), -1, memory_order_relaxed);
    s->forced = rend; s->waited = 0;
#ifndef DM_TEST_SCENE_LATCH_NO_KICK   // negative control of t_frame_step only: the page and the index written, the mode-change path not asked for
    atomic_store_explicit((_Atomic uint32_t *)(void *)(page + 56), 0x80, memory_order_release);
#endif
    return r | SCENE_WROTE;
}
// A live size w x h: as scene_size takes it (64x64 up to 1920x1080 or the overlay's note max), and at most the pixels of the larger of 1600x900 and
// boot (the size the page got at boot, w | h << 32). Returns w | h << 32, or 0 when refused
static uint64_t scene_live_size(unsigned w, unsigned h, const char *max, uint64_t boot) {
    char b[32]; float sz[2]; snprintf(b, sizeof b, "%ux%u", w, h);
    if (scene_size(b, 1, max, sz) != 1) return 0;
    uint64_t cap = (uint64_t)(uint32_t)boot * (boot >> 32); if (cap < 1600 * 900) cap = 1600 * 900;
    if (max && *max) { unsigned mw, mh; char extra; if (sscanf(max, "%ux%u%c", &mw, &mh, &extra) == 2 && (uint64_t)mw * mh > cap) cap = (uint64_t)mw * mh; }
    return (uint64_t)w * h <= cap ? (uint64_t)h << 32 | w : 0;
}
static void frame_step_store(_Atomic uint64_t *page, double seconds) {   // page: at least 16 bytes
    float v[2] = {(float)(seconds * 30), (float)(seconds * 60)}, sec = (float)seconds; uint64_t bits; memcpy(&bits, v, sizeof bits); uint32_t sb; memcpy(&sb, &sec, sizeof sb);
#ifndef DM_TEST_CLOTH_NO_SECONDS   // negative control of t_frame_step only: +8 left as it was
    atomic_store_explicit((_Atomic uint32_t *)(void *)((uint8_t *)page + 8), sb, memory_order_relaxed);
#endif
    atomic_store_explicit(page, bits, memory_order_release);
}

// The iOS setting "Native" (DM_DISPLAY_SIZE, plan item 281) runs the game in the console's docked mode unless DM_OPMODE says otherwise: docked, the
// shipped heap holds a 1920x1080 scene beside the 1920x1080 canvas the game lays out there; handheld (the phone's build otherwise, plan item 62d)
// it does not: on the Mac a 1920x1080 canvas beside a 1920x1080 or 1600x900 scene ran it out at main+0x1c2f20 within seconds (beside the
// game's own 1280x720 scene it held, and so did a 1920x1080 scene beside handheld's 1280x720 canvas)
static int native_docked(const char *display, const char *opmode) {
#ifndef DM_TEST_NATIVE_HANDHELD   // negative control of t_frame_step only: the mode left as it was
    return display && *display && !opmode;
#else
    (void)display; (void)opmode; return 0;
#endif
}
#ifndef FRAME_STEP_TEST
static _Atomic uint64_t *g_page;
static frame_mean g_mean;
static double g_pin;
static int g_realtime;   // the option realtime is on: the step is the measured frame time
static int g_aspect;     // the option aspect is on: +40 follows the wide picture
static int g_scene;      // the option scene is on: a size asked while the game runs is latched at the presents (scene_latch_step)
static _Atomic uint64_t g_scene_want; static scene_latch g_latch; static char g_scene_max[32]; static uint64_t g_scene_boot;
void dm_game_boot(void) {
    g_page = dm_host_page("main");
    if (!g_page) { LOG("[red] frame step: main has no host page (product built without rules.py HOSTREFS): the game's step stays at its chosen interval"); return; }
    const char *e = getenv("DM_FRAME_STEP_MS"); g_pin = e ? atof(e) / 1000 : 0;
    frame_step_store(g_page, g_pin > 0 ? g_pin : 1.0 / 30);   // the controller starts at interval 2
    if (native_docked(getenv("DM_DISPLAY_SIZE"), getenv("DM_OPMODE")) && dm_opmode() != 1) {
        dm_set_opmode(1); LOG("[scene size] DM_DISPLAY_SIZE (the setting Native): docked, whose heap holds a 1920x1080 scene and canvas (DM_OPMODE sets a mode)");
    }
    if (dm_option_on("scene")) {
        char note[1024], max[32] = ""; FILE *f = dm_overlay_file("System/HeapSize/Product.Nin_NX_NVN.HeapDef.dm_scene", note, sizeof note) ? fopen(note, "r") : NULL;
        if (f) { if (!fgets(max, sizeof max, f)) max[0] = 0; fclose(f); max[strcspn(max, "\r\n")] = 0; }
        char hb[32]; const char *rh = getenv("DM_RENDER_HEIGHT"), *want = scene_env(getenv("DM_RENDER_RES"), rh, max[0] ? max : NULL, hb, sizeof hb),
                                *src = want == hb ? "DM_RENDER_HEIGHT" : "DM_RENDER_RES";
        float sz[2]; int from = scene_size(want, dm_opmode(), max[0] ? max : NULL, sz); memcpy((uint8_t *)g_page + 16, sz, sizeof sz);
        if (from < 0) LOG("[red] %s=%s: expected <width>x<height>, from 64x64 up to %s; the game's own size", src, want,
                          max[0] ? max : "1920x1080 (more needs the heap overlay of games/totk/heapdef.py)");
        else if (max[0]) LOG("[scene size] the RomFS overlay's heap allows up to %s", max);
        if (from > 0 && want == hb && (unsigned)atoi(rh) > (unsigned)sz[1])
            LOG("[scene size] DM_RENDER_HEIGHT=%s is more than the heap holds: the largest 16:9 it does, %s", rh, max[0] ? max : "1920x1080");
        LOG("[scene size] %.0fx%.0f (%s)", sz[0], sz[1], from > 0 ? src : dm_opmode() ? "the game's own, docked" : "the game's own, handheld");
        g_scene_boot = g_latch.latched = (uint64_t)(uint32_t)sz[1] << 32 | (uint32_t)sz[0]; atomic_store(&g_scene_want, g_scene_boot);
        strlcpy(g_scene_max, max, sizeof g_scene_max); g_scene = 1;
    } else if (getenv("DM_RENDER_RES")) LOG("[red] DM_RENDER_RES needs the option scene (DM_OPTIONS)");
    if (dm_option_on("output")) {
        char note[1024], max[32] = ""; FILE *f = dm_overlay_file("System/HeapSize/Product.Nin_NX_NVN.HeapDef.dm_output", note, sizeof note) ? fopen(note, "r") : NULL;
        if (f) { if (!fgets(max, sizeof max, f)) max[0] = 0; fclose(f); max[strcspn(max, "\r\n")] = 0; }
        char ob[32]; const char *ds = getenv("DM_DISPLAY_SIZE"), *want = output_env(getenv("DM_OUTPUT_RES"), ds, max[0] ? max : NULL, ob, sizeof ob),
                                *src = want == ob ? "DM_DISPLAY_SIZE" : "DM_OUTPUT_RES";
        uint64_t sz[2]; int from = output_size(want, max[0] ? max : NULL, sz); memcpy((uint8_t *)g_page + 24, sz, sizeof sz);
        if (from < 0) LOG("[red] %s=%s: expected <width>x<height>, from 640x360 up to %s; the game's own", src, want,
                          max[0] ? max : "1920x1080 (more needs the heap overlay of games/totk/heapdef.py --output)");
        else if (max[0]) LOG("[output size] the RomFS overlay's heap allows up to %s", max);
        if (from > 0 && want == ob && strcmp(ds, ob)) LOG("[output size] DM_DISPLAY_SIZE=%s: the largest 16:9 the heap holds within it, %s", ds, ob);
        if (from > 0) LOG("[output size] swapchain and canvas %ux%u (%s)", (unsigned)sz[0], (unsigned)(sz[0] >> 32), src);
        else LOG("[output size] the game's own (swapchain 1920x1080, the canvas as the game lays it out)");
    } else if (getenv("DM_OUTPUT_RES")) LOG("[red] DM_OUTPUT_RES needs the option output (DM_OPTIONS)");
    if ((g_aspect = dm_option_on("aspect"))) dm_aspect_init();
    else if (getenv("DM_ASPECT")) LOG("[red] DM_ASPECT needs the option aspect (DM_OPTIONS)");
    if (!(g_realtime = dm_option_on("realtime"))) { LOG("[frame step] the game's own (option realtime not on: DM_OPTIONS or the product's options.default)"); return; }
    if (g_pin > 0) LOG("[frame step] pinned at %.3f ms (DM_FRAME_STEP_MS, diagnostics)", g_pin * 1000);
    else LOG("[frame step] real time: the mean of the last five present intervals (kept between 1/120 and 1/15 s)");
}
// A scene size while the game runs (the command channel's "scene <w>x<h>", games/totk/survey.c); 0 when taken (it applies at the next present),
// -1 refused (logged)
int totk_scene_request(unsigned w, unsigned h) {
    if (!g_scene) { LOG("[red] scene %ux%u: needs the option scene (DM_OPTIONS)", w, h); return -1; }
    uint64_t v = scene_live_size(w, h, g_scene_max, g_scene_boot);
    if (!v) { uint64_t b = g_scene_boot; LOG("[red] scene %ux%u refused: 64x64 up to %s, and no more pixels than %ux%u or 1600x900 (the heap is sized at boot)", w, h,
                                             g_scene_max[0] ? g_scene_max : "1920x1080", (unsigned)(uint32_t)b, (unsigned)(b >> 32)); return -1; }
    atomic_store(&g_scene_want, v); LOG("[scene size] %ux%u asked: taken at the next present", w, h); return 0;
}
int dm_game_realtime_step(void) { return g_realtime && g_pin <= 0; }   // (a pinned step needs the fixed pacing)
int nvn_fstep_step(void);   // gfx/nvn/hle_nvn.c: DM_FRAME_STEP=vsync, the host's step in vsyncs of 60 Hz (plan item 133), 0 otherwise
// The game's dynamic resolution as it runs (plan items 131a / 276): the scale the renderer holds at +0xccc (the controller 0x584300's tier, stored by
// its caller; rules.py option fixedres holds it at 100 %), read at each present from the renderer the scene option keeps at +48; every 600 presents
// the values seen and how often, when more than one was or it moved
static void dr_note(void) {
    static struct { uint32_t v; unsigned n; } seen[4]; static int ns; static unsigned presents, other; static uint32_t last;
    uintptr_t rend = (uintptr_t)atomic_load_explicit((_Atomic uint64_t *)(void *)((uint8_t *)g_page + 48), memory_order_relaxed);
    if (!rend) return;
    uint32_t v = atomic_load_explicit((_Atomic uint32_t *)(rend + 0xccc), memory_order_relaxed);
    int k = 0; while (k < ns && seen[k].v != v) k++;
    if (k == ns) { if (ns < 4) { seen[ns].v = v; seen[ns].n = 0; ns++; } else k = -1; }
    if (k >= 0) seen[k].n++; else other++;
    int moved = presents && v != last; last = v;
    if (++presents < 600 && !moved) return;
    if (presents >= 600) {
        if (ns > 1 || other) { char b[256]; int o = 0; for (int i = 0; i < ns; i++) { float f; memcpy(&f, &seen[i].v, 4); o += snprintf(b + o, sizeof b - o, " %g × %u", f, seen[i].n); }
            LOG("[scene size] the game's dynamic resolution over %u presents: renderer +0xccc%s%s", presents, b, other ? " and others" : ""); }
        ns = 0; presents = 0; other = 0; }
}
void dm_game_present(void) {
    if (g_scene) dr_note();
    if (g_scene) { int r = scene_latch_step(&g_latch, (uint8_t *)g_page, atomic_load(&g_scene_want)); uint64_t l = g_latch.latched;
        if (r & SCENE_WROTE) LOG("[scene size] %ux%u on the page; the game's mode-change path asked for one pick", (unsigned)(uint32_t)l, (unsigned)(l >> 32));
        if (r & SCENE_NO_RENDERER) LOG("[scene size] %ux%u on the page; the game has not picked a size yet and takes it at its first", (unsigned)(uint32_t)l, (unsigned)(l >> 32));
        if (r & SCENE_PICKED) { float v[2]; memcpy(v, (void *)(g_latch.picked + 0xcc4), sizeof v); LOG("[scene size] the game picked again: renderer %.0fx%.0f", v[0], v[1]); }
        if (r & SCENE_GAVE_UP) LOG("[red] scene size: no pick after 600 presents; the size waits for the game's next pick of its own"); }
    if (g_aspect) { float f = dm_aspect_factor(dm_aspect_now(), 16.0 / 9); uint32_t b; memcpy(&b, &f, sizeof b);
                    atomic_store_explicit((_Atomic uint32_t *)(void *)((uint8_t *)g_page + 40), b, memory_order_relaxed); }
    if (!g_realtime || g_pin > 0) return;
    int k = nvn_fstep_step(); if (k > 0) { frame_step_store(g_page, k / 60.0); return; }
    double s = frame_mean_add(&g_mean, guest_ns());
    if (s > 0) frame_step_store(g_page, s);
}
#endif
