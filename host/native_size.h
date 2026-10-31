// The setting "Native" (plan items 281 and 301g): the 16:9 picture a screen of w x h pixels holds, reported to the game as its display
// (DM_DISPLAY_SIZE) and rendered at (DM_RENDER_HEIGHT). Its height is the screen's short side or 9/16 of its long side, whichever is less; its
// width the one manosaba's Screen.SetResolution hook gives at that height from the game's 2560x1440 (games/manosaba/graphics.c); both even.
// 0 when the height would be under 360 lines (then the game's own size stays). The iPhone's settings (ios/Sources/main.m) and the Android
// app's (android/settings.c) both take it. Checked by host/tests/t_native_size.c
#pragma once
#include <string.h>
static inline int dm_native_size(unsigned w, unsigned h, unsigned *W, unsigned *H) {
    unsigned lng = w > h ? w : h, sht = w > h ? h : w, nh = lng * 9 / 16;
#ifndef DM_TEST_NATIVE_LONG_SIDE   // negative control of t_native_size only: the long side's 9/16 alone (a screen wider than 16:9 then overflows)
    if (sht < nh) nh = sht;
#else
    (void)sht;
#endif
    nh &= ~1u; if (nh < 360) return 0;
    *H = nh; *W = ((2560u * nh + 720u) / 1440u) & ~1u;
    return 1;
}

// Plan item 302 (the iPhone Duo: the game's window moves between two displays while it runs): dm_native_size of a screen of w x h pixels, at most
// the display the game made its window textures at (dw x dh, the size reported to it at boot): a larger picture would reach only their
// top-left part. 0 when it would be under 360 lines. games/manosaba/graphics.c renders at it when the window changes screens
static inline int dm_native_fit(unsigned w, unsigned h, unsigned dw, unsigned dh, unsigned *W, unsigned *H) {
    unsigned nw, nh; if (!dm_native_size(w, h, &nw, &nh)) return 0;
#ifndef DM_TEST_NATIVE_FIT_UNCLAMPED   // negative control of t_native_size only: the screen's own size whatever the textures hold
    if (nh > dh) nh = dh & ~1u;
    if (((2560u * nh + 720u) / 1440u & ~1u) > dw) nh = (dw * 1440u / 2560u) & ~1u;
#else
    (void)dw; (void)dh;
#endif
    if (nh < 360) return 0;
    *H = nh; *W = ((2560u * nh + 720u) / 1440u) & ~1u;
    return 1;
}

// A device's other screen, measured (plan item 302): the iPhone Duo (iPhone19,4) closed shows the game on its outer display, 1398x2034 pixels, and
// the system lists only that one, while its inner display is 2007x2853 (iOS 27.1 24A94425, 2026-11-01). Its first launch closed takes the inner one
// for the window textures this way, before the front end saw it (ios/Sources/main.m). 0 for a device not listed
static inline int dm_native_other_screen(const char *machine, unsigned *w, unsigned *h) {
    if (machine && !strcmp(machine, "iPhone19,4")) { *w = 2007; *h = 2853; return 1; }
    return 0;
}
