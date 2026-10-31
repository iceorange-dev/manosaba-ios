// host/native_size.h (plan items 281 and 301g): the setting "Native"'s 16:9 picture for a screen's pixels. The Redmi K100 Pro Max's 2608x1200
// (wider than 16:9: the short side decides, 2132x1200), the iPhone Air's 2736x1260, a 16:9 screen as it is, the same screen given portrait, an
// iPad's 2732x2048 (4:3: 9/16 of the long side decides), a screen too small (0: the game's size stays).
// dm_native_fit (plan item 302, the iPhone Duo): the outer display's 1398x2034 and the inner one's 2007x2853 within window textures made at the
// inner one's 2852x1604 (as they are), the inner display within textures made at the outer one's 2034x1144 (that size), within textures narrower
// than 16:9 at its height (the width decides, 1998x1124), a screen too small (0).
// The long side's 9/16 alone (-DDM_TEST_NATIVE_LONG_SIDE): the screens wider than 16:9 overflow their short side, 3 fail. The screen's size whatever
// the textures hold (-DDM_TEST_NATIVE_FIT_UNCLAMPED): 2 fail. dm_native_other_screen: the iPhone Duo (iPhone19,4) gives its inner display, 2007x2853;
// the iPhone Air (iPhone18,4) nothing.
#include <stdio.h>
#include "native_size.h"

int main(void) {
    static const struct { unsigned w, h, ok, W, H; const char *what; } t[] = {
        {2608, 1200, 1, 2132, 1200, "Redmi K100 Pro Max 2608x1200"}, {2736, 1260, 1, 2240, 1260, "iPhone Air 2736x1260"},
        {1920, 1080, 1, 1920, 1080, "a 16:9 screen 1920x1080"}, {1200, 2608, 1, 2132, 1200, "the K100 given portrait, 1200x2608"},
        {2732, 2048, 1, 2730, 1536, "iPad 2732x2048 (4:3)"}, {600, 300, 0, 0, 0, "600x300: too small"}};
    int n = 0, fail = 0;
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        unsigned W = 0, H = 0; int ok = dm_native_size(t[i].w, t[i].h, &W, &H);
        int good = ok == (int)t[i].ok && (!ok || (W == t[i].W && H == t[i].H)); n++; fail += !good;
        printf("%-60s %ux%u (want %ux%u) %s\n", t[i].what, ok ? W : 0, ok ? H : 0, t[i].W, t[i].H, good ? "✓" : "✗");
    }
    static const struct { unsigned w, h, dw, dh, ok, W, H; const char *what; } f[] = {
        {1398, 2034, 2852, 1604, 1, 2034, 1144, "fit: the Duo's outer 1398x2034 in textures of 2852x1604"},
        {2007, 2853, 2852, 1604, 1, 2852, 1604, "fit: the Duo's inner 2007x2853 in textures of 2852x1604"},
        {2007, 2853, 2034, 1144, 1, 2034, 1144, "fit: the inner 2007x2853 in textures of 2034x1144 (started closed)"},
        {2007, 2853, 2000, 1604, 1, 1998, 1124, "fit: the inner in textures of 2000x1604 (the width decides)"},
        {600, 300, 2852, 1604, 0, 0, 0, "fit: 600x300 too small"}};
    for (unsigned i = 0; i < sizeof f / sizeof *f; i++) {
        unsigned W = 0, H = 0; int ok = dm_native_fit(f[i].w, f[i].h, f[i].dw, f[i].dh, &W, &H);
        int good = ok == (int)f[i].ok && (!ok || (W == f[i].W && H == f[i].H)); n++; fail += !good;
        printf("%-60s %ux%u (want %ux%u) %s\n", f[i].what, ok ? W : 0, ok ? H : 0, f[i].W, f[i].H, good ? "✓" : "✗");
    }
    { unsigned w = 0, h = 0; int k = dm_native_other_screen("iPhone19,4", &w, &h), k2 = dm_native_other_screen("iPhone18,4", &w, &h); n++;
      int good = k == 1 && w == 2007 && h == 2853 && k2 == 0; fail += !good;
      printf("%-60s %s\n", "other screen: the iPhone Duo's inner 2007x2853; the iPhone Air none", good ? "✓" : "✗"); }
    printf("t_native_size: %d checks, %d failed\n", n, fail);
    return fail != 0;
}
