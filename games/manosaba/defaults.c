// manosaba's defaults: depth and stencil targets made without memory (DM_NVN_DS_MEMORYLESS=0 turns it off; gfx/nvn/mtl/rtheap.inc dsml_*, plan item
// 280). The game clears every such target at the start of each pass that attaches it and binds none as a texture, so what they held between
// passes was never read: 281-574 MB of them at its first trial's opening. And the cross-submission validity ledger off (DM_NVN_XV=1 turns it on;
// gfx/nvn/core/tbdr.c): it leaves a colour target unstored once a few frames have cleared it before reading it, and names it in red when a later
// frame reads it after all. Here it saves nothing (render targets stored a frame and the GPU's time the same with it on and off, from the title
// through the first trial's opening into its debate) and every run of that opening read seven targets it had left unstored. An explicit setting
// wins. Checked by host/tests t_mano_defaults.
#include <stdlib.h>

__attribute__((constructor)) static void mano_defaults(void) {
#ifndef DM_TEST_MANO_DS_MEMORY   // negative control of host/tests t_mano_defaults only
    setenv("DM_NVN_DS_MEMORYLESS", "1", 0);   // 0: an explicit setting wins
#endif
#ifndef DM_TEST_MANO_XV_ON   // negative control of host/tests t_mano_defaults only
    setenv("DM_NVN_XV", "0", 0);
#endif
}
