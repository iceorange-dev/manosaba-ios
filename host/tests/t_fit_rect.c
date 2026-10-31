// The game picture's place on the screen (host/fit_rect.h, plan item 302, iPhone Duo):
//   no region: the centered rectangle, the same as the iOS front end's letterbox_rect before (its formula copied here), 1% fills the view;
//   the outer display (678x466 points, 16:9 picture 678x381): a camera region over the top-left corner moves the picture down below it at the
//   same size, one over the bottom-right corner moves it up; both at once: the largest smaller picture clear of both; a 16:9 view with no bars:
//   the largest smaller picture beside the region; a region inside a bar, an empty one, one outside the view: nothing moves; a region covering
//   the whole view: the centered picture
// The cutout the iPhone Duo's outer display does not report (dm_known_cutout): the safe area on the left gives the top-left 84x82 and the picture
// goes down to y 82 at its size; on the right the bottom-right one and the picture goes up to y 3; the inner display, another device, both sides
// or none: not known.
// The regions not looked at (-DDM_TEST_FIT_IGNORE_AVOID): 6 fail.
#include "fit_rect.h"
#include <stdio.h>
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-110s %s\n", what, ok ? "✓" : "✗"); }
static const double GA = 1920.0 / 1080;   // manosaba's picture
static int is(dm_rect r, double x, double y, double w, double h) { return r.x == x && r.y == y && r.w == w && r.h == h; }
static int clear(dm_rect r, const dm_rect *av, int k) { for (int i = 0; i < k; i++) if (dm_rect_overlap(r, av[i])) return 0; return 1; }
static int ratio_kept(dm_rect r) { return fabs(r.w / r.h / GA - 1) < 0.006; }
// ios/Sources/front_ios.m letterbox_rect as it was (batch 142)
static dm_rect old_letterbox(dm_rect f) {
    if (f.w <= 0 || f.h <= 0) return f;
    double wa = f.w / f.h; if (fabs(wa / GA - 1) < 0.01) return f;
    double w = f.w, h = f.h; if (wa > GA) w = floor(h * GA); else h = floor(w / GA);
    return (dm_rect){f.x + floor((f.w - w) / 2), f.y + floor((f.h - h) / 2), w, h};
}
int main(void) {
    dm_rect o;
    { static const double sz[][2] = {{678, 466}, {951, 669}, {956, 440}, {932, 430}, {1366, 1024}, {1280, 720}, {1290, 720}, {320, 568}, {0, 0}};
      int same = 1; for (unsigned i = 0; i < sizeof sz / sizeof sz[0]; i++) { dm_rect b = {0, 0, sz[i][0], sz[i][1]}, c = dm_fit_center(b, GA), p = old_letterbox(b);
          same &= is(c, p.x, p.y, p.w, p.h) && dm_fit_rect(b, GA, NULL, 0, &o) == 0 && is(o, p.x, p.y, p.w, p.h); }
      check("no region: the centered rectangle, as letterbox_rect placed it (outer, inner, iPhone Air, 17, iPad, 16:9, 1% off, portrait, empty)", same); }
    dm_rect outer = {0, 0, 678, 466};
    check("the outer display with no region: 678x381 at (0, 42), returned 0", dm_fit_rect(outer, GA, NULL, 0, &o) == 0 && is(o, 0, 42, 678, 381));
    { dm_rect b = {0, 0, 1290, 720}; check("a view 0.8% wider than 16:9 is filled", dm_fit_rect(b, GA, NULL, 0, &o) == 0 && is(o, 0, 0, 1290, 720)); }
    { dm_rect cam = {31.5, 36.5, 33, 33}; int k = dm_fit_rect(outer, GA, &cam, 1, &o);
      check("the camera over the top-left corner (y 36.5-69.5): the same 678x381 moved down to y 70, clear of it, returned 1", k == 1 && is(o, 0, 70, 678, 381) && clear(o, &cam, 1)); }
    { dm_rect cam = {613, 396, 34, 34}; int k = dm_fit_rect(outer, GA, &cam, 1, &o);
      check("the camera over the bottom-right corner (turned the other way): moved up to y 15, clear of it, returned 1", k == 1 && is(o, 0, 15, 678, 381) && clear(o, &cam, 1)); }
    { dm_rect cam[2] = {{31, 36, 34, 34}, {613, 396, 34, 34}}; int k = dm_fit_rect(outer, GA, cam, 2, &o);
      check("both corners: no place in the bars clears both; the largest smaller one, 613x344 at (65, 52), clear of both, ratio kept, returned 2",
            k == 2 && is(o, 65, 52, 613, 344) && clear(o, cam, 2) && ratio_kept(o)); }
    { dm_rect b = {0, 0, 932, 430}, island = {20, 150, 40, 130}; int k = dm_fit_rect(b, GA, &island, 1, &o);
      check("a region inside a side bar (a 2.17:1 iPhone's island): the centered 764x430 at (84, 0), returned 0", k == 0 && is(o, 84, 0, 764, 430)); }
    { dm_rect b = {0, 0, 640, 360}, cam = {10, 10, 30, 30}; int k = dm_fit_rect(b, GA, &cam, 1, &o);
      check("a 16:9 view, no bars: the largest smaller picture beside the region, 600x337 at (40, 12), clear of it, returned 2",
            k == 2 && is(o, 40, 12, 600, 337) && clear(o, &cam, 1) && ratio_kept(o)); }
    { dm_rect r[2] = {{100, 100, 0, 40}, {700, 10, 30, 30}}; int k = dm_fit_rect(outer, GA, r, 2, &o);
      check("an empty region and one outside the view: nothing moves, returned 0", k == 0 && is(o, 0, 42, 678, 381)); }
    { dm_rect all = {-10, -10, 700, 500}; int k = dm_fit_rect(outer, GA, &all, 1, &o);
      check("a region covering the whole view: no area beside it, the centered picture, returned 0", k == 0 && is(o, 0, 42, 678, 381)); }
    { dm_rect c; int k = dm_known_cutout("iPhone19,4", 1398, 2034, 678, 466, 84, 0, &c), m = dm_fit_rect(outer, GA, &c, 1, &o);
      check("the Duo's outer display, safe area on the left: the cutout 84x82 at the top-left; the picture 678x381 down to y 82, returned 1",
            k == 1 && is(c, 0, 0, 84, 82) && m == 1 && is(o, 0, 82, 678, 381) && clear(o, &c, 1)); }
    { dm_rect c; int k = dm_known_cutout("iPhone19,4", 1398, 2034, 678, 466, 0, 84, &c), m = dm_fit_rect(outer, GA, &c, 1, &o);
      check("... safe area on the right (turned the other way): 84x82 at the bottom-right; the picture up to y 3, returned 1",
            k == 1 && is(c, 594, 384, 84, 82) && m == 1 && is(o, 0, 3, 678, 381) && clear(o, &c, 1)); }
    { dm_rect c; int k1 = dm_known_cutout("iPhone19,4", 2007, 2853, 951, 669, 0, 0, &c), k2 = dm_known_cutout("iPhone18,4", 1260, 2736, 956, 440, 62, 62, &c),
          k3 = dm_known_cutout("iPhone19,4", 1398, 2034, 678, 466, 84, 84, &c), k4 = dm_known_cutout("iPhone19,4", 1398, 2034, 678, 466, 0, 0, &c);
      check("not known: the inner display, an iPhone Air, both sides, no side", !k1 && !k2 && !k3 && !k4); }
    printf("%d checks, %d failed\n", n, fail);
    return fail != 0;
}
