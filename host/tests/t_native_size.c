// host/native_size.h (plan items 281 and 301g): the setting "Native"'s 16:9 picture for a screen's pixels. The Redmi K100 Pro Max's 2608x1200
// (wider than 16:9: the short side decides, 2132x1200), the iPhone Air's 2736x1260, a 16:9 screen as it is, the same screen given portrait, an
// iPad's 2732x2048 (4:3: 9/16 of the long side decides), a screen too small (0: the game's size stays).
// The long side's 9/16 alone (-DDM_TEST_NATIVE_LONG_SIDE): the screens wider than 16:9 overflow their short side, 3 fail.
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
    printf("t_native_size: %d checks, %d failed\n", n, fail);
    return fail != 0;
}
