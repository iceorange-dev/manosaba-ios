// The game data an APK carries (plan item 301h, the user 2026-10-30: manosaba's APK with its data, as the iPhone's IPA with it; Tears of the Kingdom
// will import its data instead). apk.py --data stores the player's RomFS and the console's fonts as assets/romfs/ and assets/sysfont/ with an
// index each (the asset manager lists no subfolders). On its first start (and after an APK with other data) the app copies them out into the
// external files folder, where adb's push-data puts them too: a file already there with its size is kept, so an interrupted copy goes on where it
// stopped and an APK with the same data copies nothing. Each file through <file>.part, then renamed; the copy is the kernel's (sendfile from the
// APK at the asset's offset: the data is stored), the asset manager's reads where the asset gives no descriptor. app_android.c runs it on a thread
// of its own with its progress on screen (Progress.java). Checked on the phone by tests/t_data_idx.c (the index)
#include "data_apk.h"
#include "dm.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int path_ok(const char *p) {
    if (!*p || *p == '/') return 0;
    for (const char *c = p; *c; c++) if ((unsigned char)*c < 0x20 || *c == '\\' || *c == 0x7f) return 0;
#ifdef DM_TEST_DATA_IDX_ANY   // negative control of tests/t_data_idx.c only: any component taken
    return 1;
#endif
    for (const char *s = p;;) {   // no empty, "." or ".." component
        const char *e = strchr(s, '/'); size_t n = e ? (size_t)(e - s) : strlen(s);
        if (!n || (n == 1 && s[0] == '.') || (n == 2 && s[0] == '.' && s[1] == '.')) return 0;
        if (!e) return 1;
        s = e + 1;
    }
}
int dm_data_idx_parse(const char *text, size_t n, dm_data_ent *out, int max) {
    int k = 0; const char *p = text, *end = text + n;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p)), *le = nl ? nl : end;
        if (le > p) {
            const char *tab = memchr(p, '\t', (size_t)(le - p)); if (!tab || tab == p || k >= max) return -1;
            uint64_t sz = 0; for (const char *c = p; c < tab; c++) { if (*c < '0' || *c > '9') return -1; sz = sz * 10 + (uint64_t)(*c - '0'); }
            size_t pl = (size_t)(le - tab - 1); if (pl >= sizeof out[0].path) return -1;
            memcpy(out[k].path, tab + 1, pl); out[k].path[pl] = 0; out[k].size = sz;
            if (!path_ok(out[k].path)) return -1;
            k++;
        }
        p = le + 1;
    }
    return k;
}

#ifdef __ANDROID__
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>
static dm_data_ent *read_idx(AAssetManager *am, const char *name, int *count) {   // NULL: the APK carries no such data (or its index is bad: said)
    char an[96]; snprintf(an, sizeof an, "%s.idx", name);
    AAsset *a = AAssetManager_open(am, an, AASSET_MODE_BUFFER); if (!a) return NULL;
    size_t n = (size_t)AAsset_getLength(a); const char *t = AAsset_getBuffer(a);
    int max = 1; for (size_t i = 0; t && i < n; i++) max += t[i] == '\n';
    dm_data_ent *e = t ? calloc((size_t)max, sizeof *e) : NULL; int k = e ? dm_data_idx_parse(t, n, e, max) : -1;
    AAsset_close(a);
    if (k < 0) { LOG("[red] app: the APK's %s is not an index of plain paths: its data left out", an); free(e); return NULL; }
    *count = k; return e;
}
static int held(const char *dir, const dm_data_ent *e) { char p[1100]; struct stat st; snprintf(p, sizeof p, "%s/%s", dir, e->path); return !stat(p, &st) && S_ISREG(st.st_mode) && (uint64_t)st.st_size == e->size; }
int64_t dm_data_need(AAssetManager *am, const char *name, const char *dir) {
    int n; dm_data_ent *e = read_idx(am, name, &n); if (!e) return -1;
    int64_t need = 0; for (int i = 0; i < n; i++) if (!held(dir, &e[i])) need += (int64_t)e[i].size;
    free(e); return need;
}
static void mkparents(char *p) { for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0700); *s = '/'; } }
int dm_data_out(AAssetManager *am, const char *name, const char *dir, void (*progress)(uint64_t done, uint64_t total, void *ctx), void *ctx) {
    int n; dm_data_ent *e = read_idx(am, name, &n); if (!e) return -1;
    uint64_t total = 0, done = 0; for (int i = 0; i < n; i++) if (!held(dir, &e[i])) total += e[i].size;
    int rc = 0, copied = 0;
    for (int i = 0; i < n && !rc; i++) {
        if (held(dir, &e[i])) continue;
        char src[512], dst[1100], tmp[1110]; snprintf(src, sizeof src, "%s/%s", name, e[i].path); snprintf(dst, sizeof dst, "%s/%s", dir, e[i].path); snprintf(tmp, sizeof tmp, "%s.part", dst);
        mkparents(dst);
        AAsset *a = AAssetManager_open(am, src, AASSET_MODE_STREAMING); int o = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        int ok = a && o >= 0; off64_t start = 0, len = 0; int fd = ok ? AAsset_openFileDescriptor64(a, &start, &len) : -1;
        uint64_t left = e[i].size;
        if (ok && fd >= 0) {   // stored: the kernel copies it from the APK
            off_t at = (off_t)start;
            while (ok && left) { ssize_t k = sendfile(o, fd, &at, left > (1u << 24) ? (1u << 24) : (size_t)left); if (k > 0) { left -= (uint64_t)k; done += (uint64_t)k; if (progress) progress(done, total, ctx); } else ok = 0; }
            close(fd);
        } else if (ok) {
            static char buf[1 << 20]; int r;
            while (ok && (r = AAsset_read(a, buf, sizeof buf)) > 0) { for (int w = 0; ok && w < r;) { ssize_t k = write(o, buf + w, (size_t)(r - w)); if (k > 0) w += (int)k; else ok = 0; }
                                                                    left -= (uint64_t)r; done += (uint64_t)r; if (progress) progress(done, total, ctx); }
            if (r < 0) ok = 0;
        }
        if (o >= 0 && (fsync(o) || close(o))) ok = 0;
        if (a) AAsset_close(a);
        if (!ok || left || rename(tmp, dst)) { LOG("[red] app: copying %s out of the APK failed: %s", src, strerror(errno)); unlink(tmp); rc = -1; }
        else copied++;
    }
    LOG("app: %s: %d of %d files copied out of the APK, %.1f MB", name, copied, n, done / 1e6);
    free(e); return rc;
}
#endif
