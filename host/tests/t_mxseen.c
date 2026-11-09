// MetricKit's reports taken once (ios/Sources/mxseen.h, plan item 317a): a report's key is new the first time, known after (also in a later
// "launch", the file read again), another span or kind is new; a file that cannot be written says so. Every report taken as new
// (-DDM_TEST_MX_SEEN_NEVER): 2 fail.
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../../ios/Sources/mxseen.h"
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("  %s %s\n", ok ? "✓" : "✗", what); }
int main(void) {
    char path[512]; snprintf(path, sizeof path, "%s/t_mxseen.txt", getenv("TMPDIR") ? getenv("TMPDIR") : "."); unlink(path);
    int a = mx_seen_add(path, "diag 1794700000 1794786400"), b = mx_seen_add(path, "diag 1794700000 1794786400");
    int c = mx_seen_add(path, "metric 1794700000 1794786400"), d = mx_seen_add(path, "diag 1794786400 1794872800");
    check("a diagnostic report new, then known [-DDM_TEST_MX_SEEN_NEVER: new again]", a == 1 && b == 0);
    check("the metric report of the same span, and the next span, new", c == 1 && d == 1);
    check("... and known at the next launch [-DDM_TEST_MX_SEEN_NEVER: new again]", mx_seen_add(path, "metric 1794700000 1794786400") == 0);
    check("a file that cannot be written: -1", mx_seen_add("/nonexistent-dir/x/seen.txt", "k") == -1);
    unlink(path);
    printf("t_mxseen: %d checks, %d failed\n", n, fail); return fail != 0;
}
