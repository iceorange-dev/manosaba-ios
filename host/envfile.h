// Environment switches from a text file, for builds started without a debugger or launch arguments (the iOS app: the bundle's dm/env.txt, baked in
// by ios/run.sh from DM_ENV_FILE, then Documents/env.txt, which the Files app can edit; nxnative 5aabb96, MIT). One NAME=value a line; blank lines and
// lines starting with # are skipped; spaces around name and value and a CR at the end are dropped; only names starting with DM_ or MTL_ are taken (so
// a stray file cannot change HOME or DYLD_*). Each applied switch is reported through say. Returns how many were applied, -1 when there is no file.
// Checked by host/tests/t_envfile.c
#pragma once
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Each switch of the file in order through fn (a name twice: twice, the last one last); returns how many, -1 when there is no file
static inline int dm_env_file_each(const char *path, void (*fn)(const char *name, const char *value, void *ctx), void *ctx) {
    FILE *f = fopen(path, "r"); if (!f) return -1;
    char line[1024]; int n = 0;
    while (fgets(line, sizeof line, f)) {
        char *s = line; while (isspace((unsigned char)*s)) s++;
        if (!*s || *s == '#') continue;
        char *eq = strchr(s, '='); if (!eq) continue;
        char *ne = eq; while (ne > s && isspace((unsigned char)ne[-1])) ne--; *ne = 0;
        char *v = eq + 1; while (*v == ' ' || *v == '\t') v++;
        char *ve = v + strlen(v); while (ve > v && isspace((unsigned char)ve[-1])) ve--; *ve = 0;
        if (strncmp(s, "DM_", 3) && strncmp(s, "MTL_", 4)) continue;
        for (char *c = s; *c; c++) if (!isalnum((unsigned char)*c) && *c != '_') { s = NULL; break; }
        if (!s) continue;
        fn(s, v, ctx); n++;
    }
    fclose(f); return n;
}
static inline void dm_env_set_(const char *name, const char *value, void *ctx) {
    setenv(name, value, 1); void (*say)(const char *, const char *) = (void (*)(const char *, const char *))ctx; if (say) say(name, value);
}
static inline int dm_env_file_apply(const char *path, void (*say)(const char *name, const char *value)) { return dm_env_file_each(path, dm_env_set_, (void *)say); }
