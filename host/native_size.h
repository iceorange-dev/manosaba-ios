// The setting "Native" (plan items 281 and 301g): the 16:9 picture a screen of w x h pixels holds, reported to the game as its display
// (DM_DISPLAY_SIZE) and rendered at (DM_RENDER_HEIGHT). Its height is the screen's short side or 9/16 of its long side, whichever is less; its
// width the one manosaba's Screen.SetResolution hook gives at that height from the game's 2560x1440 (games/manosaba/graphics.c); both even.
// 0 when the height would be under 360 lines (then the game's own size stays). The iPhone's settings (ios/Sources/main.m) and the Android
// app's (android/settings.c) both take it. Checked by host/tests/t_native_size.c
#pragma once
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
