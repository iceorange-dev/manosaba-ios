// android/settings.c (plan item 301g): the panel's settings file and env.txt at the app's start and while it runs. The settings taken; env.txt's
// value winning over the file's; DM_RENDER_HEIGHT=native becoming the K100's 2132x1200 (host/native_size.h); the file read again while running
// changes dm_setting's answer and leaves the environment alone; DM_SETTINGS=0 in env.txt leaving the file out; an explicit DM_DISPLAY_SIZE kept
// with native; native with the screen's size unknown leaving the game's. The file over env.txt (-DDM_TEST_SETTINGS_OVER_ENV): 2 fail; the file
// taken whatever DM_SETTINGS says (-DDM_TEST_SETTINGS_ALWAYS): 1 fails.
#include "../settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static void put(const char *path, const char *text) { FILE *f = fopen(path, "w"); fputs(text, f); fclose(f); }
static int k100(unsigned *w, unsigned *h) { *w = 2608; *h = 1200; return 1; }
static int unknown(unsigned *w, unsigned *h) { (void)w; (void)h; return 0; }
static const char *env(const char *name) { const char *v = getenv(name); return v ? v : "(unset)"; }
static void clean(void) { const char *names[] = {"DM_FPS", "DM_HOT_HOLD", "DM_CMD", "DM_RENDER_HEIGHT", "DM_DISPLAY_SIZE", "DM_SETTINGS", "DM_PERF_LINE"}; for (unsigned i = 0; i < 7; i++) unsetenv(names[i]); }

int main(void) {
    char v[64];
    put("t_settings.txt", "DM_FPS=30\nDM_HOT_HOLD=0\nDM_RENDER_HEIGHT=native\nHOME=/nope\n"); put("t_env.txt", "DM_HOT_HOLD=1\nDM_CMD=/x\n");
    clean(); dm_settings_boot("t_settings.txt", "t_env.txt", k100, NULL);
    CHECK(!strcmp(env("DM_FPS"), "30") && !strcmp(env("DM_CMD"), "/x") && strcmp(env("HOME"), "/nope"), "the settings and env.txt taken, a name not DM_ left out (DM_FPS %s, DM_CMD %s)", env("DM_FPS"), env("DM_CMD"));
    CHECK(!strcmp(env("DM_HOT_HOLD"), "1"), "env.txt's DM_HOT_HOLD over the file's (%s)", env("DM_HOT_HOLD"));
    CHECK(!strcmp(env("DM_RENDER_HEIGHT"), "1200") && !strcmp(env("DM_DISPLAY_SIZE"), "2132x1200"), "native on 2608x1200: rendering %s lines, the display %s", env("DM_RENDER_HEIGHT"), env("DM_DISPLAY_SIZE"));
    CHECK(dm_setting("DM_HOT_HOLD", v, sizeof v) && !strcmp(v, "1"), "while running, dm_setting answers env.txt's value too (%s)", v);
    put("t_settings.txt", "DM_FPS=60\nDM_PERF_LINE=1\n"); dm_settings_reload();
    CHECK(dm_setting("DM_FPS", v, sizeof v) && !strcmp(v, "60") && dm_setting("DM_PERF_LINE", v, sizeof v) && !strcmp(env("DM_FPS"), "30"),
          "the file read again: dm_setting says 60 and the new switch, the environment still 30 (%s)", env("DM_FPS"));
    put("t_settings.txt", "DM_FPS=30\n"); put("t_env.txt", "DM_SETTINGS=0\n");
    clean(); dm_settings_boot("t_settings.txt", "t_env.txt", k100, NULL);
    CHECK(!strcmp(env("DM_FPS"), "(unset)") && !dm_setting("DM_FPS", v, sizeof v), "DM_SETTINGS=0 in env.txt: the file left out (DM_FPS %s)", env("DM_FPS"));
    put("t_settings.txt", "DM_RENDER_HEIGHT=native\n"); put("t_env.txt", "DM_DISPLAY_SIZE=1920x1080\n");
    clean(); dm_settings_boot("t_settings.txt", "t_env.txt", k100, NULL);
    CHECK(!strcmp(env("DM_DISPLAY_SIZE"), "1920x1080") && !strcmp(env("DM_RENDER_HEIGHT"), "1200"), "native with env.txt's DM_DISPLAY_SIZE: that kept (%s), 1200 lines", env("DM_DISPLAY_SIZE"));
    put("t_env.txt", "");
    clean(); dm_settings_boot("t_settings.txt", "t_env.txt", unknown, NULL);
    CHECK(!strcmp(env("DM_RENDER_HEIGHT"), "(unset)") && !strcmp(env("DM_DISPLAY_SIZE"), "(unset)"), "native with the screen's size unknown: the game's own (%s)", env("DM_RENDER_HEIGHT"));
    remove("t_settings.txt"); remove("t_env.txt");
    printf("t_settings: %d checks, %d failed\n", n, fail); return fail != 0;
}
