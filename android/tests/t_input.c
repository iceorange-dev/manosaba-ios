// android/input_map.h, what the app makes of Android's input (plan item 301d), on the phone: a controller's keys as the Switch's buttons (A / B / X /
// Y by the system's names, by position with DM_FACE_BUTTONS=position; the shoulders, triggers, stick buttons, Start / Select, the d-pad), keys that
// are not the game's (volume, Back) left alone; sticks up positive and saturated, the hat and the triggers as buttons; the game picture letterboxed
// in the window and a finger normalised to it, one outside it not the game's. -DDM_TEST_INPUT_Y_DOWN (the sticks' Y as Android gives it): 2 fail.
#include "input_map.h"
#include <stdio.h>
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    CHECK(dm_and_key_bit(AKEYCODE_BUTTON_A, 0) == DM_BTN_A && dm_and_key_bit(AKEYCODE_BUTTON_B, 0) == DM_BTN_B && dm_and_key_bit(AKEYCODE_BUTTON_X, 0) == DM_BTN_X &&
          dm_and_key_bit(AKEYCODE_BUTTON_Y, 0) == DM_BTN_Y, "A / B / X / Y by the system's names");
    CHECK(dm_and_key_bit(AKEYCODE_BUTTON_A, 1) == DM_BTN_B && dm_and_key_bit(AKEYCODE_BUTTON_B, 1) == DM_BTN_A && dm_and_key_bit(AKEYCODE_BUTTON_X, 1) == DM_BTN_Y &&
          dm_and_key_bit(AKEYCODE_BUTTON_Y, 1) == DM_BTN_X, "by position: bottom B, right A, left Y, top X");
    CHECK(dm_and_key_bit(AKEYCODE_BUTTON_L1, 0) == DM_BTN_L && dm_and_key_bit(AKEYCODE_BUTTON_R2, 0) == DM_BTN_ZR && dm_and_key_bit(AKEYCODE_BUTTON_THUMBL, 0) == DM_BTN_LS &&
          dm_and_key_bit(AKEYCODE_BUTTON_START, 0) == DM_BTN_PLUS && dm_and_key_bit(AKEYCODE_BUTTON_SELECT, 0) == DM_BTN_MINUS && dm_and_key_bit(AKEYCODE_DPAD_LEFT, 0) == DM_BTN_LEFT,
          "shoulders, triggers, stick buttons, Start / Select, the d-pad");
    CHECK(!dm_and_key_bit(AKEYCODE_VOLUME_UP, 0) && !dm_and_key_bit(AKEYCODE_BACK, 0) && !dm_and_key_bit(AKEYCODE_BUTTON_MODE, 0), "volume, Back and the controller's home key are not the game's");
    CHECK(dm_and_axis(-1.0f, 1) == DM_STICK_MAX && dm_and_axis(1.0f, 1) == -DM_STICK_MAX, "a stick pushed up (Android's -1) is +32767, down -32767 (%d, %d)", dm_and_axis(-1.0f, 1), dm_and_axis(1.0f, 1));
    CHECK(dm_and_axis(0.5f, 0) == 16384 && dm_and_axis(1.7f, 0) == DM_STICK_MAX && dm_and_axis(0.0f / 0.0f, 1) == 0, "x as it is, past the end saturated, NaN at rest (%d)", dm_and_axis(0.5f, 0));
    CHECK(dm_and_axis(-0.25f, 1) > 0, "a quarter up is up");
    CHECK(dm_and_hat_bits(-1, 0) == DM_BTN_LEFT && dm_and_hat_bits(1, -1) == (DM_BTN_RIGHT | DM_BTN_UP) && dm_and_hat_bits(0.2f, 0.3f) == 0, "the hat as the d-pad");
    CHECK(dm_and_trigger_bits(0.6f, 0.4f) == DM_BTN_ZL && dm_and_trigger_bits(0, 1) == DM_BTN_ZR, "a trigger past half way is ZL / ZR");
    float r[4]; dm_and_letterbox(2800, 1264, 1920, 1080, r);
    CHECK(r[2] == 2247 && r[3] == 1264 && r[0] == 276 && r[1] == 0, "2800 x 1264 window, 16:9 game: 2247 x 1264 at x 276 (%.0f x %.0f at %.0f, %.0f)", r[2], r[3], r[0], r[1]);
    dm_and_letterbox(1924, 1080, 1920, 1080, r); CHECK(r[0] == 0 && r[2] == 1924, "a window within 1%% of the game's shape: filled");
    dm_and_letterbox(2800, 1264, 1920, 1080, r); float x, y;
    CHECK(dm_and_touch_norm(r, 276 + 2247 / 2.0f, 632, &x, &y) && x > 0.499f && x < 0.501f && y == 0.5f, "the picture's centre is (0.5, 0.5) (%.4f, %.4f)", x, y);
    CHECK(!dm_and_touch_norm(r, 100, 600, &x, &y) && x == 0, "a finger left of the picture is not the game's (clamped %.3f)", x);
    CHECK(dm_and_touch_norm(r, 276, 0, &x, &y) && x == 0 && y == 0 && !dm_and_touch_norm(r, 276 + 2247, 10, &x, &y) && x < 1, "the top left corner is (0, 0); the right edge is outside, clamped below 1");
    printf("t_input: %d checks, %d failed\n", n, fail); return fail != 0;
}
