// manosaba's resolution following the screen (games/manosaba/graphics.c, plan item 302, the iPhone Duo with the setting "Native"): the window
// textures made at the inner display's 2852x1604 (DM_DISPLAY_SIZE), started on the outer display (DM_RENDER_HEIGHT=1144), Screen.SetResolution's
// four-argument overload replaced by a recorder, the main loop's step (observation gate 1) called by hand:
//   the inner display reported before the game set its resolution: no call yet, and the game's own call then renders 2852x1604 (not the boot 1144);
//   the outer display reported: 2034x1144 from the step; the game's own call again keeps 2034x1144; the same screen again: no call;
//   a 2160x3840 screen: at most the textures, 2852x1604; a step with nothing reported: no call;
//   with no DM_DISPLAY_SIZE (1920x1080) and DM_RENDER_HEIGHT=720 the game's 2560x1440 renders 1280x720 as before (t_mano_follow 720).
// DM_RENDER_HEIGHT as at boot whatever the step applied (-DDM_TEST_FOLLOW_BOOT_HEIGHT): 1 fails.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "dm.h"
module_t g_mod[MAX_MOD]; int g_nmod;
void dm_host_call_register(int num, int (*fn)(uint64_t *regs)) { (void)num; (void)fn; }
static void (*g_gate)(const uint64_t *regs);
void gate_obs_set(int id, void (*fn)(const uint64_t *regs)) { if (id == 1) g_gate = fn; }
#include "../../games/manosaba/graphics.c"
static int calls; static int cw, ch;
static void fake_set4(int w, int h, int mode, uint64_t rr) { (void)mode; (void)rr; calls++; cw = w; ch = h; }
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-110s %s\n", what, ok ? "✓" : "✗"); }
static void game_sets(void) { uint64_t r[8] = {2560, 1440, 1}; hc_set_resolution(r); }
static void step(void) { uint64_t regs[32] = {0}; g_gate(regs); }
int main(int argc, char **argv) {
    static uint8_t fake_main[0x4000000];
    g_nmod = 1; strcpy(g_mod[0].name, "main"); g_mod[0].base = fake_main;
    if (argc > 1) {   // the other settings: no display reported, DM_RENDER_HEIGHT=720
        unsetenv("DM_DISPLAY_SIZE"); setenv("DM_RENDER_HEIGHT", "720", 1);
        dm_game_boot(); g_set_resolution4 = fake_set4; game_sets();
        check("no DM_DISPLAY_SIZE, DM_RENDER_HEIGHT=720: the game's 2560x1440 renders 1280x720 as before", calls == 1 && cw == 1280 && ch == 720);
        printf("%d checks, %d failed\n", n, fail); return fail != 0;
    }
    setenv("DM_DISPLAY_SIZE", "2852x1604", 1); setenv("DM_RENDER_HEIGHT", "1144", 1);
    dm_game_boot(); g_set_resolution4 = fake_set4;
    check("the main loop's step attached to observation gate 1", g_gate != NULL);
    dm_game_native_screen(2007, 2853); step();
    check("the inner display reported before the game set its resolution: no call from the step", calls == 0);
    game_sets();
    check("... the game's own call then renders 2852x1604 (the inner display's native, not the boot's 1144 lines)", calls == 1 && cw == 2852 && ch == 1604);
    dm_game_native_screen(1398, 2034); step();
    check("the outer display reported: 2034x1144 from the step", calls == 2 && cw == 2034 && ch == 1144);
    game_sets();
    check("the game's own call again keeps 2034x1144", calls == 3 && cw == 2034 && ch == 1144);
    dm_game_native_screen(1398, 2034); step();
    check("the same screen again: no call", calls == 3);
    dm_game_native_screen(2160, 3840); step();
    check("a 2160x3840 screen: at most the window textures, 2852x1604", calls == 4 && cw == 2852 && ch == 1604);
    step();
    check("a step with nothing reported: no call", calls == 4);
    printf("%d checks, %d failed\n", n, fail);
    return fail != 0;
}
