// Default (no-op) game hooks; a game directory overrides them with strong definitions (game.h).
#include "game.h"
__attribute__((weak)) void dm_game_boot(void) {}
__attribute__((weak)) void dm_game_mount_save(const char *dir) { (void)dir; }
__attribute__((weak)) void dm_game_save_precommit(const char *workdir) { (void)workdir; }
__attribute__((weak)) int dm_game_inactivity_fps(int on, uint64_t ra) { (void)on; (void)ra; return 0; }
__attribute__((weak)) int dm_game_command(const char *line, int run) { (void)line; (void)run; return 0; }
__attribute__((weak)) void dm_game_native_screen(unsigned w, unsigned h) { (void)w; (void)h; }

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int dm_engine_il2cpp(void) { return !strcmp(dm_game.engine, "unity"); }   // crash.c names the IL2CPP objects registers point to (plan item 141)
const char *dm_game_ws_path(const char *rel, char *buf, unsigned long n) {
    const char *root = getenv("DM_ROOT");
    if (root) snprintf(buf, n, "%s/%s", root, rel); else snprintf(buf, n, "%s/dm-work/%s", getenv("HOME") ?: ".", rel);
    return buf;
}
void dm_game_env_defaults(void) {
    static const struct { const char *var; int off; } map[] = {
        {"DM_ROMFS", 0}, {"DM_SYSFONT", 1}, {"DM_NVN_SHMAP", 3}, {"DM_MSL_DIR", 4},
    };
    const char *ws[] = {dm_game.ws.romfs, dm_game.ws.sysfont, dm_game.ws.product, dm_game.ws.shader_map, dm_game.ws.msl};
    char buf[1024];
    for (unsigned i = 0; i < sizeof map / sizeof *map; i++)
        if (!getenv(map[i].var) && ws[map[i].off]) setenv(map[i].var, dm_game_ws_path(ws[map[i].off], buf, sizeof buf), 0);
}
