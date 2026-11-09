// On-screen controls for a game steered with a controller (host/front/touchpad.c; game.json "touch": "pad"). Positions in points of the view the front
// end lays the controls out in. The front end calls tp_layout / tp_touch / tp_enable on its main thread and draws tp_draw(); hle_hid.c reads the
// controller they make at every read of the game (dm_touchpad_read).
#pragma once
#include <stdint.h>

enum { TP_DOWN, TP_MOVE, TP_UP };                     // tp_touch phases (UP also for a cancelled touch)
enum { TP_CIRCLE, TP_PILL, TP_DPAD, TP_EYE, TP_MENU };   // shapes: TP_PILL is r wide and hh high (half sizes), the others r in radius
typedef struct { int shape; float x, y, r, hh; const char *label; int pressed; uint32_t dir; } tp_item;   // dir: the D-pad's direction held
typedef struct {
    int visible, collapsed, n; float u;               // collapsed: only the eye (items[0]); u: points per design point
    tp_item items[20];
    float home_x, home_y, stick_r; int stick; float bx, by, kx, ky;   // the floating stick at home, or its base and knob while held
    int look; float look_x, look_y;                    // a camera finger
    int editing, sel; float opacity;                   // the layout editor is on, the control it holds (-1 none, TP_SEL_STICK the stick); drawing opacity
} tp_draw_state;
enum { TP_SEL_STICK = 100 };

// The view's size and safe area, and win_t: the top inset below the iPadOS window controls in the top-left corner (plan item 171; 0 = none): the
// controls of the left column (ZL, L, the D-pad) go below it. Again on any change
void tp_layout(float w, float h, float safe_l, float safe_t, float safe_r, float safe_b, float win_t);
void tp_touch(long id, int phase, float x, float y);
void tp_enable(int on);            // every finger released; off (a game controller is connected): nothing drawn or read
void tp_set_hidden(int hidden);    // the eye's choice (the front end keeps it across launches)
void tp_look_tuning(float sensitivity, int invert_x, int invert_y);   // the camera finger: 0.25-3× (clamped; 0 or NaN = 1), axes inverted
void tp_look_tuning_get(float *sensitivity, int *invert_x, int *invert_y);
int tp_hidden(void);
// The controller at time t (seconds, any monotonic clock): buttons and sticks in -1..1 (up positive); 0 when the controls are off
int tp_read(double t, uint64_t *buttons, float *lx, float *ly, float *rx, float *ry);
void tp_draw(tp_draw_state *d);
// What the view draws of d, as one number (plan item 315e, after upstream d2r-mobile 7b62d9c, MIT: the overlay redrawn only when a control's look
// changes): the stick's base and knob and the camera finger's ring are layers of their own that only move, so their places are left out (the stick
// base's not while the layout editor outlines it)
uint64_t tp_visual_sig(const tp_draw_state *d);
int tp_find(const char *label, float *x, float *y, float *r);   // a control's centre and size ("eye", "dpad", button labels); 0 if none
// Layout customization (plan item 148): "<label>:<dx>,<dy>,<size>;..." (design points, size 0.5-2; labels as tp_find's, "stick" the stick's home)
int tp_custom_set(const char *spec);   // 1, applied (an empty spec is the default layout); 0 when it does not parse: nothing changes
void tp_custom_get(char *out, unsigned long n);
void tp_custom_reset(void);
void tp_edit(int on);                  // the layout editor: a finger drags the control under it; nothing reaches the game
int tp_edit_scale(float size);         // the size of the control touched last in the editor (0.5-2); 0 when none
float tp_edit_scale_get(void);         // its size now, 0 when none
void tp_opacity(float a);              // drawing opacity of the controls, 0.15-1
int tp_menu_take(void);                // 1 once after the menu button (between - and +) was tapped
// A game controller (plan item 171, after Touchable: a key on a real controller hides the controls, a touch on the screen shows them again; one only
// connected leaves them): returns 1 while the controls are hidden by a controller (they are also off, as tp_enable(0))
enum { TP_CTL_CONNECT, TP_CTL_KEY, TP_CTL_TOUCH, TP_CTL_GONE };
int tp_controller(int event);

int dm_touchpad_read(uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry);   // hle_hid.c, the game's thread
void dm_touchpad_selftest_start(void);   // DM_TEST_TOUCHPAD (main.c)
int tp_selftest_now(const char *name);   // one of its tests (check, look, hide) now: the command channel's padtest; 0 for an unknown name
