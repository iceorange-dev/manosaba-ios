// Widening's k and sizes (gfx/nvn/core/widen.h, plan item 320a; S2Arm's numbers): 2868x1320 gives 426 columns over 1920, k 1.221875; 1920 →
// 2346, 1280 → 1564, 1600 → 1955, 160 → 195 (= 1564 >> 3), 1024 → 1252, 640x360 → 782; a 4:3 texture and a square one stay; the Duo's inner
// 2670x1878 gives 270 rows over 1080 (1350), 720 → 900, its width unchanged; a 16:9 screen and one within 1 % of it widen nothing. Always the even
// count (-DDM_TEST_WIDEN_EVEN_ONLY): 2 fail.
#include <stdio.h>
#include "../nvn/core/widen.h"
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("  %s %s\n", ok ? "✓" : "✗", what); }
int main(void) {
    widen_t p = widen_for_screen(2868, 1320);
    check("2868x1320: 426 columns over 1920, k 1.221875, no rows", p.extra_w == 426 && p.k == 1.221875 && p.extra_h == 0 && p.ky == 1.0);
    check("... 1920x1080 → 2346, 1280x720 → 1564 (whole: w·426 / 1920)", widen_width(&p, 1920, 1080) == 2346 && widen_width(&p, 1280, 720) == 1564);
    check("... 1600x900 → 1955 (whole for 1600 itself) [-DDM_TEST_WIDEN_EVEN_ONLY: 1956]", widen_width(&p, 1600, 900) == 1955);
    check("... 160x90 → 195, = 1564 >> 3 (whole for 320, so the mip chain lines up) [-DDM_TEST_WIDEN_EVEN_ONLY: 196]", widen_width(&p, 160, 90) == 195 && (1564 >> 3) == 195);
    check("... 1024x576 → 1252 (never whole: the nearest even count), 640x360 → 782", widen_width(&p, 1024, 576) == 1252 && widen_width(&p, 640, 360) == 782);
    check("... a 4:3 texture and a square one stay; heights stay", widen_width(&p, 1024, 768) == 1024 && widen_width(&p, 512, 512) == 512 && widen_height(&p, 1280, 720) == 720);
    widen_t d = widen_for_screen(2670, 1878);
    check("the Duo's inner 2670x1878 (1.42): 270 rows over 1080, 720 → 900, 1080 → 1350, widths stay", d.extra_h == 270 && d.extra_w == 0 && widen_height(&d, 1280, 720) == 900 &&
          widen_height(&d, 1920, 1080) == 1350 && widen_width(&d, 1280, 720) == 1280);
    widen_t s = widen_for_screen(1920, 1080), t = widen_for_screen(1935, 1080);
    check("a 16:9 screen and one within 1 % widen nothing", s.extra_w == 0 && s.extra_h == 0 && t.extra_w == 0 && widen_width(&t, 1280, 720) == 1280);
    { int c[4] = {0, 0, 1280, 720}, o[4], f[4] = {0, 0, 1920, 1080}, of[4]; widen_map_rect(c, 2346.0 / 1920, 1, 2346, 1080, o); widen_map_rect(f, 2346.0 / 1920, 1, 2346, 1080, of);
      check("the present's crop 1280x720 of a 1920x1080 texture on its 2346x1080 storage: 1564x720; the whole 1920x1080: 2346x1080 [-DDM_TEST_WIDEN_CROP_GAME: as it is]",
            o[0] == 0 && o[1] == 0 && o[2] == 1564 && o[3] == 720 && of[2] == 2346 && of[3] == 1080); }
    printf("t_widen: %d checks, %d failed\n", n, fail); return fail != 0;
}
