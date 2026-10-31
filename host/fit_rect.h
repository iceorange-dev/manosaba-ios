// Where the game picture goes on the screen (plan item 302, iPhone Duo): the largest rectangle of the game's aspect ratio centered in the view,
// as the iOS front end always placed it; and when that one overlaps a region the system reserves for a camera (UIKit's reservedRegionsOfKind:
// occlusion, iOS 27.1: on the iPhone Duo's outer display, 1.46:1, the front camera's cutout lies inside a 16:9 picture's top corner), the
// largest one of the same ratio that overlaps none of them, moved as little as possible from the centered place: within the bars at the same
// size when that clears them, smaller only when no place does. ios/Sources/front_ios.m lays the game view out with it; checked by
// host/tests/t_fit_rect.c. Points, with the regions widened to whole points and every result in whole points.
#pragma once
#include <math.h>
#include <string.h>

typedef struct { double x, y, w, h; } dm_rect;

static inline int dm_rect_overlap(dm_rect a, dm_rect b) {   // a shared area (touching edges do not count)
    return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 && a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// The largest rectangle of ratio ga (width / height) centered in b; a difference under 1% fills b
static inline dm_rect dm_fit_center(dm_rect b, double ga) {
    if (b.w <= 0 || b.h <= 0 || ga <= 0) return b;
    double wa = b.w / b.h;
    if (fabs(wa / ga - 1) < 0.01) return b;
    double w = b.w, h = b.h;
    if (wa > ga) w = floor(h * ga); else h = floor(w / ga);
    return (dm_rect){b.x + floor((b.w - w) / 2), b.y + floor((b.h - h) / 2), w, h};
}

#define DM_FIT_MAX_AVOID 4   // regions taken into account (the Duo's outer display reports two); the rest are left out

// *out = where the picture goes. Returns 0 for the centered rectangle (no region overlaps it), 1 when it was moved at the same size, 2 when it
// had to be made smaller. Each region overlapping b leaves the picture one of four sides (above, below, left, right of it); every choice of sides
// gives an area, the largest picture of the ratio fits it at the place nearest the centered one; the largest of those wins, then the nearest
static inline int dm_fit_rect(dm_rect b, double ga, const dm_rect *avoid, int n, dm_rect *out) {
    dm_rect c = dm_fit_center(b, ga); *out = c;
#ifdef DM_TEST_FIT_IGNORE_AVOID   // negative control of t_fit_rect only: the regions are not looked at (the picture stays centered over them)
    n = 0;
#endif
    dm_rect av[DM_FIT_MAX_AVOID]; int m = 0, hit = 0;
    for (int i = 0; i < n && m < DM_FIT_MAX_AVOID; i++) {
        dm_rect r = avoid[i];
        if (!(r.w > 0 && r.h > 0) || !dm_rect_overlap(r, b)) continue;
        double x0 = floor(r.x), y0 = floor(r.y), x1 = ceil(r.x + r.w), y1 = ceil(r.y + r.h);
        av[m] = (dm_rect){x0, y0, x1 - x0, y1 - y0}; hit |= dm_rect_overlap(av[m], c); m++;
    }
    if (!hit || ga <= 0 || b.w <= 0 || b.h <= 0) return 0;
    double cx = c.x + c.w / 2, cy = c.y + c.h / 2, best_area = -1, best_d = 0; dm_rect best = c;
    int combos = 1; for (int i = 0; i < m; i++) combos *= 4;
    for (int k = 0; k < combos; k++) {
        double ax0 = b.x, ay0 = b.y, ax1 = b.x + b.w, ay1 = b.y + b.h;
        for (int i = 0, s = k; i < m; i++, s /= 4) {
            dm_rect r = av[i];
            switch (s % 4) {
            case 0: ay1 = fmin(ay1, r.y); break;         // above it
            case 1: ay0 = fmax(ay0, r.y + r.h); break;   // below it
            case 2: ax1 = fmin(ax1, r.x); break;         // left of it
            default: ax0 = fmax(ax0, r.x + r.w); break;  // right of it
            }
        }
        ax0 = ceil(ax0); ay0 = ceil(ay0); ax1 = floor(ax1); ay1 = floor(ay1);
        double aw = ax1 - ax0, ah = ay1 - ay0;
        if (aw <= 0 || ah <= 0) continue;
        double w = fmin(aw, floor(ah * ga)), h = floor(w / ga);
        if (w <= 0 || h <= 0) continue;
        double x = fmin(fmax(round(cx - w / 2), ax0), ax1 - w), y = fmin(fmax(round(cy - h / 2), ay0), ay1 - h);
        double area = w * h, d = (x + w / 2 - cx) * (x + w / 2 - cx) + (y + h / 2 - cy) * (y + h / 2 - cy);
        if (area > best_area || (area == best_area && d < best_d)) { best_area = area; best_d = d; best = (dm_rect){x, y, w, h}; }
    }
    if (best_area < 0) return 0;   // no area left beside the regions: the centered picture
    *out = best;
    return best.w == c.w && best.h == c.h ? 1 : 2;
}

// The front camera's cutout where the system reports none (plan item 302): on the iPhone Duo (iPhone19,4) the outer display, 1398x2034 pixels,
// gave no reserved region on the device (iOS 27.1 24A94425), while its Simulator gave the camera's corner, 84x82 points with its margins (the camera
// alone 37x37 at (29, 29)). Its side is the one the safe area insets name (84 points): the user saw the cutout at the top of the left side in
// interface orientation 3 and at the bottom of the right side in 4. sw x sh: the screen's pixels; w x h: the view's points; left, right: its safe
// area's side insets. 0 when not known (another device or screen, or no single side)
static inline int dm_known_cutout(const char *machine, unsigned sw, unsigned sh, double w, double h, double left, double right, dm_rect *out) {
    if (!machine || strcmp(machine, "iPhone19,4") || !((sw == 1398 && sh == 2034) || (sw == 2034 && sh == 1398))) return 0;
    if (left > 0 && right <= 0) { *out = (dm_rect){0, 0, 84, 82}; return 1; }
    if (right > 0 && left <= 0) { *out = (dm_rect){w - 84, h - 82, 84, 82}; return 1; }
    return 0;
}
