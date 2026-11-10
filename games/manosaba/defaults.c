// manosaba's defaults: depth and stencil targets made without memory (DM_NVN_DS_MEMORYLESS=0 turns it off; gfx/nvn/mtl/rtheap.inc dsml_*, plan item
// 280). The game clears every such target at the start of each pass that attaches it and binds none as a texture, so what they held between
// passes was never read: 281-574 MB of them at its first trial's opening. An explicit setting wins. Checked by host/tests t_mano_defaults.
#include <stdlib.h>

__attribute__((constructor)) static void mano_defaults(void) {
#ifndef DM_TEST_MANO_DS_MEMORY   // negative control of host/tests t_mano_defaults only
    setenv("DM_NVN_DS_MEMORYLESS", "1", 0);   // 0: an explicit setting wins
#endif
}
