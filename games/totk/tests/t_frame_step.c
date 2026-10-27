// The host half of the real-time frame step (games/totk/frame_step.c): the mean of the present intervals and the page layout; the scene size the
// page carries at +16 (DM_RENDER_RES or the game's own for the operation mode, plan item 52). Built and run by
// games/totk/check_frame_rate.py (clang -DFRAME_STEP_TEST -I host). The heap overlay's note ignored (-DDM_TEST_SCENE_NO_HEAP) fails 1; +8 not
// written (-DDM_TEST_CLOTH_NO_SECONDS) fails 1. The output size at +24 / +32 (DM_OUTPUT_RES, plan item 63); the canvas left to the game
// (-DDM_TEST_OUTPUT_NO_CANVAS) fails 3. DM_RENDER_HEIGHT at 16:9 when DM_RENDER_RES is unset (plan item 62l); ignored (-DDM_TEST_SCENE_NO_HEIGHT)
// fails 3. The mean of the intervals before the 1/120 floor (plan item 250); each interval clamped first (-DDM_TEST_FSTEP_CLAMP_EACH) fails 3.
// A live scene size (plan item 240, scene_latch_step / scene_live_size): the mode-change path not asked for (-DDM_TEST_SCENE_LATCH_NO_KICK) fails
// 2; +56 left set after the pick (-DDM_TEST_SCENE_LATCH_NO_CLEAR) fails 3. The iOS setting "Native" (plan item 281): DM_RENDER_HEIGHT's lines at
// most the largest 16:9 the heap holds (asked as they are, -DDM_TEST_SCENE_HEIGHT_NO_FIT, fails 2); the output from DM_DISPLAY_SIZE, fitted the
// same way (ignored, -DDM_TEST_OUTPUT_NO_DISPLAY, fails 3); docked when a display size is reported and DM_OPMODE is unset (the mode left as it was,
// -DDM_TEST_NATIVE_HANDHELD, fails 1).
#include <stdio.h>
#include <math.h>
#define FRAME_STEP_TEST 1
#include "../frame_step.c"

