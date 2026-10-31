// manosaba's display size host call (games/manosaba/graphics.c hc_display_size, the cut of main+0x90c990 / +0x99bcc0 in rules.py): the size it
// writes through the guest's two pointers, for the DM_DISPLAY_SIZE the run gives: t_mano_display <want width> <want height>. The Makefile runs it
// unset (1920x1080, the constant it replaces), with 2241x1261 (2240x1260: even), and with a malformed value (1920x1080).
// -DDM_TEST_DISPLAY_CONST (the constant whatever is asked): the 2241x1261 run fails.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "dm.h"
module_t g_mod[MAX_MOD]; int g_nmod;
void dm_host_call_register(int num, int (*fn)(uint64_t *regs)) { (void)num; (void)fn; }
void gate_obs_set(int id, void (*fn)(const uint64_t *regs)) { (void)id; (void)fn; }
#include "../../games/manosaba/graphics.c"
int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: t_mano_display <width> <height>\n"); return 2; }
    unsigned ww = (unsigned)atoi(argv[1]), wh = (unsigned)atoi(argv[2]);
    uint32_t w = 0xdead, h = 0xbeef; uint64_t r[8] = {0, (uint64_t)(uintptr_t)&w, (uint64_t)(uintptr_t)&h};
    int ret = hc_display_size(r);
    int ok = ret == 1 && w == ww && h == wh;
    printf("%s DM_DISPLAY_SIZE=%s: the display reported as %ux%u (expected %ux%u)\n", ok ? "✓" : "✗", getenv("DM_DISPLAY_SIZE") ?: "(unset)", w, h, ww, wh);
    return !ok;
}
