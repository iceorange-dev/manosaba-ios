// Memory snapshots for offline comparison (after tomodachi-native's memsnap.c, a71964e, MIT): DM_MEMSNAP=frame[,frame...]:path prefix writes
// <prefix>_<frame>.bin when the game reaches each of those frames (up to 16), with the pages of guest memory actually in use: resident per
// mincore(2), readable, in the guest address space block (heap, alias region, stack region) and in each module's data segment (data_off up to the
// key page). For the manosaba leak: snapshots after the first and the ninth save load, then tools/memsnap.py diff (what grew) and refs (which words
// point into an object). The game keeps running while a snapshot is written (seconds for a few GB), so keep the input still around it; pages the
// system compressed or swapped count as not resident and are left out.
// File: "DMSNAP01", u64 frame, u32 range count, u32 0, the named ranges { u64 lo, u64 hi, char name[16] }, then records up to the end
// { u64 address, u64 length, length bytes }. Addresses are host addresses (guest and host share one address space). Checked by
// host/tests/t_memsnap.c (and the reader by tools/tests/t_memsnap.py)
#include "dm.h"
#include "plat.h"
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
void svc_guest_range(uint8_t **base, size_t *size);
void svc_guest_layout(uint8_t **alias, size_t *alias_n, uint8_t **stack, size_t *stack_n, uint8_t **heap, size_t *heap_n);
#define SNAPN 16
static long g_frames[SNAPN]; static int g_nframes; static char g_prefix[900];

