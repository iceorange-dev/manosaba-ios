// gfx/nvn/vk/vk_shot.c: screenshots of presented frames (plan item 301e3), as the Metal backend's: DM_NVN_SHOT=frame[,frame...]:path (%d in the
// path is the frame number) and the command channel's shot (dm_gfx_shot_at: k presents after the current one). At that present the window
// texture's crop is read back (ReadTexels' way: the GPU's content after the work submitted so far) and written as an 8-bit RGB PNG with zlib's
// deflate and crc32, with or without a window (the picture the game presented, not what the screen shows over it: adb's screencap has the
// system's overlays, HyperOS's pocket mode among them).
#include "vk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

enum { SHOTQ_MAX = 8 };
static struct { uint64_t frame; char path[1024]; } g_q[SHOTQ_MAX]; static int g_nq;
static pthread_mutex_t g_qmu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_presented;
void dm_gfx_shot_at(long k, const char *path) {
    pthread_mutex_lock(&g_qmu);
    if (g_nq < SHOTQ_MAX) { g_q[g_nq].frame = g_presented + (uint64_t)(k > 0 ? k : 0) + 1; snprintf(g_q[g_nq].path, sizeof g_q[0].path, "%s", path); g_nq++; }
    else VLOG("[red] more than %d screenshot requests: %s dropped", SHOTQ_MAX, path);
    pthread_mutex_unlock(&g_qmu);
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static int chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n) {
    uint8_t h[8], t[4]; put32(h, n); memcpy(h + 4, type, 4);
    uLong c = crc32(crc32(0, h + 4, 4), d, n); put32(t, (uint32_t)c);
    return fwrite(h, 1, 8, f) == 8 && (!n || fwrite(d, 1, n, f) == n) && fwrite(t, 1, 4, f) == 4;
}
// rgba: w × h texels of 4 bytes, rows top first, row bytes apart; bgra: blue first
static int png_write(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h, size_t row, int bgra) {
    size_t line = (size_t)w * 3 + 1, raw_n = line * h; uint8_t *raw = malloc(raw_n); if (!raw) return 0;
    for (uint32_t y = 0; y < h; y++) {
#ifndef DM_TEST_VK_SHOT_FLIPPED   // negative control of t_vk_be only: the rows bottom first
        const uint8_t *s = rgba + (size_t)y * row;
#else
        const uint8_t *s = rgba + (size_t)(h - 1 - y) * row;
#endif
        uint8_t *d = raw + (size_t)y * line; d[0] = 0;   // (filter: none)
        for (uint32_t x = 0; x < w; x++) { d[1 + 3 * x] = s[4 * x + (bgra ? 2 : 0)]; d[2 + 3 * x] = s[4 * x + 1]; d[3 + 3 * x] = s[4 * x + (bgra ? 0 : 2)]; }
    }
    uLongf zn = compressBound(raw_n); uint8_t *z = malloc(zn); int ok = z && compress2(z, &zn, raw, raw_n, 6) == Z_OK;
    FILE *f = ok ? fopen(path, "wb") : NULL; ok = f != NULL;
    if (ok) {
        static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'}; uint8_t ih[13]; put32(ih, w); put32(ih + 4, h); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
        ok = fwrite(sig, 1, 8, f) == 8 && chunk(f, "IHDR", ih, 13) && chunk(f, "IDAT", z, (uint32_t)zn) && chunk(f, "IEND", NULL, 0);
        ok = fclose(f) == 0 && ok;
    }
    free(z); free(raw); return ok;
}
static void shot(const tex_t *t, const int crop[4], const char *path) {
    int f = t->d.format, bgra = f == NVN_FORMAT_BGRA8 || f == NVN_FORMAT_BGRA8_SRGB || f == NVN_FORMAT_BGRX8 || f == NVN_FORMAT_BGRX8_SRGB;
    int bw, bh; if (nvn_fmt_bpb(f, &bw, &bh) != 4 || bw != 1) { VLOG("[red] a screenshot of a window texture of format %#x: only 4-byte colours are written", f); return; }
    uint32_t cx = crop ? (uint32_t)crop[0] : 0, cy = crop ? (uint32_t)crop[1] : 0;
    uint32_t cw = crop && crop[2] > 0 ? (uint32_t)crop[2] : t->d.width, ch = crop && crop[3] > 0 ? (uint32_t)crop[3] : t->d.height;
    uint8_t *px = malloc((size_t)cw * ch * 4); if (!px) return;
    NVNcopyRegion r = {(int)cx, (int)cy, 0, (int)cw, (int)ch, 1}; mtl_read_texels(t, 0, 0, &r, px, (size_t)cw * 4, 0);
    if (png_write(path, px, cw, ch, (size_t)cw * 4, bgra)) VLOG("screenshot of frame %llu: %s (%ux%u)", (unsigned long long)g_presented, path, cw, ch);
    else VLOG("[red] screenshot of frame %llu: %s cannot be written", (unsigned long long)g_presented, path);
    free(px);
}
void vk_shot_present(const tex_t *t, const int crop[4]) {
    uint64_t n = ++g_presented;
    static long frames[16]; static int nf = -1; static const char *fpath;
    if (nf < 0) {   // DM_NVN_SHOT=frame[,frame...]:path
        nf = 0; const char *e = getenv("DM_NVN_SHOT"), *c = e ? strchr(e, ':') : NULL;
        if (c) { fpath = c + 1; for (const char *q = e; q < c && nf < 16; q = strchr(q, ',') && strchr(q, ',') < c ? strchr(q, ',') + 1 : c) frames[nf++] = atol(q); }
    }
    for (int i = 0; i < nf; i++) if ((long)n == frames[i]) { char p[1024]; snprintf(p, sizeof p, fpath, (int)n); shot(t, crop, p); }
    for (;;) {
        char p[1024]; int due = 0;
        pthread_mutex_lock(&g_qmu);
        for (int i = 0; i < g_nq; i++) if (g_q[i].frame <= n) { snprintf(p, sizeof p, "%s", g_q[i].path); g_q[i] = g_q[--g_nq]; due = 1; break; }
        pthread_mutex_unlock(&g_qmu);
        if (!due) break;
        shot(t, crop, p);
    }
}
