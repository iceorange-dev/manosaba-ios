// On-screen controls (plan item 58), after BotWRecomp's runtime/src/host/touch.cpp (IshaanKalra2103/BotWRecomp 2b84b82, MIT): its layout, feel
// constants, finger rules and input checks, in C for this host. For a game steered with a controller (game.json "touch": "pad": Tears of the
// Kingdom) the iOS front end (ios/Sources/front_ios.m) lays the controls out in points inside the safe area, forwards every finger and draws
// tp_draw(); hle_hid.c ORs dm_touchpad_read() into the controller at every read of the game.
//   [ZL]                         (-)        (+)  (eye)                     [ZR]
//   [L ]                                                                   [R ]
//   D-pad                                                                   (X)
//                                                                        (Y)   (A)
//   floating stick (a touch on the left half)                               (B)
//                         (L3)                         (R3)
// Every finger owns one control from touch to lift (sliding off a button keeps it; on the D-pad it changes direction). One stick finger and one
// camera finger at a time; more fingers there are ignored. The stick's base follows a thumb that leaves its ring. The right half outside the
// buttons is the camera: the finger's speed, smoothed, moves the right stick (a resting finger 0, a moving one at least kLookFloor, past the game's
// own dead zone); lifting it returns the stick to 0. The eye collapses everything to itself and back. A press shorter than the time between two
// reads of the game is still seen once (as hle_hid.c does for the front end's keys).
// Camera tuning (plan item 109; lordran-native 42ad4cf's idea, MIT): a sensitivity of 0.25-3× scales the drag speed before the output saturates
// (0.5× still turns at full speed with a fast enough swipe; a resting finger stays 0 at 3×), and each axis can be inverted. The iOS Settings page
// (dm.look_sensitivity, dm.look_invert_x / _y; front_ios.m) or DM_TOUCH_LOOK=<sensitivity>[,x][,y] (x / y: inverted) set it.
// Layout (plan item 148; lordran-native's control editor and BotWRecomp's settings button, MIT, the ideas): each control (and the stick's home) can
// be moved and sized (0.5-2×) from its default place, kept inside the safe area; the front end keeps the spec (tp_custom_get / _set) and an opacity.
// In the editor (tp_edit) a finger drags the control under it, tp_edit_scale sizes the one touched last, and nothing reaches the game. The menu
// button between - and + only asks the front end for its panel (tp_menu_take).
// DM_TEST_TOUCHPAD=<test>[:<s>][,...] (any game, the Mac too), s seconds after start (default 30), on a 844x390 layout when the front end has none:
//   check  BotWRecomp's input checks;  hide  its eye checks;  look  only a camera finger dragged right for 1 s (to see the camera turn in shots)
// Each check logs "touch pad test: <name> ok|FAIL" with the controller the game last read (hle_hid.c); the frame numbers of each test are logged.
// Checked by host/tests/t_touchpad.c.
#include "dm.h"
#include "dm_input.h"
#include "touchpad.h"
#include <math.h>
#include <pthread.h>
#include <unistd.h>

// Feel (BotWRecomp's values). Distances in design points (points / u).
static const float kStickRadius = 54;     // floating stick drag radius
static const float kStickDead = 0.10f;    // radial dead zone, fraction of the radius
static const float kLookFullSpeed = 420;  // camera drag speed (pt/s) for a full right-stick deflection
static const float kLookRest = 10;        // slower than this (smoothed) counts as a resting finger
static const float kLookFloor = 0.12f;    // smallest non-zero output
static const float kLookTau = 0.045f;     // velocity smoothing time constant, seconds
static const float kSlop = 1.25f;         // round buttons react a bit outside their drawn radius
static const float kPillSlop = 10;        // and shoulder buttons this many points outside

