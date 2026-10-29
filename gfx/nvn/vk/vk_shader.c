// gfx/nvn/vk/vk_shader.c: the game's shaders as Vulkan modules (plan item 301e2), from the bundle gfx/tools/dmsh_vk.py writes (its docstring has the
// format): mapped from DM_VK_SHADERS (the app puts the product's shaders.dmvk there), a shader found by its key (the first 8 bytes of the sha256 the
// interface layer takes at SetShaders, prog_t key), its module made once and kept with its reflection. Every stage's bindings were moved by the
// bundle so a program's stages share one descriptor set (vertex 0-63, pixel 64-127).
#include "vk.h"
#include "vk_shader.h"
#include "../nvn_hmap.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

typedef struct { uint64_t key; uint32_t stage, spv_off, spv_len, refl_off, refl_len; } __attribute__((packed)) ent;
static const uint8_t *g_map; static uint32_t g_n; static const ent *g_ent;
static nvn_hmap g_mods = {.klen = 8};   // key → vk_sh
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static void open_bundle(void) {
    const char *p = getenv("DM_VK_SHADERS");
    if (!p || !*p) { VLOG("[red] no shader bundle (DM_VK_SHADERS): nothing can be drawn"); return; }
    int fd = open(p, O_RDONLY | O_CLOEXEC); struct stat st;
    if (fd < 0 || fstat(fd, &st) || st.st_size < 12) { VLOG("[red] the shader bundle %s cannot be read", p); if (fd >= 0) close(fd); return; }
    const uint8_t *m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0); close(fd);
    if (m == MAP_FAILED || memcmp(m, "DMVK", 4) || ((const uint32_t *)m)[1] != 1) { VLOG("[red] %s is not a version 1 shader bundle", p); return; }
    g_n = ((const uint32_t *)m)[2]; g_ent = (const ent *)(m + 12); g_map = m;
    if (12 + (uint64_t)g_n * sizeof(ent) > (uint64_t)st.st_size) { VLOG("[red] the shader bundle %s is cut short", p); g_map = NULL; return; }
    VLOG("shader bundle %s: %u shaders, %.1f MB", p, g_n, st.st_size / 1e6);
}
static const ent *find(uint64_t key) {
    uint32_t lo = 0, hi = g_n;
    while (lo < hi) { uint32_t m = (lo + hi) / 2; uint64_t k; memcpy(&k, &g_ent[m].key, 8); if (k < key) lo = m + 1; else hi = m; }
    if (lo < g_n) { uint64_t k; memcpy(&k, &g_ent[lo].key, 8); if (k == key) return &g_ent[lo]; }
    return NULL;
}
static int parse_refl(const uint8_t *p, uint32_t n, vk_sh *s) {
    if (n < 16) return -1;
    uint16_t h[8]; memcpy(h, p, 16);
    s->nb = h[0]; s->nv = h[1]; s->nout = h[2]; s->c1_len = h[3]; s->local[0] = h[4]; s->local[1] = h[5]; s->local[2] = h[6];
    if (s->nb > VK_SH_MAXB || s->nv > 32 || s->nout > 8 || 16u + s->nb * 14u + (s->nv + s->nout) * 2u + s->c1_len > n) return -1;
    const uint8_t *q = p + 16;
    for (int i = 0; i < s->nb; i++, q += 14) {
        vk_bind *b = &s->b[i]; b->kind = q[0]; b->dtype = q[1]; memcpy(&b->binding, q + 2, 2); memcpy(&b->count, q + 4, 2);
        memcpy(&b->reg, q + 6, 2); memcpy(&b->sreg, q + 8, 2); b->rw = q[10]; b->ty = q[11]; memcpy(&b->off, q + 12, 2);
    }
    for (int i = 0; i < s->nv; i++, q += 2) { s->vin[i].loc = q[0]; s->vin[i].ct = q[1]; }
    for (int i = 0; i < s->nout; i++, q += 2) { s->vout[i].loc = q[0]; s->vout[i].ct = q[1]; }
    s->c1 = q;
    return 0;
}
// The shader of this key, its module made on first use; NULL when the bundle does not have it (logged once a key)
const vk_sh *vk_shader(const uint8_t key8[8]) {
    pthread_once(&g_once, open_bundle);
    uint64_t key = 0; for (int i = 0; i < 8; i++) key = key << 8 | key8[i];   // the bundle's key: the 16 hex digits as a number
    pthread_mutex_lock(&g_mu);
    vk_sh *s = nvn_hm_get(&g_mods, &key);
    if (!s) {
        s = calloc(1, sizeof *s); s->key = key;
        const ent *e = g_map ? find(key) : NULL;
        if (!e) { VLOG("[red] shader %016llx is not in the bundle: its draws are dropped", (unsigned long long)key); s->missing = 1; }
        else if (parse_refl(g_map + e->refl_off, e->refl_len, s)) { VLOG("[red] shader %016llx: its reflection does not read", (unsigned long long)key); s->missing = 1; }
        else {
            s->stage = (int)e->stage;
            VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = e->spv_len, .pCode = (const uint32_t *)(g_map + e->spv_off)};
            VkResult r = vkCreateShaderModule(V.dev, &ci, NULL, &s->mod);
            if (r != VK_SUCCESS) { VLOG("[red] shader %016llx: vkCreateShaderModule %d", (unsigned long long)key, (int)r); s->missing = 1; }
        }
        nvn_hm_put(&g_mods, &key, s);
    }
    pthread_mutex_unlock(&g_mu);
    return s->missing ? NULL : s;
}
