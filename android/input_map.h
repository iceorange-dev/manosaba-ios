// What the Android front end (front_android.c, plan item 301d) makes of the system's input, without the system: pure, checked on the phone by
// tests/t_input.c.
//   Keys: a game controller's KEYCODE_BUTTON_* and the d-pad's keys → the Switch buttons (dm_input.h). Its A / B / X / Y as the system names them
//   (Android names the bottom face button A, as GameController's buttonA on Apple: host/gamepad_map.h has the rule, DM_FACE_BUTTONS=position by
//   position); L1 / R1 → L / R, L2 / R2 → ZL / ZR, the stick buttons → LS / RS, Start → +, Select → −. Other keys (volume, Back, Home) are not the
//   game's and stay the system's.
//   Sticks: AXIS_X / Y left, AXIS_Z / RZ right, ±1 → ±DM_STICK_MAX with up positive (Android's Y grows downwards); the hat (AXIS_HAT_X / Y) is the
//   d-pad; a trigger axis (LTRIGGER / BRAKE, RTRIGGER / GAS) past half way is ZL / ZR.
//   Touches: the game picture is the largest rectangle of the game's aspect ratio centred in the window (as the iOS view, a difference under 1%
//   fills it); a finger is normalised to it, [0, 1) from the top left; a finger that goes down outside the picture is not the game's.
#pragma once
#include <android/input.h>
#include <android/keycodes.h>
#include <math.h>
#include <stdint.h>
#include "gamepad_map.h"

// The system's A / B / X / Y: which one a key is (0-3), or -1
static inline int dm_and_face(int32_t key) {
    switch (key) { case AKEYCODE_BUTTON_A: return 0; case AKEYCODE_BUTTON_B: return 1; case AKEYCODE_BUTTON_X: return 2; case AKEYCODE_BUTTON_Y: return 3; }
    return -1;
}
// A key's Switch button, 0 when it is not the game's
static inline uint64_t dm_and_key_bit(int32_t key, int by_position) {
    int f = dm_and_face(key);
    if (f >= 0) return dm_gamepad_face_by(f == 0, f == 1, f == 2, f == 3, by_position);
    switch (key) {
    case AKEYCODE_BUTTON_L1: return DM_BTN_L;       case AKEYCODE_BUTTON_R1: return DM_BTN_R;
    case AKEYCODE_BUTTON_L2: return DM_BTN_ZL;      case AKEYCODE_BUTTON_R2: return DM_BTN_ZR;
    case AKEYCODE_BUTTON_THUMBL: return DM_BTN_LS;  case AKEYCODE_BUTTON_THUMBR: return DM_BTN_RS;
    case AKEYCODE_BUTTON_START: return DM_BTN_PLUS; case AKEYCODE_BUTTON_SELECT: return DM_BTN_MINUS;
    case AKEYCODE_DPAD_UP: return DM_BTN_UP;        case AKEYCODE_DPAD_DOWN: return DM_BTN_DOWN;
    case AKEYCODE_DPAD_LEFT: return DM_BTN_LEFT;    case AKEYCODE_DPAD_RIGHT: return DM_BTN_RIGHT;
    }
    return 0;
}
static inline int32_t dm_and_axis(float v, int flip) {   // flip: Android's downward Y → the Switch's upward
    if (!(v == v)) v = 0;
    if (v > 1) v = 1; if (v < -1) v = -1;
#ifndef DM_TEST_INPUT_Y_DOWN   // negative control of t_input only: Y as Android gives it
    if (flip) v = -v;
#endif
    return (int32_t)lroundf(v * DM_STICK_MAX);
}
// The hat and the triggers as buttons
static inline uint64_t dm_and_hat_bits(float hx, float hy) {
    return (hx <= -0.5f ? DM_BTN_LEFT : 0) | (hx >= 0.5f ? DM_BTN_RIGHT : 0) | (hy <= -0.5f ? DM_BTN_UP : 0) | (hy >= 0.5f ? DM_BTN_DOWN : 0);
}
static inline uint64_t dm_and_trigger_bits(float l, float r) { return (l > 0.5f ? DM_BTN_ZL : 0) | (r > 0.5f ? DM_BTN_ZR : 0); }

// The game picture in a W × H window for a game of gw × gh: r = {x, y, w, h}
static inline void dm_and_letterbox(float W, float H, float gw, float gh, float r[4]) {
    r[0] = 0; r[1] = 0; r[2] = W; r[3] = H;
    if (W <= 0 || H <= 0 || gw <= 0 || gh <= 0) return;
    float ga = gw / gh, wa = W / H;
    if (fabsf(wa / ga - 1) < 0.01f) return;
    if (wa > ga) r[2] = floorf(H * ga); else r[3] = floorf(W / ga);
    r[0] = floorf((W - r[2]) / 2); r[1] = floorf((H - r[3]) / 2);
}
// A finger at window pixel (x, y): 1 with its normalised place when inside the picture; outside, 0 (clamped to the edge in *nx / *ny)
static inline int dm_and_touch_norm(const float r[4], float x, float y, float *nx, float *ny) {
    float u = r[2] > 0 ? (x - r[0]) / r[2] : 0, v = r[3] > 0 ? (y - r[1]) / r[3] : 0;
    int in = u >= 0 && u < 1 && v >= 0 && v < 1;
    *nx = fminf(fmaxf(u, 0), .99999f); *ny = fminf(fmaxf(v, 0), .99999f);
    return in;
}
