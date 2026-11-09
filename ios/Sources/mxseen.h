// MetricKit's reports taken once (plan item 317a, metrickit.m): the system hands a report again (the earlier ones at each launch, and a
// subscriber's call), so each is known by its kind and its own time span ("diag 1794700000 1794786400") in a file of the keys taken. Header only:
// host/tests/t_mxseen.c checks it on the Mac
#pragma once
#include <stdio.h>
#include <string.h>
// 1 when key was not in the file at path (it is added), 0 when it was; -1 when the file cannot be written
static inline int mx_seen_add(const char *path, const char *key) {
    char line[256]; FILE *f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            size_t n = strlen(line); while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
#ifndef DM_TEST_MX_SEEN_NEVER   // negative control of t_mxseen only: every report taken as new
            if (!strcmp(line, key)) { fclose(f); return 0; }
#endif
        }
        fclose(f);
    }
    f = fopen(path, "a"); if (!f) return -1;
    fprintf(f, "%s\n", key); fclose(f); return 1;
}