enum { ROLE_CTRL, ROLE_STICK, ROLE_LOOK, ROLE_NONE };
typedef struct { long id; int role, ctrl; uint32_t dir; float x, y; } finger;
typedef struct { tp_item d; uint32_t mask; } control;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static struct {
    float w, h, u, safe[5]; int laid_out, enabled, hidden;   // safe: l t r b, then the window controls' bottom (tp_layout)
    control c[20]; int nc, eye, dpad;
    float home_x, home_y, stick_r;
    finger f[16]; int nf;
    int stick; long stick_id; float bx, by, kx, ky;
    int look; long look_id; float lpx, lpy, vx, vy;
    float look_sens; int look_inv_x, look_inv_y;
    double last_t; uint64_t latch;
    tp_item def[20]; float def_hx, def_hy, def_sr; int menu, menu_req;   // the default layout (before customization); the menu button
    int editing, sel; long edit_id; float grab_x, grab_y; float opacity;  // the editor: the control held (-1, TP_SEL_STICK) and where it was grabbed
} g = {.enabled = 1, .eye = -1, .dpad = -1, .menu = -1, .sel = -1, .look_sens = 1, .opacity = 1};
typedef struct { char label[8]; float dx, dy, s; } tp_cust;   // a control moved (design points) and sized from its default
static tp_cust g_cust[24]; static int g_ncust;
void tp_look_tuning(float sens, int inv_x, int inv_y) {
    if (!(sens > 0)) sens = 1;   // NaN or unset
    pthread_mutex_lock(&g_mu); g.look_sens = sens < 0.25f ? 0.25f : sens > 3 ? 3 : sens; g.look_inv_x = !!inv_x; g.look_inv_y = !!inv_y; pthread_mutex_unlock(&g_mu);
}
void tp_look_tuning_get(float *sens, int *inv_x, int *inv_y) { pthread_mutex_lock(&g_mu); *sens = g.look_sens; *inv_x = g.look_inv_x; *inv_y = g.look_inv_y; pthread_mutex_unlock(&g_mu); }
static void look_parse(const char *e) {   // "<sensitivity>[,x][,y]"
    tp_look_tuning((float)atof(e), strstr(e, ",x") != NULL, strstr(e, ",y") != NULL);
    LOG("touch pad: camera sensitivity %.2f×%s%s (DM_TOUCH_LOOK)", g.look_sens, g.look_inv_x ? ", horizontal inverted" : "", g.look_inv_y ? ", vertical inverted" : "");
}
static void look_env(void) { static int done; if (done) return; done = 1; const char *e = getenv("DM_TOUCH_LOOK"); if (e) look_parse(e); }   // once

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static void release_all(void) { g.nf = 0; g.stick = g.look = 0; g.vx = g.vy = 0; }
static void add(int shape, float x, float y, float r, float hh, const char *label, uint32_t mask) {
    if (g.nc < 20) g.c[g.nc++] = (control){{shape, x, y, r, hh, label, 0, 0}, mask};
}
static tp_cust *cust_of(const char *label, int make) {
    for (int i = 0; i < g_ncust; i++) if (!strcmp(g_cust[i].label, label)) return &g_cust[i];
    if (!make || g_ncust == 24) return NULL;
    tp_cust *c = &g_cust[g_ncust++]; memset(c, 0, sizeof *c); strlcpy(c->label, label, sizeof c->label); c->s = 1; return c;
}
// The default layout with the customization over it, every control kept inside the safe area (g_mu held, laid out)
static void apply_custom(void) {
    float L = g.safe[0], T = g.safe[1], R = g.w - g.safe[2], B = g.h - g.safe[3];
    for (int i = 0; i < g.nc; i++) {
        tp_item *it = &g.c[i].d; const tp_item *df = &g.def[i]; *it = *df;
#ifndef DM_TEST_TP_CUSTOM_IGNORED   // negative control of t_touchpad only: the default layout whatever the customization
        tp_cust *c = cust_of(df->label, 0);
        if (c) { it->x = df->x + c->dx * g.u; it->y = df->y + c->dy * g.u; it->r = df->r * c->s; it->hh = df->hh * c->s; }
#endif
        float ex = it->r, ey = it->shape == TP_PILL ? it->hh : it->r;
        float Ti = g.safe[4] > T && it->x - ex < L + 0.25f * (R - L) ? g.safe[4] : T;   // the top-left corner: below the window controls
        it->x = clampf(it->x, L + ex, R - ex > L + ex ? R - ex : L + ex); it->y = clampf(it->y, Ti + ey, B - ey > Ti + ey ? B - ey : Ti + ey);
    }
    g.home_x = g.def_hx; g.home_y = g.def_hy; g.stick_r = g.def_sr;
#ifndef DM_TEST_TP_CUSTOM_IGNORED
    tp_cust *c = cust_of("stick", 0);
    if (c) { g.home_x += c->dx * g.u; g.home_y += c->dy * g.u; g.stick_r *= c->s; }
#endif
    g.home_x = clampf(g.home_x, L + g.stick_r, R - g.stick_r); g.home_y = clampf(g.home_y, T + g.stick_r, B - g.stick_r);
}
void tp_layout(float W, float H, float sl, float st, float sr, float sb, float wt) {
    look_env();
    pthread_mutex_lock(&g_mu);
#ifdef DM_TEST_TP_NO_WINDOW   // negative control of t_touchpad only: the window controls ignored
    wt = 0;
#endif
    float safe[5] = {sl, st, sr, sb, wt};
    // only when the size or the safe area changed (UIKit lays views out again for other reasons too): a new layout releases every finger
    if (W > 0 && H > 0 && !(g.laid_out && W == g.w && H == g.h && !memcmp(safe, g.safe, sizeof safe))) {
        release_all(); memcpy(g.safe, safe, sizeof safe);
        // one design point = u points, grown a little on big screens
        float u = g.u = clampf((W < H ? W : H) / 400.0f, 0.85f, 1.5f), L = sl, T = st, R = W - sr, B = H - sb, m = 10 * u;
        g.w = W; g.h = H; g.nc = 0;
        float rb = 25 * u, d = 47 * u, cx = R - m - d - rb, cy = B - m - d - rb;   // face buttons, Nintendo positions: A right, B bottom, X top, Y left
        add(TP_CIRCLE, cx + d, cy, rb, 0, "A", DM_BTN_A); add(TP_CIRCLE, cx, cy + d, rb, 0, "B", DM_BTN_B);
        add(TP_CIRCLE, cx, cy - d, rb, 0, "X", DM_BTN_X); add(TP_CIRCLE, cx - d, cy, rb, 0, "Y", DM_BTN_Y);
        float pw = 37 * u, ph = 16 * u, gap = 8 * u, TL = wt > T ? wt : T;   // shoulders: ZL / ZR in the top corners, L / R under them; the left column below the window controls
        add(TP_PILL, L + m + pw, TL + m + ph, pw, ph, "ZL", DM_BTN_ZL); add(TP_PILL, L + m + pw, TL + m + 3 * ph + gap, pw, ph, "L", DM_BTN_L);
        add(TP_PILL, R - m - pw, T + m + ph, pw, ph, "ZR", DM_BTN_ZR); add(TP_PILL, R - m - pw, T + m + 3 * ph + gap, pw, ph, "R", DM_BTN_R);
        float sr2 = 16 * u, ty = T + m + sr2;   // top centre: - and +, the menu button between them, the eye
        add(TP_CIRCLE, W * 0.5f - 62 * u, ty, sr2, 0, "-", DM_BTN_MINUS); add(TP_CIRCLE, W * 0.5f + 62 * u, ty, sr2, 0, "+", DM_BTN_PLUS);
        g.menu = g.nc; add(TP_MENU, W * 0.5f, ty, 14 * u, 0, "menu", 0);   // the front end's panel (plan item 148), where BotWRecomp has its settings button
        g.eye = g.nc; add(TP_EYE, W * 0.5f + 124 * u, ty, 14 * u, 0, "eye", 0);
        float dr = 40 * u;   // D-pad on the left, under L, above the stick's area
        g.dpad = g.nc; add(TP_DPAD, L + m + dr + 4 * u, TL + m + 4 * ph + gap + 16 * u + dr, dr, 0, "dpad", 0);
        float r3 = 18 * u;   // stick clicks
        add(TP_CIRCLE, W * 0.5f - 122 * u, B - m - r3, r3, 0, "L3", DM_BTN_LS); add(TP_CIRCLE, W * 0.5f + 122 * u, B - m - r3, r3, 0, "R3", DM_BTN_RS);
        g.stick_r = kStickRadius * u; g.home_x = L + m + 20 * u + g.stick_r; g.home_y = B - m - 14 * u - g.stick_r;   // the stick drawn at home while idle
        for (int i = 0; i < g.nc; i++) g.def[i] = g.c[i].d;
        g.def_hx = g.home_x; g.def_hy = g.home_y; g.def_sr = g.stick_r;
        g.laid_out = 1; apply_custom();
        LOG("touch pad: layout %.0fx%.0f pt, %.2f pt per design point, safe area l %.0f t %.0f r %.0f b %.0f, window controls down to %.0f", W, H, u, sl, st, sr, sb, wt);
    }
    pthread_mutex_unlock(&g_mu);
}
static int hit(float x, float y) {   // the control under (x, y), -1 for none
    int best = -1; float best_score = 1e9f;
    for (int i = 0; i < g.nc; i++) {
        const tp_item *d = &g.c[i].d; float score;
        if (d->shape == TP_PILL) {
            float dx = fmaxf(fabsf(x - d->x) - d->r, 0), dy = fmaxf(fabsf(y - d->y) - d->hh, 0), out = hypotf(dx, dy);
            if (out > kPillSlop * g.u) continue;
            score = out / (kPillSlop * g.u);
        } else {
            float slop = d->shape == TP_DPAD ? 1.15f : kSlop, dist = hypotf(x - d->x, y - d->y);
            if (dist > d->r * slop) continue;
            score = dist / (d->r * slop);
        }
        if (score < best_score) { best_score = score; best = i; }
    }
    return best;
}
static uint32_t dpad_dir(const tp_item *d, float x, float y) {
    float dx = x - d->x, dy = y - d->y;
    if (hypotf(dx, dy) < d->r * 0.2f) return 0;
    if (fabsf(dx) > fabsf(dy)) return dx > 0 ? DM_BTN_RIGHT : DM_BTN_LEFT;
    return dy > 0 ? DM_BTN_DOWN : DM_BTN_UP;
}
static void stick_to(float x, float y) {   // the base follows a thumb that leaves the ring
    float dx = x - g.bx, dy = y - g.by, m = hypotf(dx, dy);
    if (m > g.stick_r) { g.bx = x - dx / m * g.stick_r; g.by = y - dy / m * g.stick_r; }
    g.kx = x; g.ky = y;
}
static finger *find(long id) { for (int i = 0; i < g.nf; i++) if (g.f[i].id == id) return &g.f[i]; return NULL; }
static void edit_touch(long id, int phase, float x, float y) {   // the layout editor: one finger drags the control under it
    if (phase == TP_DOWN) {
        if (g.edit_id) return;
        int c = hit(x, y);
        if (c >= 0) { g.sel = c; g.grab_x = x - g.c[c].d.x; g.grab_y = y - g.c[c].d.y; }
        else if (hypotf(x - g.home_x, y - g.home_y) <= g.stick_r) { g.sel = TP_SEL_STICK; g.grab_x = x - g.home_x; g.grab_y = y - g.home_y; }
        else return;
        g.edit_id = id;
    } else if (id == g.edit_id && phase == TP_MOVE) {
        int st = g.sel == TP_SEL_STICK; const char *label = st ? "stick" : g.def[g.sel].label;
        float bx = st ? g.def_hx : g.def[g.sel].x, by = st ? g.def_hy : g.def[g.sel].y;
        tp_cust *c = cust_of(label, 1); if (!c) return;
        c->dx = (x - g.grab_x - bx) / g.u; c->dy = (y - g.grab_y - by) / g.u;
        apply_custom();   // kept inside the safe area: the move stored is where it ended up
        c->dx = ((st ? g.home_x : g.c[g.sel].d.x) - bx) / g.u; c->dy = ((st ? g.home_y : g.c[g.sel].d.y) - by) / g.u;
    } else if (id == g.edit_id) g.edit_id = 0;
}
static void touch_locked(long id, int phase, float x, float y) {
    finger *fg = find(id);
    if (!g.laid_out || !g.enabled) return;
    if (g.editing) { edit_touch(id, phase, x, y); return; }
    if (phase == TP_DOWN) {
        if (fg || g.nf == 16) return;
        finger n = {id, ROLE_CTRL, -1, 0, x, y};
        int c = hit(x, y);
        if (g.hidden && c != g.eye) return;   // collapsed: only the eye is live
        if (c >= 0) {
            n.ctrl = c;
            if (c == g.dpad) n.dir = dpad_dir(&g.c[c].d, x, y);
            if (c == g.eye) {
                g.hidden = !g.hidden; if (g.hidden) { g.stick = g.look = 0; g.vx = g.vy = 0; }
                LOG("touch pad: controls %s", g.hidden ? "hidden" : "shown");
            }
            if (c == g.menu) g.menu_req = 1;
            g.latch |= c == g.dpad ? n.dir : g.c[c].mask;
        } else if (x < g.w * 0.5f) {
            if (g.stick) return;   // one stick finger at a time
            n.role = ROLE_STICK; g.stick = 1; g.stick_id = id; g.bx = g.kx = x; g.by = g.ky = y;
        } else {
            if (g.look) return;
            n.role = ROLE_LOOK; g.look = 1; g.look_id = id; g.lpx = x; g.lpy = y; g.vx = g.vy = 0;
        }
        g.f[g.nf++] = n;
    } else if (!fg) {
        return;
    } else if (phase == TP_MOVE) {
        fg->x = x; fg->y = y;
        if (fg->role == ROLE_STICK && g.stick && g.stick_id == id) stick_to(x, y);
#ifndef DM_TEST_TP_REHIT   // negative control of t_touchpad only: a sliding finger takes the button under it instead of keeping its own
        else if (fg->role == ROLE_CTRL && fg->ctrl == g.dpad) fg->dir = dpad_dir(&g.c[fg->ctrl].d, x, y);
#else
        else if (fg->role == ROLE_CTRL) { int c = hit(x, y); if (c < 0) fg->role = ROLE_NONE; else { fg->ctrl = c; if (c == g.dpad) fg->dir = dpad_dir(&g.c[c].d, x, y); } }
#endif
    } else {   // up / cancelled
        if (fg->role == ROLE_STICK && g.stick_id == id) g.stick = 0;
        if (fg->role == ROLE_LOOK && g.look_id == id) { g.look = 0; g.vx = g.vy = 0; }
        *fg = g.f[--g.nf];
    }
}
void tp_touch(long id, int phase, float x, float y) { pthread_mutex_lock(&g_mu); touch_locked(id, phase, x, y); pthread_mutex_unlock(&g_mu); }
void tp_enable(int on) {
    pthread_mutex_lock(&g_mu);
    release_all();
    if (on != g.enabled) LOG("touch pad: on-screen controls %s", on ? "on" : "off");
    g.enabled = on; g.latch = 0;
    pthread_mutex_unlock(&g_mu);
}
static int g_ctl_hidden;
int tp_controller(int ev) {
    int h = g_ctl_hidden;
    if (ev == TP_CTL_KEY) h = 1;
    else if (ev == TP_CTL_TOUCH || ev == TP_CTL_GONE) h = 0;
#ifdef DM_TEST_TP_CTL_ON_CONNECT   // negative control of t_touchpad only: hidden as soon as a controller connects, as before
    else if (ev == TP_CTL_CONNECT) h = 1;
#endif
    if (h != g_ctl_hidden) { g_ctl_hidden = h; tp_enable(!h);
        LOG("touch pad: %s (%s)", h ? "hidden" : "shown", ev == TP_CTL_KEY ? "a key on a game controller" : ev == TP_CTL_TOUCH ? "the screen touched" : ev == TP_CTL_GONE ? "no game controller left" : "a game controller connected"); }
    return h;
}
void tp_set_hidden(int h) { pthread_mutex_lock(&g_mu); g.hidden = !!h; if (h) { g.stick = g.look = 0; g.vx = g.vy = 0; } pthread_mutex_unlock(&g_mu); }
int tp_hidden(void) { pthread_mutex_lock(&g_mu); int h = g.hidden; pthread_mutex_unlock(&g_mu); return h; }

