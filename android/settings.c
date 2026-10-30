// The player's settings in the Android app (plan item 301g): the in-game panel (android/java/.../Panel.java, opened by Back or a three-finger tap)
// writes them to a file in the app's own folder; env.txt (the external folder, adb's) is for tests and hand-set switches and wins over them, so an
// unattended test's env.txt never loses or changes the player's choices (and DM_SETTINGS=0 there leaves them out of the test). See settings.h
#include "settings.h"
#include "envfile.h"
#include "native_size.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAXS = 64 };
typedef struct { int n; char name[MAXS][48], value[MAXS][128]; } kv_t;
static kv_t g_env, g_set;   // env.txt as read at the start; the settings file as last read
static char g_set_path[512];
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static void put(const char *name, const char *value, void *ctx) {   // (a name twice: the last line's value, as setenv would leave it)
    kv_t *k = ctx; int i = 0; while (i < k->n && strcmp(k->name[i], name)) i++;
    if (i == k->n) { if (k->n >= MAXS) return; k->n++; }
    snprintf(k->name[i], sizeof k->name[0], "%s", name); snprintf(k->value[i], sizeof k->value[0], "%s", value);
}
static void read_kv(const char *path, kv_t *k) { k->n = 0; if (path && *path) dm_env_file_each(path, put, k); }   // a file's switches, the environment untouched
static const char *kv_get(const kv_t *k, const char *name) { for (int i = 0; i < k->n; i++) if (!strcmp(k->name[i], name)) return k->value[i]; return NULL; }
// A switch's value: env.txt's when it has one, else the settings file's
static const char *lookup(const char *name, const char **who) {
    const char *e = kv_get(&g_env, name), *s = kv_get(&g_set, name);
#ifdef DM_TEST_SETTINGS_OVER_ENV   // negative control of tests/t_settings.c only: the settings file over env.txt
    if (s) { *who = "settings"; return s; }
#endif
    if (e) { *who = "env.txt"; return e; }
    *who = "settings"; return s;
}
static int settings_off(void) {   // DM_SETTINGS=0 in env.txt
#ifdef DM_TEST_SETTINGS_ALWAYS   // negative control of tests/t_settings.c only: the settings file taken whatever env.txt says
    return 0;
#endif
    const char *off = kv_get(&g_env, "DM_SETTINGS"); return off && !strcmp(off, "0");
}

void dm_settings_boot(const char *settings_path, const char *env_path, int (*display)(unsigned *w, unsigned *h),
                      void (*say)(const char *who, const char *name, const char *value)) {
    pthread_mutex_lock(&g_mu);
    snprintf(g_set_path, sizeof g_set_path, "%s", settings_path ? settings_path : "");
    read_kv(env_path, &g_env);
    if (settings_off()) g_set.n = 0; else read_kv(g_set_path, &g_set);
    const kv_t *both[2] = {&g_set, &g_env};
    for (int f = 0; f < 2; f++) for (int i = 0; i < both[f]->n; i++) {
        const char *name = both[f]->name[i], *who, *v = lookup(name, &who);
        if (f == 1 && kv_get(&g_set, name)) continue;   // (set already, with the winner's value)
        setenv(name, v, 1); if (say) say(who, name, v);
    }
    const char *rh = getenv("DM_RENDER_HEIGHT");
    if (rh && !strcmp(rh, "native")) {   // the screen's 16:9 picture (an explicit DM_DISPLAY_SIZE kept)
        unsigned w, h, W, H; char b[32];
        if (display && display(&w, &h) && dm_native_size(w, h, &W, &H)) {
            snprintf(b, sizeof b, "%ux%u", W, H); setenv("DM_DISPLAY_SIZE", b, 0); snprintf(b, sizeof b, "%u", H); setenv("DM_RENDER_HEIGHT", b, 1);
            if (say) say("native", "DM_RENDER_HEIGHT", b);
        } else { unsetenv("DM_RENDER_HEIGHT"); if (say) say("native", "DM_RENDER_HEIGHT", "(the screen's size unknown: the game's)"); }
    }
    pthread_mutex_unlock(&g_mu);
}
int dm_setting(const char *name, char *out, size_t n) {
    pthread_mutex_lock(&g_mu);
    const char *who, *v = lookup(name, &who); if (v) snprintf(out, n, "%s", v);
    pthread_mutex_unlock(&g_mu);
    return v != NULL;
}
void dm_settings_reload(void) {
    pthread_mutex_lock(&g_mu);
    if (!settings_off()) read_kv(g_set_path, &g_set);
    pthread_mutex_unlock(&g_mu);
}
