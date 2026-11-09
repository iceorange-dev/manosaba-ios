// On-screen controls (touchpad.c, plan item 58): an 844x390 layout (an iPhone in landscape, points), fingers at chosen times and the game reading
// the controller at 30 fps on a fake clock. BotWRecomp's input checks (touch.cpp plan_check: stick, A with the stick, a camera drag at 250 pt/s,
// resting, release, the base following, the D-pad sliding, several buttons, a second left finger ignored) with their limits, then this host's own:
// a finger keeps its button when it slides off it, a tap between two reads is seen once, the eye collapses the controls, off releases every finger,
// a read after a pause gives the camera no spike, every control's centre reaches its own button. Camera tuning (plan item 109): 2× turns faster than
// 1× for the same swipe, 0.5× still reaches full speed with a fast swipe, a slow drift stays 0 at 3×, each axis inverts alone, out-of-range values
// are clamped, DM_TOUCH_LOOK is read. Negative controls: -DDM_TEST_TP_REHIT (a sliding finger takes the button under it) fails "slid off A";
// -DDM_TEST_TP_SENS_AFTER (the sensitivity scales the saturated output) fails 1: 0.5× never turns at full speed. Layout (plan item 148): a spec moves and
// sizes A (pressed at its new place, not at the old), B pushed off the screen stays inside the safe area, the stick's home moves and shrinks, the
// spec read back gives the same layout, one that does not parse changes nothing, reset; the editor drags and sizes X with nothing reaching the
// game, and X then works there; the menu button asks for the panel once and presses nothing; opacity clamped. The customization ignored
// (-DDM_TEST_TP_CUSTOM_IGNORED): 5 fail. Device sizes and game controllers (plan item 171): ten devices from Touchable's table, each with every
// control inside its safe area, none overlapping, the left column below the iPadOS window controls; a controller connected leaves the controls, a key
// hides them, a touch or the last controller gone shows them. The window controls ignored (-DDM_TEST_TP_NO_WINDOW): 3 fail (the iPads that have
// them); hidden on connect, as before (-DDM_TEST_TP_CTL_ON_CONNECT): 1 fails. The view's redraws (plan item 315e): moving the stick or a camera finger
// leaves tp_visual_sig as it was (they are layers that move), a button pressed changes it and released brings it back, the opacity changes it; the
// places in it (-DDM_TEST_TP_SIG_MOVES): 3 fail (the last places stay in it after the fingers are up).
#include "../front/touchpad.c"
#include <stdio.h>
uint64_t g_t0_ns;
static uint64_t clock_ns = 1000000000ull;
uint64_t now_ns(void) { return clock_ns; }
uint64_t dm_nvn_frame_count(void) { return 0; }
int hle_hid_last(uint64_t *b, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) { *b = 0; *lx = *ly = *rx = *ry = 0; return 0; }
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static double T;                     // test time, seconds
static pad S;                        // what the game read last
static void rd(void) { tp_read(T, &S.b, &S.lx, &S.ly, &S.rx, &S.ry); }
static void run_to(double t) { while (T + 1 / 30.0 <= t + 1e-9) { T += 1 / 30.0; rd(); } T = t; rd(); }   // the game reads every 1/30 s up to t
static float cx(const char *l) { float x, y, r; return tp_find(l, &x, &y, &r) ? x : -1000; }
static float cy(const char *l) { float x, y, r; return tp_find(l, &x, &y, &r) ? y : -1000; }
static void down(long id, const char *l, float dx, float dy) { tp_touch(id, TP_DOWN, cx(l) + dx, cy(l) + dy); }
static void up(long id) { tp_touch(id, TP_UP, 0, 0); }
static pad drag(long id, float speed, float ux, float uy) {   // a camera finger dragged at speed (pt/s) along (ux, uy) for 0.4 s, the last read
    float u = g.u, x0 = 844 * 0.62f, y0 = 390 * 0.5f; tp_touch(id, TP_DOWN, x0, y0); double start = T;
    while (T < start + 0.4 - 1e-9) { T += 1 / 60.0; float d = (float)(T - start) * speed * u; tp_touch(id, TP_MOVE, x0 + ux * d, y0 + uy * d);
        if (((int)lround((T - start) * 60)) % 2 == 0) rd(); }
    pad r = S; up(id); T += 0.2; rd(); return r;
}
#define PAD(name, pred) CHECK(pred(&S), "%-24s buttons %05llx  L %+.2f %+.2f  R %+.2f %+.2f", name, (unsigned long long)S.b, S.lx, S.ly, S.rx, S.ry)

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    tp_layout(844, 390, 47, 0, 47, 21, 0);
    float u, px, py, r; tp_find("stick", &px, &py, &r); u = g.u;
    CHECK(u > 0.97f && u < 0.98f && fabsf(r - 54 * u) < 0.01f, "layout: %.4f points per design point (390 / 400), stick ring %.1f", u, r);

    // BotWRecomp's plan_check, t0 = 1 s
    double t0 = 1; run_to(t0);
    tp_touch(1, TP_DOWN, px, py); run_to(t0 + 0.05); tp_touch(1, TP_MOVE, px + 40 * u, py);
    run_to(t0 + 0.25); PAD("stick right", c_stick_right);
    down(2, "A", 0, 0);
    run_to(t0 + 0.45); PAD("A held + stick", c_a_stick);
    float lx0 = 844 * 0.62f, ly0 = 390 * 0.5f; tp_touch(3, TP_DOWN, lx0, ly0);
    double start = T;
    while (T < start + 0.5 - 1e-9) {   // the finger moves at 60 Hz (touch events), the game reads at 30 Hz
        T += 1 / 60.0; tp_touch(3, TP_MOVE, lx0 + (float)(T - start) * 250 * u, ly0);
        if (((int)lround((T - start) * 60)) % 2 == 0) rd();
        if (fabs(T - (t0 + 0.85)) < 1 / 120.0) PAD("camera drag right", c_cam_right);
    }
    run_to(t0 + 1.35); PAD("camera finger resting", c_cam_rest);
    up(3); run_to(t0 + 1.45); PAD("camera released", c_cam_up);
    up(2); run_to(t0 + 1.55); PAD("A released", c_a_up);
    tp_touch(1, TP_MOVE, px - 160 * u, py); run_to(t0 + 1.75); PAD("stick hard left", c_hard_left);
    tp_touch(1, TP_MOVE, px - 130 * u, py); run_to(t0 + 1.95); PAD("stick base followed", c_followed);
    tp_touch(1, TP_MOVE, px - 106 * u, py - 80 * u); run_to(t0 + 2.10); PAD("stick up", c_stick_up);
    up(1); run_to(t0 + 2.20); PAD("stick released", c_released);
    float dr; { float x, y; tp_find("dpad", &x, &y, &dr); }
    down(4, "dpad", 0, -dr * 0.6f); run_to(t0 + 2.30); PAD("dpad up", c_dpad_up);
    tp_touch(4, TP_MOVE, cx("dpad") + dr * 0.6f, cy("dpad")); run_to(t0 + 2.40); PAD("dpad slid right", c_dpad_right);
    up(4); run_to(t0 + 2.50);
    down(5, "ZR", -30 * u, 10 * u); down(6, "R3", 8 * u, 0); down(7, "L", 0, 0); down(8, "Y", 0, -12 * u);
    run_to(t0 + 2.60); PAD("ZR + R3 + L + Y", c_four_a);
    for (long i = 5; i <= 8; i++) up(i);
    run_to(t0 + 2.70); down(9, "-", 0, 0); down(10, "L3", 0, 0); down(11, "X", 0, 0); down(12, "ZL", 0, 0);
    run_to(t0 + 2.80); PAD("- + L3 + X + ZL", c_four_b);
    for (long i = 9; i <= 12; i++) up(i);
    run_to(t0 + 2.85); down(13, "+", 0, 0); down(14, "R", 0, 0); down(15, "B", 0, 0);
    run_to(t0 + 2.95); PAD("+ + R + B", c_three);
    for (long i = 13; i <= 15; i++) up(i);
    run_to(t0 + 3.05); tp_touch(16, TP_DOWN, px, py); tp_touch(17, TP_DOWN, px + 120 * u, py - 40 * u);
    run_to(t0 + 3.10); tp_touch(16, TP_MOVE, px, py + 40 * u); tp_touch(17, TP_MOVE, px + 200 * u, py - 40 * u);
    run_to(t0 + 3.25); PAD("2nd left finger ignored", c_second_left);
    up(16); up(17); run_to(t0 + 3.35); PAD("all released", c_released);

    // A finger owns its control: from A slid onto Y and past the stick's half, still A (and no stick)
    run_to(6); down(20, "A", 0, 0); run_to(6.1);
    tp_touch(20, TP_MOVE, cx("Y"), cy("Y")); run_to(6.2); tp_touch(20, TP_MOVE, 200, 300); run_to(6.3);
    CHECK(S.b == DM_BTN_A && !S.lx && !S.ly, "slid off A onto Y and the stick's half: A still held, nothing else (%05llx)", (unsigned long long)S.b);
    up(20); run_to(6.4);
    // A tap between two reads is seen once
    run_to(7); T += 0.005; down(21, "B", 0, 0); T += 0.010; up(21); run_to(7 + 1 / 30.0);
    CHECK(S.b == DM_BTN_B, "a 10 ms tap between two reads: B seen at the next read");
    run_to(7 + 2 / 30.0); CHECK(S.b == 0, "and only once");
    // The eye
    run_to(8); down(22, "eye", 0, 0); up(22);
    tp_draw_state d; tp_draw(&d);
    CHECK(tp_hidden() && d.collapsed && d.n == 1 && !strcmp(d.items[0].label, "eye"), "eye: collapsed, only the eye drawn");
    tp_touch(23, TP_DOWN, px, py); tp_touch(23, TP_MOVE, px + 40 * u, py); down(24, "A", 0, 0); run_to(8.2);
    CHECK(S.b == 0 && !S.lx && !S.rx, "collapsed: the stick and A dead");
    up(23); up(24); down(25, "eye", 0, 0); up(25);
    tp_touch(26, TP_DOWN, px, py); tp_touch(26, TP_MOVE, px + 40 * u, py); run_to(8.4);
    CHECK(!tp_hidden() && S.lx > 0.6f, "eye again: shown, the stick works (%.2f)", S.lx);
    // Off (a controller connected) releases every finger; back on, the old finger counts no more
    tp_enable(0); { uint64_t b; float a[4]; CHECK(!tp_read(8.5, &b, &a[0], &a[1], &a[2], &a[3]), "off: nothing read"); }
    tp_draw(&d); CHECK(!d.visible, "off: nothing drawn");
    tp_enable(1); tp_touch(26, TP_MOVE, px + 50 * u, py); T = 8.6; rd();
    CHECK(S.lx == 0 && !S.b, "on again: the finger held from before counts no more");
    // A pause between reads (the game loading): the first read after it gives the camera no spike
    run_to(9); tp_touch(27, TP_DOWN, lx0, ly0); run_to(9.1);
    tp_touch(27, TP_MOVE, lx0 + 300 * u, ly0); T = 9.6; rd();
    CHECK(S.rx == 0, "camera: 300 pt moved during a 0.5 s pause in reads, no spike after it (%.2f)", S.rx);
    up(27); run_to(9.7);
    // Every control's centre reaches its own button; the drawing marks it pressed
    static const struct { const char *l; uint64_t m; } all[] = {{"A", DM_BTN_A}, {"B", DM_BTN_B}, {"X", DM_BTN_X}, {"Y", DM_BTN_Y}, {"ZL", DM_BTN_ZL}, {"L", DM_BTN_L},
        {"ZR", DM_BTN_ZR}, {"R", DM_BTN_R}, {"-", DM_BTN_MINUS}, {"+", DM_BTN_PLUS}, {"L3", DM_BTN_LS}, {"R3", DM_BTN_RS}};
    int ok = 1, inside = 1;
    for (int i = 0; i < 12; i++) {
        float x = cx(all[i].l), y = cy(all[i].l); inside &= x > 47 && x < 844 - 47 && y > 0 && y < 390 - 21;
        down(30 + i, all[i].l, 0, 0); T += 1 / 30.0; rd(); tp_draw(&d);
        int pressed = 0; for (int k = 0; k < d.n; k++) pressed |= !strcmp(d.items[k].label, all[i].l) && d.items[k].pressed;
        if (S.b != all[i].m || !pressed) { ok = 0; printf("    %s: %05llx\n", all[i].l, (unsigned long long)S.b); }
        up(30 + i); T += 1 / 30.0; rd();
    }
    CHECK(ok, "each of the 12 buttons' centres presses that button alone, drawn pressed");
    CHECK(inside, "every button inside the safe area");
    // Laid out again: the same size and safe area keep a held finger (UIKit lays out for other reasons too); a new size releases it
    down(41, "A", 0, 0); T += 1 / 30.0; rd();   // (this read takes the press latch)
    tp_layout(844, 390, 47, 0, 47, 21, 0); T += 1 / 30.0; rd();
    CHECK(S.b == DM_BTN_A, "laid out again unchanged: A still held");
    tp_layout(844, 390, 0, 0, 0, 21, 0); T += 1 / 30.0; rd();
    CHECK(S.b == 0, "a new safe area: every finger released");
    up(41); tp_layout(844, 390, 47, 0, 47, 21, 0); tp_find("stick", &px, &py, &r);
    // Camera tuning
    { T = 12; rd();
      pad a = drag(50, 200, 1, 0); tp_look_tuning(2, 0, 0); pad b = drag(51, 200, 1, 0);
      CHECK(a.rx > 0.2f && b.rx > a.rx + 0.15f && b.rx <= 1, "sensitivity 2×: a 200 pt/s swipe turns faster than at 1× (%.2f → %.2f)", a.rx, b.rx);
      tp_look_tuning(0.5f, 0, 0); pad c = drag(52, 2000, 1, 0);
      CHECK(c.rx > 0.99f, "sensitivity 0.5×: a fast swipe (2000 pt/s) still turns at full speed (%.2f)", c.rx);
      pad c2 = drag(53, 200, 1, 0);
      CHECK(c2.rx > 0.1f && c2.rx < a.rx - 0.1f, "... and a 200 pt/s swipe slower than at 1× (%.2f < %.2f)", c2.rx, a.rx);
      tp_look_tuning(3, 0, 0); pad e = drag(54, 6, 1, 0);
      CHECK(e.rx == 0 && e.ry == 0, "sensitivity 3×: a drift of 6 pt/s stays a resting finger (%.2f)", e.rx);
      tp_look_tuning(1, 1, 0); pad f = drag(55, 250, 1, 0), f2 = drag(56, 250, 0, -1);
      CHECK(f.rx < -0.45f && f2.ry > 0.45f, "horizontal inverted: a swipe right turns left (%.2f), up still up (%.2f)", f.rx, f2.ry);
      tp_look_tuning(1, 0, 1); pad h = drag(57, 250, 0, -1), h2 = drag(58, 250, 1, 0);
      CHECK(h.ry < -0.45f && h2.rx > 0.45f, "vertical inverted: a swipe up turns down (%.2f), right still right (%.2f)", h.ry, h2.rx);
      float sv; int ix, iy; tp_look_tuning(10, 0, 0); tp_look_tuning_get(&sv, &ix, &iy); float s1 = sv;
      tp_look_tuning(0.01f, 0, 0); tp_look_tuning_get(&sv, &ix, &iy); float s2 = sv;
      tp_look_tuning(NAN, 0, 0); tp_look_tuning_get(&sv, &ix, &iy);
      CHECK(s1 == 3 && s2 == 0.25f && sv == 1, "out of range: 10 → 3, 0.01 → 0.25, NaN → 1");
      look_parse("1.5,y"); tp_look_tuning_get(&sv, &ix, &iy);
      CHECK(sv == 1.5f && !ix && iy, "DM_TOUCH_LOOK=1.5,y: 1.5×, vertical inverted");
      tp_look_tuning(1, 0, 0); }
    // dm_touchpad_read: the hid scale (±32767, up positive)
    clock_ns = 20000000000ull; tp_touch(40, TP_DOWN, px, py); tp_touch(40, TP_MOVE, px, py - 200 * u);
    { uint64_t b; int32_t a[4]; int on = dm_touchpad_read(&b, &a[0], &a[1], &a[2], &a[3]); CHECK(on && a[0] == 0 && a[1] == 32767, "dm_touchpad_read: stick fully up = ly 32767 (%d, %d)", a[0], a[1]); }
    up(40);
    // Layout customization and the editor (plan item 148)
    { T += 1; rd();
      float ax0 = cx("A"), ay0 = cy("A"), ar0, x, y; tp_find("A", &x, &y, &ar0);
      CHECK(tp_custom_set("A:-100,-20,1.5"), "a spec that parses is taken");
      float ax = cx("A"), ay = cy("A"), ar; tp_find("A", &x, &y, &ar);
      CHECK(fabsf(ax - (ax0 - 100 * u)) < 0.01f && fabsf(ay - (ay0 - 20 * u)) < 0.01f && fabsf(ar - ar0 * 1.5f) < 0.01f, "A moved 100 left and 20 up, 1.5× its size (%.1f, %.1f, r %.1f)", ax, ay, ar);
      tp_touch(60, TP_DOWN, ax, ay); T += 1 / 30.0; rd(); uint64_t at_new = S.b; up(60); T += 1 / 30.0; rd();
      tp_touch(61, TP_DOWN, ax0 + ar0 * 0.9f, ay0); T += 1 / 30.0; rd(); uint64_t at_old = S.b; up(61); T += 1 / 30.0; rd();
      CHECK(at_new == DM_BTN_A && !(at_old & DM_BTN_A), "a finger on A's new place presses it, one on the old edge does not (%05llx / %05llx)", (unsigned long long)at_new, (unsigned long long)at_old);
      tp_custom_set("B:5000,0,1;stick:200,-50,0.8");
      float bx = cx("B"), br; tp_find("B", &x, &y, &br); float hx, hy, hr; tp_find("stick", &hx, &hy, &hr);
      CHECK(fabsf(bx + br - (844 - 47)) < 0.01f && fabsf(hx - (px + 200 * u)) < 0.01f && fabsf(hr - r * 0.8f) < 0.01f, "B pushed far right stays inside the safe area; the stick's home moved and 0.8× (%.1f, %.1f)", bx + br, hr);
      char spec[512]; tp_custom_get(spec, sizeof spec); float b1 = cx("B");
      tp_custom_reset(); float b0 = cx("B"); tp_custom_set(spec);
      CHECK(b0 != b1 && cx("B") == b1 && strstr(spec, "stick:200.0,-50.0,0.80;"), "the spec read back gives the same layout (%s)", spec);
      CHECK(!tp_custom_set("A:1,2") && cx("B") == b1, "a spec that does not parse changes nothing");
      tp_custom_reset();
      CHECK(cx("A") == ax0 && cy("A") == ay0, "reset: the default layout");
      // the editor: a finger on X drags it 30 right and 40 down, nothing reaches the game, the size of X set; then X works where it was put
      float xx = cx("X"), xy = cy("X"), xr; tp_find("X", &x, &y, &xr);
      tp_edit(1); tp_touch(62, TP_DOWN, xx + 3, xy); tp_touch(62, TP_MOVE, xx + 33, xy + 40); T += 1 / 30.0; rd(); uint64_t during = S.b; float dl = S.lx;
      tp_touch(62, TP_UP, 0, 0); int scaled = tp_edit_scale(1.3f); float sz = tp_edit_scale_get(); tp_draw(&d); int ed = d.editing;
      tp_edit(0);
      float nx = cx("X"), ny = cy("X"), nr; tp_find("X", &x, &y, &nr);
      CHECK(fabsf(nx - (xx + 30)) < 0.01f && fabsf(ny - (xy + 40)) < 0.01f && fabsf(nr - xr * 1.3f) < 0.01f && scaled && sz == 1.3f && ed,
            "the editor: X dragged 30 right, 40 down and sized 1.3× (%.1f, %.1f, r %.1f)", nx - xx, ny - xy, nr / xr);
      CHECK(!during && dl == 0, "... nothing reaches the game while it is on");
      tp_touch(63, TP_DOWN, nx, ny); T += 1 / 30.0; rd(); CHECK(S.b == DM_BTN_X, "... and X works where it was put (%05llx)", (unsigned long long)S.b); up(63); T += 1 / 30.0; rd();
      tp_custom_reset();
      // the menu button: asks for the panel once, presses nothing
      tp_touch(64, TP_DOWN, cx("menu"), cy("menu")); T += 1 / 30.0; rd(); uint64_t mb = S.b; up(64);
      int m1 = tp_menu_take(), m2 = tp_menu_take();
      CHECK(m1 && !m2 && !mb, "the menu button: the panel asked for once, no button pressed");
      tp_opacity(0.4f); tp_draw(&d); float o1 = d.opacity; tp_opacity(0.01f); tp_draw(&d); float o2 = d.opacity; tp_opacity(1);
      CHECK(o1 == 0.4f && o2 == 0.15f, "opacity 0.4 drawn as such, 0.01 held at 0.15"); }
    {   // Device sizes (plan item 171; Touchable's internal/tests/common/test_touch_layout.c table, MIT, as data: points in landscape, the safe area, and
        // the top inset below the iPadOS window controls, which Touchable read on iPadOS 27's simulator for an iPad mini: safe top 10, window controls
        // down to 53). Each: every control and the stick's home inside the safe area, none overlapping another, the left column (ZL, L, D-pad) below
        // the window controls
        typedef struct { const char *name; float w, h, l, r, t, b, win; } device;
        static const device dv[] = {{"iPhone 16 Pro Max", 956, 440, 62, 62, 0, 21, 0}, {"iPhone 16 Pro", 874, 402, 62, 62, 0, 21, 0}, {"iPhone 13 mini", 812, 375, 50, 50, 0, 21, 0},
            {"iPhone SE", 667, 375, 0, 0, 0, 0, 0}, {"iPad mini", 1133, 744, 0, 0, 10, 20, 53}, {"iPad mini, no window controls", 1133, 744, 0, 0, 0, 20, 0},
            {"iPad mini, status bar", 1133, 744, 0, 0, 24, 20, 24}, {"iPad Air 11", 1180, 820, 0, 0, 10, 20, 53}, {"iPad Pro 13", 1376, 1032, 0, 0, 10, 20, 53},
            {"iPad small window", 700, 520, 0, 0, 0, 0, 0}};
        tp_custom_reset();
        for (size_t k = 0; k < sizeof dv / sizeof *dv; k++) {
            const device *v = &dv[k]; tp_layout(v->w, v->h, v->l, v->t, v->r, v->b, v->win);
            float L = v->l, T = v->t, R = v->w - v->r, B = v->h - v->b; int inside = 1, below = 1, apart = 1; char why[160] = "";
            enum { NS = 24 }; float sx[NS], sy[NS], ex[NS], ey[NS]; int round_[NS], ns = 0; const char *lab[NS];
            for (int i = 0; i < g.nc && ns < NS - 1; i++) { const tp_item *d = &g.c[i].d;
                sx[ns] = d->x; sy[ns] = d->y; ex[ns] = d->r; ey[ns] = d->shape == TP_PILL ? d->hh : d->r; round_[ns] = d->shape != TP_PILL; lab[ns] = d->label; ns++; }
            sx[ns] = g.home_x; sy[ns] = g.home_y; ex[ns] = ey[ns] = g.stick_r; round_[ns] = 1; lab[ns] = "stick"; ns++;
            for (int i = 0; i < ns; i++) {
                if (sx[i] - ex[i] < L - 0.01f || sx[i] + ex[i] > R + 0.01f || sy[i] - ey[i] < T - 0.01f || sy[i] + ey[i] > B + 0.01f) { inside = 0; if (!*why) snprintf(why, sizeof why, "%s outside", lab[i]); }
                if ((!strcmp(lab[i], "ZL") || !strcmp(lab[i], "L") || !strcmp(lab[i], "dpad")) && sy[i] - ey[i] < v->win - 0.01f) { below = 0; if (!*why) snprintf(why, sizeof why, "%s under the window controls", lab[i]); }
                for (int j = i + 1; j < ns; j++) {   // circles by their centres; a pill against anything by the nearest point of its box
                    float gap;
                    if (round_[i] && round_[j]) gap = hypotf(sx[i] - sx[j], sy[i] - sy[j]) - ex[i] - ex[j];
                    else { int a = round_[i] ? j : i, c = a == i ? j : i;   // a: a pill
                        float qx = fmaxf(fabsf(sx[c] - sx[a]) - ex[a], 0), qy = fmaxf(fabsf(sy[c] - sy[a]) - ey[a], 0);
                        gap = round_[c] ? hypotf(qx, qy) - ex[c] : fmaxf(fabsf(sx[c] - sx[a]) - ex[a] - ex[c], fabsf(sy[c] - sy[a]) - ey[a] - ey[c]); }
                    if (gap < -0.01f) { apart = 0; if (!*why) snprintf(why, sizeof why, "%s and %s overlap by %.1f pt", lab[i], lab[j], -gap); }
                }
            }
            CHECK(inside && below && apart, "%-30s %4.0fx%-4.0f inside the safe area, below the window controls, none overlapping%s%s", v->name, v->w, v->h, *why ? ": " : "", why);
        }
        tp_layout(844, 390, 47, 0, 47, 21, 0); }
    {   // A game controller (plan item 171): connected, the controls stay; a key hides them (off: a finger reaches nothing); a touch shows them; a key
        // again, then the last controller gone: shown
        tp_set_hidden(0); int c0 = tp_controller(TP_CTL_CONNECT); int k1 = tp_controller(TP_CTL_KEY); T += 1 / 30.0; rd();
        down(60, "A", 0, 0); T += 1 / 30.0; rd(); int none = !(S.b & DM_BTN_A); up(60);
        int t1 = tp_controller(TP_CTL_TOUCH); int k2 = tp_controller(TP_CTL_KEY), g1 = tp_controller(TP_CTL_GONE);
        down(61, "A", 0, 0); T += 1 / 30.0; rd(); int a = (S.b & DM_BTN_A) != 0; up(61); T += 1 / 30.0; rd();
        CHECK(!c0 && k1 && none && !t1 && k2 && !g1 && a, "a controller connected: shown; a key: hidden (A not pressed by a finger); a touch: shown; a key, then none left: shown, A works"); }
    {   // 315e: what the view redraws
        tp_set_hidden(0); tp_opacity(1); tp_draw_state d; tp_draw(&d); uint64_t s0 = tp_visual_sig(&d);
        tp_touch(70, TP_DOWN, px, py); tp_draw(&d); uint64_t s1 = tp_visual_sig(&d);
        tp_touch(70, TP_MOVE, px + 30 * u, py + 10 * u); tp_draw(&d); uint64_t s2 = tp_visual_sig(&d); float k2 = d.kx;
        tp_touch(70, TP_MOVE, px - 20 * u, py - 25 * u); tp_draw(&d); uint64_t s3 = tp_visual_sig(&d); float k3 = d.kx; up(70);
        CHECK(s1 == s2 && s2 == s3 && k2 != k3, "the stick moved (its knob from %.0f to %.0f): nothing to redraw [-DDM_TEST_TP_SIG_MOVES]", k2, k3);
        float lx = 844 * 0.62f, ly = 390 * 0.5f; tp_touch(71, TP_DOWN, lx, ly); tp_draw(&d); uint64_t c1 = tp_visual_sig(&d);
        tp_touch(71, TP_MOVE, lx + 60 * u, ly + 5 * u); tp_draw(&d); uint64_t c2 = tp_visual_sig(&d); int look = d.look; up(71);
        CHECK(look && c1 == c2, "a camera finger moved: nothing to redraw [-DDM_TEST_TP_SIG_MOVES]");
        tp_draw(&d); uint64_t back0 = tp_visual_sig(&d);
        down(72, "A", 0, 0); tp_draw(&d); uint64_t sa = tp_visual_sig(&d); up(72); tp_draw(&d); uint64_t sb = tp_visual_sig(&d);
        tp_opacity(0.5f); tp_draw(&d); uint64_t so = tp_visual_sig(&d); tp_opacity(1);
        CHECK(back0 == s0 && sa != s0 && sb == s0 && so != s0, "A pressed: redrawn; released: as before; the opacity changed: redrawn"); }
    printf("t_touchpad: %d checks, %d failed\n", n, fail);
    return fail != 0;
}