int tp_read(double t, uint64_t *buttons, float *lx, float *ly, float *rx, float *ry) {
    *buttons = 0; *lx = *ly = *rx = *ry = 0;
    pthread_mutex_lock(&g_mu);
    if (!g.laid_out || !g.enabled) { g.last_t = 0; pthread_mutex_unlock(&g_mu); return 0; }
    float gap = g.last_t ? (float)(t - g.last_t) : 1.0f, dt = clampf(gap, 0.0005f, 0.1f);
    g.last_t = t;
    finger *lf = g.look ? find(g.look_id) : NULL;
    if (gap > 0.1f && lf) { g.lpx = lf->x; g.lpy = lf->y; g.vx = g.vy = 0; }   // the first read after a pause: no velocity spike
    if (g.hidden || g.editing) { g.latch = 0; pthread_mutex_unlock(&g_mu); return 1; }   // collapsed, or the layout editor: nothing reaches the game
    uint64_t b = g.latch; g.latch = 0;
    for (int i = 0; i < g.nf; i++) if (g.f[i].role == ROLE_CTRL) b |= g.f[i].ctrl == g.dpad ? g.f[i].dir : g.c[g.f[i].ctrl].mask;
    *buttons = b;
    if (g.stick) {
        float vx = (g.kx - g.bx) / g.stick_r, vy = (g.ky - g.by) / g.stick_r, m = hypotf(vx, vy);
        if (m > kStickDead) { float o = fminf(1.0f, (m - kStickDead) / (1 - kStickDead)); *lx = vx / m * o; *ly = -vy / m * o; }
    }
    if (lf) {
        float dx = (lf->x - g.lpx) / g.u, dy = (lf->y - g.lpy) / g.u, a = 1 - expf(-dt / kLookTau);
        g.lpx = lf->x; g.lpy = lf->y;
        g.vx += (dx / dt - g.vx) * a; g.vy += (dy / dt - g.vy) * a;
        float sp = hypotf(g.vx, g.vy);
        if (sp > kLookRest) {
#ifndef DM_TEST_TP_SENS_AFTER
            float o = kLookFloor + (1 - kLookFloor) * fminf(1.0f, g.look_sens * (sp - kLookRest) / (kLookFullSpeed - kLookRest));
#else       // negative control of t_touchpad only: the sensitivity scales the saturated output
            float o = fminf(1.0f, g.look_sens * (kLookFloor + (1 - kLookFloor) * fminf(1.0f, (sp - kLookRest) / (kLookFullSpeed - kLookRest))));
#endif
            *rx = g.vx / sp * o * (g.look_inv_x ? -1 : 1); *ry = -g.vy / sp * o * (g.look_inv_y ? -1 : 1);
        }
    }
    pthread_mutex_unlock(&g_mu);
    return 1;
}
void tp_draw(tp_draw_state *d) {
    memset(d, 0, sizeof *d);
    pthread_mutex_lock(&g_mu);
    d->visible = g.laid_out && g.enabled;
    d->sel = -1; d->opacity = g.opacity;
    if (d->visible) {
        d->u = g.u; d->editing = g.editing; d->sel = g.editing ? g.sel : -1;
        for (int i = 0; i < g.nc; i++) d->items[i] = g.c[i].d;
        for (int i = 0; i < g.nf; i++) if (g.f[i].role == ROLE_CTRL) { d->items[g.f[i].ctrl].pressed = 1; d->items[g.f[i].ctrl].dir |= g.f[i].dir; }
        if (g.hidden) { d->items[0] = d->items[g.eye]; d->n = 1; d->collapsed = 1; }
        else {
            d->n = g.nc; d->home_x = g.home_x; d->home_y = g.home_y; d->stick_r = g.stick_r;
            d->stick = g.stick; d->bx = g.bx; d->by = g.by; d->kx = g.kx; d->ky = g.ky;
            finger *lf = g.look ? find(g.look_id) : NULL;
            if (lf) { d->look = 1; d->look_x = lf->x; d->look_y = lf->y; }
        }
    }
    pthread_mutex_unlock(&g_mu);
}
int tp_find(const char *label, float *x, float *y, float *r) {   // "stick": the stick's home
    pthread_mutex_lock(&g_mu); int ok = 0;
    for (int i = 0; i < g.nc && !ok; i++) if (!strcmp(g.c[i].d.label, label)) { *x = g.c[i].d.x; *y = g.c[i].d.y; *r = g.c[i].d.r; ok = 1; }
    if (!ok && !strcmp(label, "stick") && g.laid_out) { *x = g.home_x; *y = g.home_y; *r = g.stick_r; ok = 1; }
    pthread_mutex_unlock(&g_mu); return ok;
}

