// The player's settings in the Android app (plan item 301g; android/settings.c)
#pragma once
#include <stddef.h>
// At the start, before the host: env.txt's switches (tests, DM_ switches by hand; host/envfile.h) and the settings file the panel writes (one
// NAME=value a line, the same format), env.txt's winning; DM_SETTINGS=0 in env.txt leaves the settings file out (the unattended tests). Both become
// the environment the host reads; DM_RENDER_HEIGHT=native becomes the screen's 16:9 picture (host/native_size.h) from display(w, h) (1 when known).
// say reports each switch taken (who: "env.txt" or "settings")
void dm_settings_boot(const char *settings_path, const char *env_path, int (*display)(unsigned *w, unsigned *h),
                      void (*say)(const char *who, const char *name, const char *value));
// While the game runs, without touching the environment (other threads read it): env.txt's value, else the settings file's as last read; 1 when set
int dm_setting(const char *name, char *out, size_t n);
void dm_settings_reload(void);   // the settings file read again (the panel changed it)