static int n, fail;
static void expect(const char *what, double want, double got, double tol) {
    n++; int ok = fabs(want - got) <= tol; fail += !ok;
    printf("%-88s expected %-12.6f got %-12.6f %s\n", what, want, got, ok ? "✓" : "✗");
}
int main(void) {
    frame_mean m = {0}; uint64_t t = 1000000000ull; double s = 0;
    expect("first present: no interval yet, the step stays", 0, frame_mean_add(&m, t), 0);
    for (int i = 0; i < 10; i++) s = frame_mean_add(&m, t += 11111111);           // 90 fps
    expect("steady 90 fps: 1/90 s", 1.0 / 90, s, 1e-6);
    for (int i = 0; i < 5; i++) s = frame_mean_add(&m, t += 33333333);            // 30 fps: five intervals replace the window
    expect("five 30 fps intervals: 1/30 s", 1.0 / 30, s, 1e-6);
    s = frame_mean_add(&m, t += 16666667);
    expect("one 60 fps interval after them: mean of the last five", (4 / 30.0 + 1 / 60.0) / 5, s, 1e-6);
    s = frame_mean_add(&m, t += 5000000);                                           // 200 fps
    expect("an interval below 1/120 s counts as it is in the mean (plan item 250)", (3 / 30.0 + 1 / 60.0 + 0.005) / 5, s, 1e-6);
    s = frame_mean_add(&m, t += 200000000);                                         // 5 fps, below 0.25 s
    expect("... and one above 1/15 s too", (2 / 30.0 + 1 / 60.0 + 0.005 + 0.2) / 5, s, 1e-6);
    expect("a pause above 0.25 s keeps the step", 0, frame_mean_add(&m, t += 400000000), 0);
    expect("... and restarts the mean", 1.0 / 60, frame_mean_add(&m, t += 16666667), 1e-6);
    {   frame_mean a = {0}; uint64_t u = 1000000000ull; double st = 0; frame_mean_add(&a, u);   // presents 6 and 11 ms apart in turn, 8.5 on average
        for (int i = 0; i < 10; i++) st = frame_mean_add(&a, u += i & 1 ? 11000000 : 6000000);
        expect("6 and 11 ms in turn: the mean of the last five, 9.0 ms [-DDM_TEST_FSTEP_CLAMP_EACH: 9.9]", (3 * 0.011 + 2 * 0.006) / 5, st, 1e-6);
        frame_mean b = {0}; u = 1000000000ull; frame_mean_add(&b, u); for (int i = 0; i < 6; i++) st = frame_mean_add(&b, u += 5000000);
        expect("five at 5 ms: the mean kept at 1/120 s", 1.0 / 120, st, 1e-6);
        frame_mean c = {0}; u = 1000000000ull; frame_mean_add(&c, u); for (int i = 0; i < 6; i++) st = frame_mean_add(&c, u += 100000000);
        expect("five at 100 ms: the mean kept at 1/15 s", 1.0 / 15, st, 1e-6); }
    _Atomic uint64_t page[2] = {0}; frame_step_store(page, 1.0 / 90);
    uint64_t bits = atomic_load(&page[0]), b2 = atomic_load(&page[1]); float v[3]; memcpy(v, &bits, 8); memcpy(&v[2], &b2, 4);
    expect("page +0: ratio = seconds x 30", 1.0 / 3, v[0], 1e-6);
    expect("page +4: ticks = seconds x 60", 2.0 / 3, v[1], 1e-6);
    expect("page +8: seconds (the cloth and hair clock, plan item 163)", 1.0 / 90, v[2], 1e-6);
    float sz[2]; int r;
    r = scene_size(NULL, 1, NULL, sz); expect("scene size unset, docked: the game's 1600 ...", 1600, sz[0], 0); expect("... x 900, its own (0)", 900 * 10 + 0, sz[1] * 10 + r, 0);
    r = scene_size("", 0, NULL, sz); expect("unset, handheld: 1280 ...", 1280, sz[0], 0); expect("... x 720, its own (0)", 720 * 10 + 0, sz[1] * 10 + r, 0);
    r = scene_size("0", 1, NULL, sz); expect("DM_RENDER_RES=0: the game's own (0)", 1600 * 10 + 0, sz[0] * 10 + r, 0);
    r = scene_size("960x540", 1, NULL, sz); expect("960x540: width", 960, sz[0], 0); expect("... height, from the variable (1)", 540 * 10 + 1, sz[1] * 10 + r, 0);
    r = scene_size("1920x1080", 0, NULL, sz); expect("1920x1080, the largest: from the variable (1)", 1080 * 10 + 1, sz[1] * 10 + r, 0);
    const char *bad[] = {"960x", "32x32", "5000x100", "960x540x", "abc", "2560x1440", "1921x1080", "1920x1081"};
    for (int i = 0; i < 8; i++) { char what[64]; r = scene_size(bad[i], 0, NULL, sz); snprintf(what, sizeof what, "%s: refused (-1), the game's own 1280", bad[i]);
        expect(what, 1280 * 10 - 1, sz[0] * 10 + r, 0); }
    // The heap overlay's note (games/totk/heapdef.py, plan item 95) raises the largest size to its own, never above 3840x2160
    { char b[32]; const char *w;
      w = scene_env(NULL, "540", NULL, b, sizeof b); r = scene_size(w, 0, NULL, sz); expect("DM_RENDER_HEIGHT=540 alone: 960 ...", 960, sz[0], 0); expect("... x 540, taken (1)", 540 * 10 + 1, sz[1] * 10 + r, 0);
      w = scene_env(NULL, "900", NULL, b, sizeof b); r = scene_size(w, 0, NULL, sz); expect("DM_RENDER_HEIGHT=900 alone: 1600x900 (width 1600)", 1600, sz[0], 0);
      w = scene_env("1280x720", "540", NULL, b, sizeof b); r = scene_size(w, 0, NULL, sz); expect("DM_RENDER_RES=1280x720 wins over DM_RENDER_HEIGHT=540", 1280, sz[0], 0);
      w = scene_env(NULL, "0", NULL, b, sizeof b); expect("DM_RENDER_HEIGHT=0: nothing asked (the game's own)", 1, w == NULL, 0);
      w = scene_env(NULL, "720p", NULL, b, sizeof b); expect("DM_RENDER_HEIGHT=720p: not a number, nothing asked", 1, w == NULL, 0);
      // The iOS setting "Native" (plan item 281): the screen's lines, at most the largest 16:9 the heap holds
      w = scene_env(NULL, "1260", NULL, b, sizeof b); r = scene_size(w, 0, NULL, sz);
      expect("Native on an iPhone Air (1260 lines), no overlay: 1920x1080, taken [-DDM_TEST_SCENE_HEIGHT_NO_FIT: refused]", 1, sz[0] == 1920 && sz[1] == 1080 && r == 1, 0);
      w = scene_env(NULL, "1260", "2560x1440", b, sizeof b); r = scene_size(w, 0, "2560x1440", sz);
      expect("1260 lines with an overlay for 2560x1440: 2240x1260, taken", 1, sz[0] == 2240 && sz[1] == 1260 && r == 1, 0);
      w = scene_env(NULL, "1548", "2560x1440", b, sizeof b); r = scene_size(w, 0, "2560x1440", sz);
      expect("1548 lines (a 13-inch iPad) with an overlay for 2560x1440: 2560x1440 [-DDM_TEST_SCENE_HEIGHT_NO_FIT: refused]", 1, sz[0] == 2560 && sz[1] == 1440 && r == 1, 0);
      w = scene_env(NULL, "750", NULL, b, sizeof b); expect("750 lines (below the heap's): 1334x750 as asked", 1, w && !strcmp(w, "1334x750"), 0); }
    r = scene_size("2560x1440", 1, "2560x1440", sz); expect("2560x1440 with an overlay for 2560x1440: taken (1)", 1440 * 10 + 1, sz[1] * 10 + r, 0);
    r = scene_size("2560x1440", 1, "1920x1080", sz); expect("2560x1440 with an overlay for 1920x1080: refused (-1)", 1600 * 10 - 1, sz[0] * 10 + r, 0);
    r = scene_size("3200x1800", 1, "2560x1440", sz); expect("3200x1800 above the overlay's 2560x1440: refused (-1)", 1600 * 10 - 1, sz[0] * 10 + r, 0);
    r = scene_size("2560x1440", 1, "9999x9999", sz); expect("a note above 3840x2160 is ignored: 2560x1440 refused (-1)", 1600 * 10 - 1, sz[0] * 10 + r, 0);
    r = scene_size("1920x1080", 1, "junk", sz); expect("a note that is not a size: the output size still allowed (1)", 1080 * 10 + 1, sz[1] * 10 + r, 0);
    // The output size: {swapchain, canvas}, width | height << 32; the canvas 0 (the game's own) unless DM_OUTPUT_RES
    uint64_t o[2]; const uint64_t hd = (uint64_t)1080 << 32 | 1920, qhd = (uint64_t)1440 << 32 | 2560, hd7 = (uint64_t)720 << 32 | 1280;
    r = output_size(NULL, NULL, o); expect("output unset: the swapchain the game's 1920x1080, the canvas 0 (its own) (0)", 1, o[0] == hd && !o[1] && !r, 0);
    r = output_size("0", "2560x1440", o); expect("DM_OUTPUT_RES=0: the game's own (0)", 1, o[0] == hd && !o[1] && !r, 0);
    r = output_size("1280x720", NULL, o); expect("1280x720: swapchain and canvas 1280x720 (1)", 1, o[0] == hd7 && o[1] == hd7 && r == 1, 0);
    r = output_size("1920x1080", NULL, o); expect("1920x1080 (the largest without the overlay): the canvas 1920x1080 too (1)", 1, o[0] == hd && o[1] == hd && r == 1, 0);
    r = output_size("2560x1440", "2560x1440", o); expect("2560x1440 with an overlay for 2560x1440: both 2560x1440 (1)", 1, o[0] == qhd && o[1] == qhd && r == 1, 0);
    const char *obad[] = {"2560x1440", "639x360", "640x359", "1920x", "1920x1080x"};
    for (int i = 0; i < 5; i++) { char what[96]; r = output_size(obad[i], NULL, o); snprintf(what, sizeof what, "output %s without an overlay: refused (-1), the game's own", obad[i]);
        expect(what, 1, o[0] == hd && !o[1] && r == -1, 0); }
    r = output_size("2560x1440", "1920x1080", o); expect("2560x1440 with an overlay for 1920x1080: refused (-1)", 1, o[0] == hd && !o[1] && r == -1, 0);
    r = output_size("3840x2160", "9999x9999", o); expect("a note above 3840x2160 is ignored: 3840x2160 refused (-1)", 1, !o[1] && r == -1, 0);
    expect("Native (a display size, DM_OPMODE unset): docked [-DDM_TEST_NATIVE_HANDHELD: as it was]", 1, native_docked("2240x1260", NULL), 0);
    expect("Native with DM_OPMODE set: the mode it says", 0, native_docked("2240x1260", "0"), 0);
    expect("no display size: the mode as it was", 0, native_docked(NULL, NULL) + native_docked("", NULL), 0);
    // The output from the display the iOS setting "Native" reports (DM_DISPLAY_SIZE, plan item 281), when DM_OUTPUT_RES is unset
    { char b[32]; const char *w; const uint64_t air = (uint64_t)1260 << 32 | 2240;
      w = output_env(NULL, "2240x1260", NULL, b, sizeof b); r = output_size(w, NULL, o);
      expect("Native on an iPhone Air, no overlay: swapchain and canvas 1920x1080 [-DDM_TEST_OUTPUT_NO_DISPLAY: the game's own]", 1, o[0] == hd && o[1] == hd && r == 1, 0);
      w = output_env(NULL, "2240x1260", "2560x1440", b, sizeof b); r = output_size(w, "2560x1440", o);
      expect("... with an overlay for 2560x1440: both 2240x1260 [-DDM_TEST_OUTPUT_NO_DISPLAY: the game's own]", 1, o[0] == air && o[1] == air && r == 1, 0);
      w = output_env("1280x720", "2240x1260", NULL, b, sizeof b); r = output_size(w, NULL, o);
      expect("DM_OUTPUT_RES=1280x720 wins over DM_DISPLAY_SIZE", 1, o[0] == hd7 && o[1] == hd7 && r == 1, 0);
      w = output_env(NULL, "1334x750", NULL, b, sizeof b); expect("a 1334x750 display: 1334x750 as reported [-DDM_TEST_OUTPUT_NO_DISPLAY: nothing]", 1, w && !strcmp(w, "1334x750"), 0);
      w = output_env(NULL, "2240x", NULL, b, sizeof b); expect("a display size that is not <w>x<h>: nothing asked", 1, w == NULL, 0);
      w = output_env(NULL, NULL, NULL, b, sizeof b); expect("no display size: nothing asked (the game's own)", 1, w == NULL, 0); }
    // A live scene size (plan item 240): the page, the renderer's cached mode index (+0xcd0) and the flags the cave ORs in (+56)
    {   _Alignas(16) static uint8_t pg[64]; _Alignas(16) static uint8_t rend[0x1000]; float f[2]; uint32_t k; int st;
        const uint64_t s720 = (uint64_t)720 << 32 | 1280, s540 = (uint64_t)540 << 32 | 960;
        scene_latch sl = {.latched = s720}; rend[0xcd0] = 1;
        st = scene_latch_step(&sl, pg, s720); expect("latch: the boot size again: nothing done (0)", 0, st, 0);
        st = scene_latch_step(&sl, pg, s540); memcpy(f, pg + 16, 8); memcpy(&k, pg + 56, 4);
        expect("latch, no renderer yet (+48 = 0): 960x540 on the page, the first pick takes it (SCENE_NO_RENDERER)", 1, st == SCENE_NO_RENDERER && f[0] == 960 && f[1] == 540 && !k, 0);
        uint64_t rp = (uint64_t)(uintptr_t)rend; memcpy(pg + 48, &rp, 8);
        st = scene_latch_step(&sl, pg, s720); memcpy(f, pg + 16, 8); memcpy(&k, pg + 56, 4);
        expect("latch, 1280x720 asked with the renderer at +48: on the page, written (SCENE_WROTE)", 1, st == SCENE_WROTE && f[0] == 1280 && f[1] == 720, 0);
        expect("... the cached mode index -1 and +56 = 0x80 (the mode-change path) [-DDM_TEST_SCENE_LATCH_NO_KICK: +56 0]", 1, (int8_t)rend[0xcd0] == -1 && k == 0x80, 0);
        st = scene_latch_step(&sl, pg, s720); memcpy(&k, pg + 56, 4); expect("... the next present, no pick yet: nothing done, +56 kept", 1, !st && k == 0x80, 0);
        rend[0xcd0] = 1; st = scene_latch_step(&sl, pg, s720); memcpy(&k, pg + 56, 4);
        expect("... the game picked (index 1): SCENE_PICKED, the renderer noted [-DDM_TEST_SCENE_LATCH_NO_CLEAR: +56 still set]", 1, st == SCENE_PICKED && sl.picked == (uintptr_t)rend && !k, 0);
        st = scene_latch_step(&sl, pg, s720); memcpy(&k, pg + 56, 4); expect("... after it: nothing done, +56 stays 0 [-DDM_TEST_SCENE_LATCH_NO_CLEAR: set]", 1, !st && !k, 0);
        st = scene_latch_step(&sl, pg, s540); int gave = -1;
        for (int i = 1; i <= 600 && gave < 0; i++) if (scene_latch_step(&sl, pg, s540) & SCENE_GAVE_UP) gave = i;
        memcpy(&k, pg + 56, 4); expect("latch: no pick at all: given up at the 600th present after the request, +56 cleared", 1, st == SCENE_WROTE && gave == 600 && !k, 0); }
    {   const uint64_t b720 = (uint64_t)720 << 32 | 1280, b1080 = (uint64_t)1080 << 32 | 1920;
        expect("live size 960x540 (boot 1280x720): taken", 1, scene_live_size(960, 540, "", b720) == ((uint64_t)540 << 32 | 960), 0);
        expect("live size 1600x900 (boot 1280x720): taken (the game's own docked size)", 1, scene_live_size(1600, 900, "", b720) != 0, 0);
        expect("live size 1920x1080 (boot 1280x720): refused, more pixels than 1600x900", 1, scene_live_size(1920, 1080, "", b720) == 0, 0);
        expect("live size 1920x1080 (booted at 1920x1080): taken", 1, scene_live_size(1920, 1080, "", b1080) != 0, 0);
        expect("live size 2560x1440 with the overlay's note 2560x1440: taken", 1, scene_live_size(2560, 1440, "2560x1440", b720) != 0, 0);
        expect("live size 32x32: refused", 1, scene_live_size(32, 32, "", b720) == 0, 0); }
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