int tp_custom_set(const char *spec) {
    tp_cust t[24]; int n = 0; char buf[1024]; strlcpy(buf, spec ? spec : "", sizeof buf);
    for (char *sv, *e = strtok_r(buf, ";", &sv); e; e = strtok_r(NULL, ";", &sv)) {
        char *c = strrchr(e, ':'); float dx, dy, sz; char tail;
        if (!c || c == e || c - e >= 8 || n == 24 || sscanf(c + 1, "%f,%f,%f%c", &dx, &dy, &sz, &tail) != 3 || !isfinite(dx) || !isfinite(dy) || !(sz > 0)) return 0;
        memset(&t[n], 0, sizeof t[n]); memcpy(t[n].label, e, (size_t)(c - e));
        t[n].dx = clampf(dx, -2000, 2000); t[n].dy = clampf(dy, -2000, 2000); t[n].s = clampf(sz, 0.5f, 2); n++;
    }
    pthread_mutex_lock(&g_mu);
    memcpy(g_cust, t, sizeof t[0] * (size_t)n); g_ncust = n;
    if (g.laid_out) { release_all(); apply_custom(); }
    pthread_mutex_unlock(&g_mu);
    return 1;
}
void tp_custom_get(char *out, unsigned long n) {
    pthread_mutex_lock(&g_mu); size_t o = 0; if (n) out[0] = 0;
    for (int i = 0; i < g_ncust && o < n; i++) if (g_cust[i].dx || g_cust[i].dy || g_cust[i].s != 1)
        o += (size_t)snprintf(out + o, n - o, "%s:%.1f,%.1f,%.2f;", g_cust[i].label, g_cust[i].dx, g_cust[i].dy, g_cust[i].s);
    pthread_mutex_unlock(&g_mu);
}
void tp_custom_reset(void) { pthread_mutex_lock(&g_mu); g_ncust = 0; if (g.laid_out) apply_custom(); pthread_mutex_unlock(&g_mu); }
void tp_edit(int on) {
    pthread_mutex_lock(&g_mu); release_all(); g.latch = 0; g.editing = !!on; g.edit_id = 0; g.sel = -1; if (on) g.hidden = 0;   // the editor shows every control
    LOG("touch pad: layout editor %s", on ? "on" : "off");
    pthread_mutex_unlock(&g_mu);
}
int tp_edit_scale(float size) {
    pthread_mutex_lock(&g_mu); int ok = g.editing && g.sel >= 0;
    if (ok) { tp_cust *c = cust_of(g.sel == TP_SEL_STICK ? "stick" : g.def[g.sel].label, 1); if (c) { c->s = clampf(size, 0.5f, 2); apply_custom(); } else ok = 0; }
    pthread_mutex_unlock(&g_mu); return ok;
}
float tp_edit_scale_get(void) {
    pthread_mutex_lock(&g_mu); float s = 0;
    if (g.editing && g.sel >= 0) { tp_cust *c = cust_of(g.sel == TP_SEL_STICK ? "stick" : g.def[g.sel].label, 0); s = c ? c->s : 1; }
    pthread_mutex_unlock(&g_mu); return s;
}
static uint64_t sig_mix(uint64_t h, const void *p, size_t n) { const uint8_t *b = p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ull; } return h; }
uint64_t tp_visual_sig(const tp_draw_state *d) {   // FNV-1a over what drawRect draws
    uint64_t h = 0xcbf29ce484222325ull;
    int head[5] = {d->visible, d->collapsed, d->n, d->editing, d->sel}; float f[5] = {d->u, d->opacity, d->home_x, d->home_y, d->stick_r};
    h = sig_mix(h, head, sizeof head); h = sig_mix(h, f, sizeof f);
    for (int i = 0; i < d->n && i < 20; i++) {
        const tp_item *it = &d->items[i]; int a[3] = {it->shape, it->pressed, (int)it->dir}; float p[4] = {it->x, it->y, it->r, it->hh};
        h = sig_mix(h, a, sizeof a); h = sig_mix(h, p, sizeof p); if (it->label) h = sig_mix(h, it->label, strlen(it->label) + 1);
    }
#ifndef DM_TEST_TP_SIG_MOVES   // negative control of t_touchpad only: the stick's and the camera finger's places in it, a redraw on every move as before
    if (d->editing) { float e[2] = {d->stick ? d->bx : d->home_x, d->stick ? d->by : d->home_y}; h = sig_mix(h, e, sizeof e); }
#else
    float m[7] = {d->bx, d->by, d->kx, d->ky, d->look_x, d->look_y, (float)d->look}; h = sig_mix(h, m, sizeof m);
#endif
    return h;
}
void tp_opacity(float a) { pthread_mutex_lock(&g_mu); g.opacity = a > 0 ? clampf(a, 0.15f, 1) : 1; pthread_mutex_unlock(&g_mu); }
int tp_menu_take(void) { pthread_mutex_lock(&g_mu); int r = g.menu_req; g.menu_req = 0; pthread_mutex_unlock(&g_mu); return r; }

