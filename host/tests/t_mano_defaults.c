// manosaba's environment defaults (games/manosaba/defaults.c): depth and stencil targets made without memory (plan item 280), and an explicit
// setting kept (DM_NVN_DS_MEMORYLESS=0 stays 0 when the defaults run again). Without the line (-DDM_TEST_MANO_DS_MEMORY): 1 fails.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../games/manosaba/defaults.c"
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "\xe2\x9c\x93" : "\xe2\x9c\x97"); }
static int is(const char *k, const char *v) { const char *e = getenv(k); return e && !strcmp(e, v); }
int main(void) {
    check("DM_NVN_DS_MEMORYLESS=1 by default (the constructor ran before main)", is("DM_NVN_DS_MEMORYLESS", "1"));
    setenv("DM_NVN_DS_MEMORYLESS", "0", 1); mano_defaults();
    check("an explicit DM_NVN_DS_MEMORYLESS=0 kept when the defaults run again", is("DM_NVN_DS_MEMORYLESS", "0"));
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