// The resident pages of [a, a + n) (page aligned) as records, one per run
static size_t dump_range(FILE *f, uint8_t *a, size_t n) {
    size_t pg = (size_t)getpagesize(), np = n / pg, written = 0;
    char *vec = np ? malloc(np) : NULL; if (!vec) return 0;
    if (mincore(a, np * pg, (void *)vec)) { free(vec); return 0; }   // (char * on Darwin, unsigned char * on Linux)
    for (size_t i = 0; i < np;) {
        if (!(vec[i] & MINCORE_INCORE)) { i++; continue; }
        size_t j = i; while (j < np && (vec[j] & MINCORE_INCORE)) j++;
        uint64_t addr = (uint64_t)(uintptr_t)(a + i * pg), len = (j - i) * pg;
        fwrite(&addr, 8, 1, f); fwrite(&len, 8, 1, f); fwrite(a + i * pg, 1, len, f); written += len;
        i = j;
    }
    free(vec);
    return written;
}
// Each readable VM region within [lo, hi), clipped to it (page aligned outward)
size_t dm_memsnap_regions(FILE *f, uint8_t *lo, uint8_t *hi) {
    uintptr_t pg = (uintptr_t)getpagesize(), l = (uintptr_t)lo & ~(pg - 1), h = ((uintptr_t)hi + pg - 1) & ~(pg - 1);
    size_t total = 0; uint64_t at = l;
    while (at < h) {
        uint64_t sz = 0; int readable = 0;
        if (!dm_mem_region(&at, &sz, &readable) || at >= h) break;
        uintptr_t a0 = at < l ? l : at, a1 = at + sz > h ? h : at + sz;
        if (readable) total += dump_range(f, (uint8_t *)a0, a1 - a0);
        at += sz;
    }
    return total;
}
// DM_MEMSNAP's value: the frames (ascending) and the prefix; 0 when it has no ':' or no frame
int dm_memsnap_parse(const char *spec, long *frames, int max, char *prefix, size_t prefix_n) {
    const char *c = spec ? strchr(spec, ':') : NULL; int n = 0;
    if (!c || !c[1]) return 0;
    for (const char *q = spec; q < c && n < max; q = strchr(q, ',') && strchr(q, ',') < c ? strchr(q, ',') + 1 : c) { char *end; long v = strtol(q, &end, 10); if (end != q) frames[n++] = v; }
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && frames[j - 1] > frames[j]; j--) { long t = frames[j]; frames[j] = frames[j - 1]; frames[j - 1] = t; }
    snprintf(prefix, prefix_n, "%s", c + 1);
    return n;
}
typedef struct { uint64_t lo, hi; char name[16]; } snap_range;
static int ranges(snap_range *r, int max) {
    int n = 0; uint8_t *as, *al, *st, *hp; size_t as_n, al_n, st_n, hp_n;
    svc_guest_range(&as, &as_n); if (!as) return 0;
    svc_guest_layout(&al, &al_n, &st, &st_n, &hp, &hp_n);
    r[n++] = (snap_range){(uintptr_t)as, (uintptr_t)al, "prefix"};
    r[n++] = (snap_range){(uintptr_t)al, (uintptr_t)(al + al_n), "alias"};
    r[n++] = (snap_range){(uintptr_t)st, (uintptr_t)(st + st_n), "stack"};
    r[n++] = (snap_range){(uintptr_t)hp, (uintptr_t)(hp + hp_n), "heap"};
    for (int i = 0; i < g_nmod && n < max; i++) {
        module_t *m = &g_mod[i]; if (!m->base || m->data_off >= m->key_page_off || m->key_page_off > m->size) continue;
        r[n] = (snap_range){(uintptr_t)(m->base + m->data_off), (uintptr_t)(m->base + m->key_page_off), ""};
        snprintf(r[n].name, sizeof r[n].name, "%s.data", m->name); n++;
    }
    return n;
}
// One snapshot to path; the bytes of memory written, -1 when the file cannot be written
long dm_memsnap_write(const char *path, long frame) {
    FILE *f = fopen(path, "wb"); if (!f) return -1;
    snap_range r[4 + MAX_MOD]; int nr = ranges(r, 4 + MAX_MOD); uint64_t fr = (uint64_t)frame; uint32_t hdr[2] = {(uint32_t)nr, 0};
    fwrite("DMSNAP01", 8, 1, f); fwrite(&fr, 8, 1, f); fwrite(hdr, 4, 2, f); fwrite(r, sizeof *r, (size_t)nr, f);
    size_t n = 0;
    for (int i = 0; i < nr; i++) n += dm_memsnap_regions(f, (uint8_t *)(uintptr_t)r[i].lo, (uint8_t *)(uintptr_t)r[i].hi);
    int bad = ferror(f); if (fclose(f) || bad) return -1;
    return (long)n;
}
static void *snap_main(void *arg) {
    (void)arg;
    for (int k = 0; k < g_nframes;) {
        long fr = (long)dm_nvn_frame_count();
        if (fr < g_frames[k]) { usleep(5000); continue; }
        char path[1024]; snprintf(path, sizeof path, "%s_%ld.bin", g_prefix, g_frames[k]);
        uint64_t t0 = now_ns(); long n = dm_memsnap_write(path, g_frames[k]);
        if (n < 0) LOG("[red] DM_MEMSNAP: cannot write %s", path);
        else LOG("DM_MEMSNAP: frame %ld (asked %ld) written to %s (%.1f MB, %.1f s)", fr, g_frames[k], path, n / 1048576.0, (now_ns() - t0) / 1e9);
        k++;
    }
    return NULL;
}
// After the modules are loaded (host_preload)
void dm_memsnap_init(void) {
    const char *e = getenv("DM_MEMSNAP"); if (!e || !*e) return;
    g_nframes = dm_memsnap_parse(e, g_frames, SNAPN, g_prefix, sizeof g_prefix);
    if (!g_nframes) { LOG("[red] DM_MEMSNAP=%s: expected frame[,frame...]:path prefix", e); return; }
    LOG("DM_MEMSNAP: %d snapshot(s) from frame %ld to %s_<frame>.bin", g_nframes, g_frames[0], g_prefix);
    pthread_t th; if (!pthread_create(&th, NULL, snap_main, NULL)) pthread_detach(th);
}