// ---------- hle_hid.c and DM_TEST_TOUCHPAD ----------
static int32_t axis(float v) { return (int32_t)lroundf(clampf(v, -1, 1) * DM_STICK_MAX); }
int dm_touchpad_read(uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) {
    float a, b, c, d;
    if (!tp_read(now_ns() / 1e9, buttons, &a, &b, &c, &d)) return 0;
    *lx = axis(a); *ly = axis(b); *rx = axis(c); *ry = axis(d);
    return 1;
}
int hle_hid_last(uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry);   // the controller the game last read (hle_hid.c)

// BotWRecomp's plan_check: steps at seconds after its start; a check reads what the game last read (at 30 fps it reads every 33 ms)
typedef struct { uint64_t b; float lx, ly, rx, ry; } pad;
static int zero_axes(const pad *s) { return !s->lx && !s->ly && !s->rx && !s->ry; }
static int c_stick_right(const pad *s) { return s->lx > 0.6f && s->lx < 0.8f && fabsf(s->ly) < 0.02f && !s->b; }
static int c_a_stick(const pad *s) { return s->b == DM_BTN_A && s->lx > 0.6f; }
static int c_cam_right(const pad *s) { return s->rx > 0.45f && s->rx < 0.8f && fabsf(s->ry) < 0.05f && s->b == DM_BTN_A && s->lx > 0.6f; }
static int c_cam_rest(const pad *s) { return fabsf(s->rx) < 0.01f && fabsf(s->ry) < 0.01f; }
static int c_cam_up(const pad *s) { return s->rx == 0 && s->ry == 0 && s->lx > 0.6f; }
static int c_a_up(const pad *s) { return !s->b; }
static int c_hard_left(const pad *s) { return s->lx < -0.99f && fabsf(s->ly) < 0.02f; }
static int c_followed(const pad *s) { return s->lx > -0.5f && s->lx < -0.25f; }
static int c_stick_up(const pad *s) { return s->ly > 0.9f && fabsf(s->lx) < 0.1f; }
static int c_released(const pad *s) { return zero_axes(s) && !s->b; }
static int c_dpad_up(const pad *s) { return s->b == DM_BTN_UP; }
static int c_dpad_right(const pad *s) { return s->b == DM_BTN_RIGHT; }
static int c_four_a(const pad *s) { return s->b == (DM_BTN_ZR | DM_BTN_RS | DM_BTN_L | DM_BTN_Y) && zero_axes(s); }
static int c_four_b(const pad *s) { return s->b == (DM_BTN_MINUS | DM_BTN_LS | DM_BTN_X | DM_BTN_ZL); }
static int c_three(const pad *s) { return s->b == (DM_BTN_PLUS | DM_BTN_R | DM_BTN_B); }
static int c_second_left(const pad *s) { return s->ly < -0.5f && fabsf(s->lx) < 0.02f && !s->b; }
static int g_pass, g_fail;
static void check(const char *name, int (*ok)(const pad *)) {
    uint64_t b = 0; int32_t a[4] = {0};
    int have = hle_hid_last(&b, &a[0], &a[1], &a[2], &a[3]);
    b &= 0xffff;   // the buttons the controls press: the sticks' direction buttons (bits 16-23, hle_hid.c) follow the sticks checked here
    pad s = {b, a[0] / (float)DM_STICK_MAX, a[1] / (float)DM_STICK_MAX, a[2] / (float)DM_STICK_MAX, a[3] / (float)DM_STICK_MAX};
    int pass = have && ok(&s); if (pass) g_pass++; else g_fail++;
    LOG("touch pad test: %-24s %s  buttons %05llx  L %+.2f %+.2f  R %+.2f %+.2f%s", name, pass ? "ok  " : "FAIL", (unsigned long long)s.b,
        s.lx, s.ly, s.rx, s.ry, have ? "" : "  (the game has read no controller)");
}
static double now_s(void) { return now_ns() / 1e9; }
static void until(double t0, double s) { while (now_s() - t0 < s) usleep(2000); }
static void at(const char *label, float dx, float dy, float *x, float *y) { float r; if (!tp_find(label, x, y, &r)) *x = *y = -1000; *x += dx; *y += dy; }
static void down_on(long id, const char *label, float dx, float dy) { float x, y; at(label, dx, dy, &x, &y); tp_touch(id, TP_DOWN, x, y); }
static void plan_check(void) {
    double t0 = now_s(); float u, w, h, px, py, r, x, y;
    LOG("touch pad test: check from frame %llu", (unsigned long long)dm_nvn_frame_count());
    pthread_mutex_lock(&g_mu); u = g.u; w = g.w; h = g.h; pthread_mutex_unlock(&g_mu);
    tp_find("stick", &px, &py, &r);
    tp_touch(1, TP_DOWN, px, py); until(t0, 0.05); tp_touch(1, TP_MOVE, px + 40 * u, py);
    until(t0, 0.25); check("stick right", c_stick_right);
    down_on(2, "A", 0, 0);
    until(t0, 0.45); check("A held + stick", c_a_stick);
    // camera: a finger dragged right at 250 pt/s for 0.5 s, then resting, then lifted
    float lx0 = w * 0.62f, ly0 = h * 0.5f; tp_touch(3, TP_DOWN, lx0, ly0);
    double start = now_s(); int checked = 0;
    while (now_s() - start < 0.5) {
        tp_touch(3, TP_MOVE, lx0 + (float)(now_s() - start) * 250 * u, ly0); usleep(4000);
        if (!checked && now_s() - t0 >= 0.85) { checked = 1; check("camera drag right", c_cam_right); }
    }
    until(t0, 1.35); check("camera finger resting", c_cam_rest);
    tp_touch(3, TP_UP, 0, 0);
    until(t0, 1.45); check("camera released", c_cam_up);
    tp_touch(2, TP_UP, 0, 0);
    until(t0, 1.55); check("A released", c_a_up);
    tp_touch(1, TP_MOVE, px - 160 * u, py);   // past the ring to the left (the base follows), back a little, then straight up from the moved base
    until(t0, 1.75); check("stick hard left", c_hard_left);
    tp_touch(1, TP_MOVE, px - 130 * u, py);
    until(t0, 1.95); check("stick base followed", c_followed);
    tp_touch(1, TP_MOVE, px - 106 * u, py - 80 * u);
    until(t0, 2.10); check("stick up", c_stick_up);
    tp_touch(1, TP_UP, 0, 0);
    until(t0, 2.20); check("stick released", c_released);
    float dr; tp_find("dpad", &x, &y, &dr);   // D-pad: press up, slide to the right
    tp_touch(4, TP_DOWN, x, y - dr * 0.6f);
    until(t0, 2.30); check("dpad up", c_dpad_up);
    tp_touch(4, TP_MOVE, x + dr * 0.6f, y);
    until(t0, 2.40); check("dpad slid right", c_dpad_right);
    tp_touch(4, TP_UP, 0, 0);
    until(t0, 2.50);   // several buttons at once, touches a little off-centre
    down_on(5, "ZR", -30 * u, 10 * u); down_on(6, "R3", 8 * u, 0); down_on(7, "L", 0, 0); down_on(8, "Y", 0, -12 * u);
    until(t0, 2.60); check("ZR + R3 + L + Y", c_four_a);
    for (long i = 5; i <= 8; i++) tp_touch(i, TP_UP, 0, 0);
    until(t0, 2.70); down_on(9, "-", 0, 0); down_on(10, "L3", 0, 0); down_on(11, "X", 0, 0); down_on(12, "ZL", 0, 0);
    until(t0, 2.80); check("- + L3 + X + ZL", c_four_b);
    for (long i = 9; i <= 12; i++) tp_touch(i, TP_UP, 0, 0);
    until(t0, 2.85); down_on(13, "+", 0, 0); down_on(14, "R", 0, 0); down_on(15, "B", 0, 0);
    until(t0, 2.95); check("+ + R + B", c_three);
    for (long i = 13; i <= 15; i++) tp_touch(i, TP_UP, 0, 0);
    // a second finger on the left half while the stick is held is ignored; the stick keeps its finger
    until(t0, 3.05); tp_touch(16, TP_DOWN, px, py); tp_touch(17, TP_DOWN, px + 120 * u, py - 40 * u);
    until(t0, 3.10); tp_touch(16, TP_MOVE, px, py + 40 * u); tp_touch(17, TP_MOVE, px + 200 * u, py - 40 * u);
    until(t0, 3.25); check("2nd left finger ignored", c_second_left);
    tp_touch(16, TP_UP, 0, 0); tp_touch(17, TP_UP, 0, 0);
    until(t0, 3.35); check("all released", c_released);
    LOG("touch pad test: check done at frame %llu, %d passed, %d failed", (unsigned long long)dm_nvn_frame_count(), g_pass, g_fail);
}
// look: only a camera finger, dragged right at 250 pt/s for 1 s, resting 0.3 s, lifted (to see the camera turn in shots)
static void plan_look(void) {
    float w, h; pthread_mutex_lock(&g_mu); w = g.w; h = g.h; float u = g.u; pthread_mutex_unlock(&g_mu);
    float x0 = w * 0.62f, y0 = h * 0.5f; double t0 = now_s();
    LOG("touch pad test: look from frame %llu", (unsigned long long)dm_nvn_frame_count());
    tp_touch(50, TP_DOWN, x0, y0);
    while (now_s() - t0 < 1.0) { tp_touch(50, TP_MOVE, x0 + (float)(now_s() - t0) * 250 * u, y0); usleep(4000); }
    LOG("touch pad test: look drag over at frame %llu", (unsigned long long)dm_nvn_frame_count());
    until(t0, 1.3); tp_touch(50, TP_UP, 0, 0);
    until(t0, 1.4); check("look released", c_released);
}
// hide (BotWRecomp's plan_hide): the eye, the stick dead, the eye again, the stick working
static int c_lx_right(const pad *s) { return s->lx > 0.6f; }
static void plan_hide(void) {
    double t0 = now_s(); float u, px, py, r; tp_find("stick", &px, &py, &r);
    pthread_mutex_lock(&g_mu); u = g.u; pthread_mutex_unlock(&g_mu);
    down_on(51, "eye", 0, 0); until(t0, 0.05); tp_touch(51, TP_UP, 0, 0);
    until(t0, 0.15); tp_touch(52, TP_DOWN, px, py); until(t0, 0.20); tp_touch(52, TP_MOVE, px + 40 * u, py);
    until(t0, 0.35); check("hidden: stick dead", c_released);
    tp_touch(52, TP_UP, 0, 0);
    until(t0, 0.45); down_on(53, "eye", 0, 0); until(t0, 0.50); tp_touch(53, TP_UP, 0, 0);
    until(t0, 0.60); tp_touch(54, TP_DOWN, px, py); until(t0, 0.65); tp_touch(54, TP_MOVE, px + 40 * u, py);
    until(t0, 0.80); check("shown again: stick right", c_lx_right);
    tp_touch(54, TP_UP, 0, 0);
    LOG("touch pad test: hide done, %d passed, %d failed", g_pass, g_fail);
}
static struct { double at; void (*run)(void); } g_plan[8]; static int g_nplan;
static int by_time(const void *a, const void *b) { double d = ((const typeof(g_plan[0]) *)a)->at - ((const typeof(g_plan[0]) *)b)->at; return d < 0 ? -1 : d > 0; }
static void *selftest(void *arg) {
    (void)arg;
    for (int i = 0; i < g_nplan; i++) { until(g_t0_ns / 1e9, g_plan[i].at); g_plan[i].run(); }
    return NULL;
}
static void *selftest_one(void *arg) { ((void (*)(void))arg)(); return NULL; }
int tp_selftest_now(const char *name) {   // the command channel's padtest: one test, now
    void (*run)(void) = !strcmp(name, "check") ? plan_check : !strcmp(name, "look") ? plan_look : !strcmp(name, "hide") ? plan_hide : NULL;
    if (!run) return 0;
    pthread_mutex_lock(&g_mu); int laid = g.laid_out; pthread_mutex_unlock(&g_mu);
    if (!laid) tp_layout(844, 390, 47, 0, 47, 21, 0);
    LOG("touch pad test: %s now (command channel)", name);
    pthread_t t; if (!pthread_create(&t, NULL, selftest_one, (void *)run)) pthread_detach(t);
    return 1;
}
void dm_touchpad_selftest_start(void) {
    const char *e = getenv("DM_TEST_TOUCHPAD"); if (!e || !*e) return;
    char buf[256]; strlcpy(buf, e, sizeof buf);
    for (char *sv, *t = strtok_r(buf, ",", &sv); t && g_nplan < 8; t = strtok_r(NULL, ",", &sv)) {
        char *c = strchr(t, ':'); if (c) *c++ = 0;
        void (*run)(void) = !strcmp(t, "check") ? plan_check : !strcmp(t, "look") ? plan_look : !strcmp(t, "hide") ? plan_hide : NULL;
        if (!run) { LOG("[red] DM_TEST_TOUCHPAD: no test \"%s\" (check, look, hide)", t); continue; }
        g_plan[g_nplan].at = c ? atof(c) : 30; g_plan[g_nplan++].run = run;
        LOG("touch pad test: %s at %.1f s", t, c ? atof(c) : 30);
    }
    if (!g_nplan) return;
    qsort(g_plan, (size_t)g_nplan, sizeof g_plan[0], by_time);
    pthread_mutex_lock(&g_mu); int laid = g.laid_out; pthread_mutex_unlock(&g_mu);
    if (!laid) tp_layout(844, 390, 47, 0, 47, 21, 0);   // an iPhone in landscape (points)
    pthread_t t; if (!pthread_create(&t, NULL, selftest, NULL)) pthread_detach(t);
}
