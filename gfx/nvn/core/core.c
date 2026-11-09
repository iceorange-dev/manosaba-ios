// gfx/nvn/core/core.c: what core.h declares
#include "core.h"
#include <string.h>

int g_red_once[123];
_Atomic uint64_t g_cur_frame;
uint64_t g_stat_draw, g_stat_pass;
_Atomic long g_pd_a0 = -2, g_pd_a1;
long passdump_read(void) {
    const char *e = getenv("DM_NVN_PASSDUMP"); long b0 = e ? atol(e) : -1, b1 = e && strchr(e, '-') ? atol(strchr(e, '-') + 1) : b0;
    long exp = -2; if (atomic_compare_exchange_strong(&g_pd_a0, &exp, b0)) atomic_store(&g_pd_a1, b1);
    return atomic_load(&g_pd_a0);
}
void mtl_passdump_next(long n) {
    long f = (long)atomic_load(&g_cur_frame); atomic_store(&g_pd_a0, -1); atomic_store(&g_pd_a1, f + n); atomic_store(&g_pd_a0, f + 1);
    MLOG("pass records of frames %ld-%ld (command channel)", f + 1, f + n);
}
