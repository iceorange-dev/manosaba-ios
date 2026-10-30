// movie library host side (hle_movie.c) with WebM / VP9 (movie_webm.c, libvpx), called the way Tears of the Kingdom calls it: a VP9 stream is encoded
// here with libvpx (12 uniform frames, key frames 0 and 6), muxed into a WebM file that also has a Void element, a BlockGroup with a ReferenceBlock and
// an unknown-sized Cluster that ends where the next Cluster begins, then read through the Extractor and decoded through the Decoder (NV12 values,
// events, end of stream, output format, pacing and dropping late frames). Expected values come from the encoder input, not from the code under test.
// t_movie <file.webm>: probe a real movie instead (every frame through the same path; decode errors, timing, three frames as PPM in $TMPDIR).
#include "../hle/hle_movie.c"
#include <vpx/vp8cx.h>
#include <vpx/vpx_encoder.h>
#include <time.h>

struct dm_flags g_flag;
uint64_t g_t0_ns;
uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static uint64_t t_clock;
uint64_t guest_ns(void) { return t_clock; }
static char t_dir[512];
int dm_fs_host_file(const char *path, char *out, size_t n) {
    if (strncmp(path, "content:/", 9)) return 0;
    snprintf(out, n, "%s/%s", t_dir, path + 9);
    return access(out, R_OK) == 0;
}
static int t_events[4]; static int t_event_obj[4];   // DecoderEvents: the test's EventType objects are these ints
static void t_signal(void *ev) { for (int i = 0; i < 4; i++) if (ev == &t_event_obj[i]) t_events[i]++; }
static void t_delete(void *p) { free(p); }
void *guest_sym(const char *name) {
    if (!strcmp(name, "_ZdlPv")) return (void *)t_delete;
    if (!strcmp(name, "_ZN2nn2os11SignalEventEPNS0_9EventTypeE")) return (void *)t_signal;
    return NULL;
}

static int g_fail, g_n;
#define EXPECT(name, want, got) do { long long w_ = (long long)(want), g_ = (long long)(got); g_n++; \
    printf("%-60s expected %-10lld got %-10lld %s\n", name, w_, g_, w_ == g_ ? "✓" : "✗"); if (w_ != g_) g_fail++; } while (0)

// ---- WebM writer: element IDs as RFC 9559 gives them, sizes as 8-byte VINTs ----
typedef struct { uint8_t *d; size_t n, cap; } wbuf;
static void put(wbuf *b, const void *p, size_t n) { if (b->n + n > b->cap) { b->cap = (b->n + n) * 2 + 4096; b->d = realloc(b->d, b->cap); } memcpy(b->d + b->n, p, n); b->n += n; }
static void put_id(wbuf *b, uint32_t id) { uint8_t t[4]; int n = id > 0xFFFFFF ? 4 : id > 0xFFFF ? 3 : id > 0xFF ? 2 : 1; for (int i = 0; i < n; i++) t[i] = (uint8_t)(id >> (8 * (n - 1 - i))); put(b, t, (size_t)n); }
static void put_size(wbuf *b, uint64_t v) { uint8_t t[8] = {0x01}; for (int i = 1; i < 8; i++) t[i] = (uint8_t)(v >> (8 * (7 - i))); put(b, t, 8); }
static size_t open_el(wbuf *b, uint32_t id) { put_id(b, id); size_t at = b->n; put_size(b, 0); return at; }
static void close_el(wbuf *b, size_t at) { uint64_t v = b->n - at - 8; for (int i = 1; i < 8; i++) b->d[at + i] = (uint8_t)(v >> (8 * (7 - i))); }
static void put_uint(wbuf *b, uint32_t id, uint64_t v) { put_id(b, id); put_size(b, 8); uint8_t t[8]; for (int i = 0; i < 8; i++) t[i] = (uint8_t)(v >> (8 * (7 - i))); put(b, t, 8); }
static void put_float(wbuf *b, uint32_t id, double f) { uint64_t u; memcpy(&u, &f, 8); put_uint(b, id, u); }
static void put_str(wbuf *b, uint32_t id, const char *s) { put_id(b, id); put_size(b, strlen(s)); put(b, s, strlen(s)); }
static void put_block(wbuf *b, uint32_t id, int16_t rel, uint8_t flags, const uint8_t *f, size_t n) {
    put_id(b, id); put_size(b, 4 + n); uint8_t h[4] = {0x81, (uint8_t)(rel >> 8), (uint8_t)rel, flags}; put(b, h, 4); put(b, f, n);   // track number 1 as a 1-byte VINT
}

enum { W = 72, H = 48, N = 12 };   // a chroma row of 36: the NEON interleave (16 at a time) and its scalar tail both run
static uint8_t *g_pkt[N]; static size_t g_pkt_n[N]; static int g_key[N];
static int64_t pts_ms(int i) { return (i * 1000 + 15) / 30; }   // 0, 33, 67, 100, ... (rounded)
static int ycol(int i) { return 32 + 12 * i; } static int ucol(int i) { return 80 + 6 * i; } static int vcol(int i) { return 176 - 6 * i; }
static int encode(void) {
    vpx_codec_enc_cfg_t cfg; if (vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &cfg, 0)) return 0;
    cfg.g_w = W; cfg.g_h = H; cfg.g_timebase.num = 1; cfg.g_timebase.den = 30; cfg.g_lag_in_frames = 0; cfg.kf_mode = VPX_KF_DISABLED;
    cfg.rc_end_usage = VPX_Q; cfg.rc_min_quantizer = 0; cfg.rc_max_quantizer = 4; cfg.g_threads = 1;
    vpx_codec_ctx_t enc; if (vpx_codec_enc_init(&enc, vpx_codec_vp9_cx(), &cfg, 0)) return 0;
    vpx_codec_control(&enc, VP8E_SET_CPUUSED, 8); vpx_codec_control(&enc, VP8E_SET_CQ_LEVEL, 0);
    vpx_image_t img; vpx_img_alloc(&img, VPX_IMG_FMT_I420, W, H, 16);
    int got = 0;
    for (int i = 0; i <= N; i++) {
        if (i < N) {
            for (int y = 0; y < H; y++) memset(img.planes[0] + y * img.stride[0], ycol(i), W);
            for (int y = 0; y < H / 2; y++) { memset(img.planes[1] + y * img.stride[1], ucol(i), W / 2); memset(img.planes[2] + y * img.stride[2], vcol(i), W / 2); }
        }
        if (vpx_codec_encode(&enc, i < N ? &img : NULL, i, 1, i % 6 == 0 ? VPX_EFLAG_FORCE_KF : 0, VPX_DL_GOOD_QUALITY)) return 0;
        vpx_codec_iter_t it = NULL; const vpx_codec_cx_pkt_t *p;
        while ((p = vpx_codec_get_cx_data(&enc, &it))) if (p->kind == VPX_CODEC_CX_FRAME_PKT && got < N) {
            g_pkt_n[got] = p->data.frame.sz; g_pkt[got] = malloc(p->data.frame.sz); memcpy(g_pkt[got], p->data.frame.buf, p->data.frame.sz);
            g_key[got] = (p->data.frame.flags & VPX_FRAME_IS_KEY) != 0; got++;
        }
    }
    vpx_img_free(&img); vpx_codec_destroy(&enc);
    return got == N;
}
static void write_webm(const char *file) {
    wbuf b = {0};
    size_t e = open_el(&b, 0x1A45DFA3); put_str(&b, 0x4282, "webm"); put_uint(&b, 0x4285, 2); close_el(&b, e);
    size_t seg = open_el(&b, 0x18538067);
    put_id(&b, 0xEC); put_size(&b, 5); put(&b, "\0\0\0\0\0", 5);                                         // Void
    size_t info = open_el(&b, 0x1549A966); put_uint(&b, 0x2AD7B1, 1000000); put_float(&b, 0x4489, N * 1000.0 / 30); close_el(&b, info);
    size_t trs = open_el(&b, 0x1654AE6B), te = open_el(&b, 0xAE);
    put_uint(&b, 0xD7, 1); put_uint(&b, 0x83, 1); put_str(&b, 0x86, "V_VP9"); put_uint(&b, 0x23E383, 33333333);
    size_t vid = open_el(&b, 0xE0); put_uint(&b, 0xB0, W); put_uint(&b, 0xBA, H); close_el(&b, vid);
    close_el(&b, te); close_el(&b, trs);
    size_t c1 = open_el(&b, 0x1F43B675); put_uint(&b, 0xE7, 0);
    for (int i = 0; i < 6; i++) {
        if (i == 4) {   // BlockGroup: a Block (no key flag there) and a ReferenceBlock, which makes it a non-key frame
            size_t bg = open_el(&b, 0xA0); put_block(&b, 0xA1, (int16_t)pts_ms(i), 0, g_pkt[i], g_pkt_n[i]); put_uint(&b, 0xFB, (uint64_t)-33); close_el(&b, bg);
        } else put_block(&b, 0xA3, (int16_t)pts_ms(i), g_key[i] ? 0x80 : 0, g_pkt[i], g_pkt_n[i]);
    }
    close_el(&b, c1);
    put_id(&b, 0x1F43B675); { uint8_t u[8] = {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; put(&b, u, 8); }   // Cluster of unknown size
    put_uint(&b, 0xE7, (uint64_t)pts_ms(6));
    for (int i = 6; i < 10; i++) put_block(&b, 0xA3, (int16_t)(pts_ms(i) - pts_ms(6)), g_key[i] ? 0x80 : 0, g_pkt[i], g_pkt_n[i]);
    size_t c3 = open_el(&b, 0x1F43B675); put_uint(&b, 0xE7, (uint64_t)pts_ms(10));                        // ends the unknown-sized Cluster
    for (int i = 10; i < N; i++) put_block(&b, 0xA3, (int16_t)(pts_ms(i) - pts_ms(10)), g_key[i] ? 0x80 : 0, g_pkt[i], g_pkt_n[i]);
    close_el(&b, c3);
    put_id(&b, 0x1C53BB6B); put_size(&b, 0);                                                             // Cues
    close_el(&b, seg);
    FILE *f = fopen(file, "wb"); if (!f) { perror(file); exit(1); }
    fwrite(b.d, 1, b.n, f); fclose(f); free(b.d);
}

// ---- the game's side: extractor → decoder input, decoder output → checks ----
typedef struct { void *vt; void *impl; } obj;   // movie::Extractor / Decoder: vtable + host state
#define VCALL(o, slot, T, ...) ((T)((void **)(o)->vt)[slot])((void **)(o), ##__VA_ARGS__)
typedef int (*f_ptr)(void **, const char *); typedef int (*f_i)(void **, int32_t *); typedef int (*f_imd)(void **, int, void **); typedef int (*f_int)(void **, int);
typedef int (*f_64)(void **, int64_t); typedef int (*f_buf)(void **, mvbuf *); typedef int (*f_p64)(void **, int64_t *); typedef int (*f_md)(void **, void **);
typedef int (*f_v)(void **); typedef int (*f_ii)(void **, int32_t *, int32_t *); typedef int (*f_ib)(void **, int32_t, mvbuf *);
typedef int (*f_send)(void **, int32_t, int32_t, int32_t, int64_t, uint32_t); typedef int (*f_acq)(void **, int32_t *, int64_t *, uint32_t *, int32_t *);
typedef int (*f_rel)(void **, int32_t);
enum { E_SETPATH = 2, E_COUNT = 4, E_CONFIG = 5, E_SELECT = 6, E_SEEK = 8, E_READ = 10, E_ADVANCE = 11, E_TIME = 12, E_META = 13 };
enum { D_CONFIGURE = 2, D_EVENTS = 3, D_OUTFMT = 4, D_START = 5, D_FLUSH = 8, D_ACQIN = 9, D_GETIN = 10, D_SEND = 11, D_ACQOUT = 12, D_GETOUT = 13, D_OUTSIZE = 14, D_RELEASE = 15 };

typedef struct { int n, eos, order_ok, values_ok, format_first, extra; int64_t pts[64]; } outlog;   // extra: a second output taken in one game frame
static int64_t g_maxdiff;
static void check_frame(const uint8_t *f, int64_t pts, outlog *L) {
    int i = -1; for (int k = 0; k < N; k++) if (pts == pts_ms(k) * 1000) i = k;
    if (i < 0) { L->values_ok = 0; return; }
    for (int p = 0; p < W * H + W * H / 2; p++) {
        int want = p < W * H ? ycol(i) : ((p - W * H) & 1) ? vcol(i) : ucol(i), d = abs(f[p] - want);
        if (d > g_maxdiff) g_maxdiff = d;
        if (d > 2) L->values_ok = 0;
    }
}
// Feeds every sample of the selected track into the decoder (end of stream after the last) and takes outputs; tick_ns > 0: one dm_movie_tick per
// round with the guest clock advanced by tick_ns, outputs taken after each tick (one game frame)
static void run(obj *ex, obj *dec, outlog *L, uint64_t tick_ns, int rounds) {
    static uint8_t frame[W * H * 2]; int fed_eos = 0;
    L->order_ok = L->values_ok = 1;
    for (int r = 0; r < rounds && !L->eos; r++) {
        for (;;) {   // inputs while the decoder has buffers
            if (fed_eos) break;
            int32_t idx, rem; if (VCALL(dec, D_ACQIN, f_ii, &idx, &rem) != ST_OK) break;
            mvbuf in; VCALL(dec, D_GETIN, f_ib, idx, &in);
            int64_t t = 0; void *meta = NULL; int32_t sync = 0;
            if (VCALL(ex, E_READ, f_buf, &in) == ST_EOS) { VCALL(dec, D_SEND, f_send, idx, 0, 0, 0, BF_EOS); fed_eos = 1; break; }
            VCALL(ex, E_TIME, f_p64, &t); VCALL(ex, E_META, f_md, &meta); mv_FindInt32(&meta, "is-sync-frame", &sync); mv_MediaDataD(&meta);
            VCALL(dec, D_SEND, f_send, idx, in.off, in.size, t, sync ? BF_SYNC : 0);
            VCALL(ex, E_ADVANCE, f_v);
        }
        if (tick_ns) { t_clock += tick_ns; dm_movie_tick(); }
        for (int k = 0; k < (tick_ns ? 2 : 64); k++) {
            int32_t o, rem; int64_t pts; uint32_t fl;
            if (VCALL(dec, D_ACQOUT, f_acq, &o, &pts, &fl, &rem) != ST_OK) break;
            if (tick_ns && k) L->extra++;
            mvbuf ob = {frame, sizeof frame, 0, 0};
            VCALL(dec, D_GETOUT, f_ib, o, &ob);
            if (fl & BF_EOS) L->eos = 1;
            else { if (!t_events[EV_FORMAT]) L->format_first = 0; if (L->n && pts <= L->pts[L->n - 1]) L->order_ok = 0; if (L->n < 64) L->pts[L->n] = pts; L->n++; check_frame(frame, pts, L); }
            VCALL(dec, D_RELEASE, f_rel, o);
        }
    }
}
static obj *new_decoder(void *md) {
    obj *d = calloc(1, sizeof *d); mv_Decoder((void **)d, DEC_VP9, 4, 1, 16);
    static void *ev[4];   // the game's DecoderEvents: registered empty, filled in after (blade2-mobile's game; plan item 64k)
    memset(ev, 0, sizeof ev); VCALL(d, D_EVENTS, f_md, ev);
    for (int i = 0; i < 4; i++) ev[i] = &t_event_obj[i];
    if (VCALL(d, D_CONFIGURE, f_md, &md) != ST_OK) return NULL;
    return d;
}

static int probe(const char *file) {   // a real movie: every frame through the extractor and decoder, no pacing
    char path[600]; const char *slash = strrchr(file, '/');
    snprintf(t_dir, sizeof t_dir, "%.*s", slash ? (int)(slash - file) : 1, slash ? file : ".");
    snprintf(path, sizeof path, "content:/%s", slash ? slash + 1 : file);
    obj ex = {0}; mv_Extractor((void **)&ex, 3);
    uint64_t t0 = now_ns();
    int st = VCALL(&ex, E_SETPATH, f_ptr, path); printf("SetDataSource %s → %#x (%.1f ms)\n", path, st, (now_ns() - t0) / 1e6);
    if (st) return 1;
    void *md = NULL; VCALL(&ex, E_CONFIG, f_imd, 0, &md); VCALL(&ex, E_SELECT, f_int, 0);
    const char *mime = ""; int32_t w = 0, h = 0, mis = 0; int64_t dur = 0;
    mv_FindString(&md, "mime", &mime); mv_FindInt32(&md, "width", &w); mv_FindInt32(&md, "height", &h); mv_FindInt32(&md, "max-input-size", &mis); mv_FindInt64(&md, "durationUs", &dur);
    printf("track 0: %s %dx%d, max input %d bytes, %.3f s\n", mime, w, h, mis, dur / 1e6);
    g_vx_pace = 0;
    obj *dec = new_decoder(md); if (!dec) { printf("Configure failed\n"); return 1; }
    VCALL(dec, D_START, f_v);
    size_t fb = (size_t)w * h * 3 / 2; uint8_t *frame = malloc(fb); int n = 0, eos = 0, fed_eos = 0; int64_t last = -1, gaps = 0; uint64_t td = now_ns();
    const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    while (!eos) {
        for (;;) {
            if (fed_eos) break;
            int32_t idx, rem; if (VCALL(dec, D_ACQIN, f_ii, &idx, &rem) != ST_OK) break;
            mvbuf in; VCALL(dec, D_GETIN, f_ib, idx, &in); int64_t t = 0;
            if (VCALL(&ex, E_READ, f_buf, &in) == ST_EOS) { VCALL(dec, D_SEND, f_send, idx, 0, 0, 0, BF_EOS); fed_eos = 1; break; }
            VCALL(&ex, E_TIME, f_p64, &t); VCALL(dec, D_SEND, f_send, idx, in.off, in.size, t, 0); VCALL(&ex, E_ADVANCE, f_v);
        }
        int32_t o, rem; int64_t pts; uint32_t fl;
        if (VCALL(dec, D_ACQOUT, f_acq, &o, &pts, &fl, &rem) != ST_OK) { if (fed_eos) { printf("no end of stream\n"); break; } continue; }
        mvbuf ob = {frame, fb, 0, 0}; VCALL(dec, D_GETOUT, f_ib, o, &ob);
        if (fl & BF_EOS) eos = 1;
        else {
            if (last >= 0 && pts - last > 40000) gaps++;
            last = pts;
            if (n == 0 || n == 3000 || n == 6000) {   // NV12 → RGB (BT.709, video range) as PPM
                char out[700]; snprintf(out, sizeof out, "%s/movie_%05d.ppm", tmp, n); FILE *f = fopen(out, "wb"); fprintf(f, "P6\n%d %d\n255\n", w, h);
                for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                    double Y = 1.164 * (frame[y * w + x] - 16), U = frame[w * h + (y / 2) * w + (x & ~1)] - 128, V = frame[w * h + (y / 2) * w + (x & ~1) + 1] - 128;
                    double rgb[3] = {Y + 1.793 * V, Y - 0.213 * U - 0.533 * V, Y + 2.112 * U};
                    for (int c = 0; c < 3; c++) fputc(rgb[c] < 0 ? 0 : rgb[c] > 255 ? 255 : (int)rgb[c], f);
                }
                fclose(f); printf("wrote %s (frame %d, %.3f s)\n", out, n, pts / 1e6);
            }
            n++;
        }
        VCALL(dec, D_RELEASE, f_rel, o);
    }
    vx_t *v = VX((void **)dec);
    printf("%d frames, %llu decode errors, %lld gaps over 40 ms, decode %.2f ms per frame, %.1f s wall\n", n, (unsigned long long)v->errors, (long long)gaps,
           v->frames ? v->decode_ns / 1e6 / (double)v->frames : 0.0, (now_ns() - td) / 1e9);
    return v->errors || !eos;
}

static void t_clean(void) {   // (the files main writes, and their directory)
    char f[700]; snprintf(f, sizeof f, "%s/t.webm", t_dir); unlink(f); snprintf(f, sizeof f, "%s/junk.webm", t_dir); unlink(f); rmdir(t_dir);
}
int main(int argc, char **argv) {
    if (argc > 1) return probe(argv[1]);
    const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    snprintf(t_dir, sizeof t_dir, "%s/t_movie.%d.d", tmp, (int)getpid()); mkdir(t_dir, 0755); atexit(t_clean);   // one per process: its variants (t_movie_evcopy) run beside it
    char file[700]; snprintf(file, sizeof file, "%s/t.webm", t_dir);
    EXPECT("libvpx encodes 12 VP9 frames, key frames 0 and 6", 1, encode() && g_key[0] && g_key[6] && !g_key[1] && !g_key[4]);
    write_webm(file);
    { char junk[700]; snprintf(junk, sizeof junk, "%s/junk.webm", t_dir); FILE *f = fopen(junk, "wb"); if (!f) { perror(junk); return 1; } fputs("not an EBML file at all", f); fclose(f); }

    // Extractor
    obj ex = {0}; mv_Extractor((void **)&ex, 3);
    obj bad = {0}; mv_Extractor((void **)&bad, 3);
    EXPECT("SetDataSource(path) on a file that is not EBML: UnsupportedMediaType", ST_UNSUPPORTED_MEDIA, VCALL(&bad, E_SETPATH, f_ptr, "content:/junk.webm"));
    EXPECT("SetDataSource(path) on a missing file: UnsupportedMediaType", ST_UNSUPPORTED_MEDIA, VCALL(&bad, E_SETPATH, f_ptr, "content:/none.webm"));
    EXPECT("SetDataSource(\"content:/t.webm\")", ST_OK, VCALL(&ex, E_SETPATH, f_ptr, "content:/t.webm"));
    int32_t nt = 0; VCALL(&ex, E_COUNT, f_i, &nt); EXPECT("one track", 1, nt);
    void *md = NULL; VCALL(&ex, E_CONFIG, f_imd, 0, &md);
    const char *mime = ""; int32_t w = 0, h = 0, mis = 0; int64_t dur = 0; size_t maxpkt = 0;
    for (int i = 0; i < N; i++) if (g_pkt_n[i] > maxpkt) maxpkt = g_pkt_n[i];
    mv_FindString(&md, "mime", &mime); mv_FindInt32(&md, "width", &w); mv_FindInt32(&md, "height", &h); mv_FindInt32(&md, "max-input-size", &mis); mv_FindInt64(&md, "durationUs", &dur);
    EXPECT("track mime is video/x-vnd.on2.vp9", 0, strcmp(mime, "video/x-vnd.on2.vp9"));
    EXPECT("track width, height", W * 10000 + H, w * 10000 + h);
    EXPECT("max-input-size = largest packet", (long long)maxpkt, mis);
    EXPECT("durationUs from Info Duration", 400000, dur);
    VCALL(&ex, E_SELECT, f_int, 0);
    int bytes_ok = 1, time_ok = 1, sync_ok = 1, k = 0;
    for (;; k++) {
        static uint8_t sb[1 << 16]; mvbuf b = {sb, sizeof sb, 0, 0}; int64_t t = -1; void *meta = NULL; int32_t sync = -1;
        if (VCALL(&ex, E_READ, f_buf, &b) != ST_OK) break;
        VCALL(&ex, E_TIME, f_p64, &t); VCALL(&ex, E_META, f_md, &meta); mv_FindInt32(&meta, "is-sync-frame", &sync); mv_MediaDataD(&meta);
        if (k >= N || (size_t)b.size != g_pkt_n[k] || memcmp(sb, g_pkt[k], g_pkt_n[k])) bytes_ok = 0;
        if (k < N && t != pts_ms(k) * 1000) time_ok = 0;
        if (k < N && sync != (k == 0 || k == 6)) sync_ok = 0;   // frame 4 sits in a BlockGroup with a ReferenceBlock
        VCALL(&ex, E_ADVANCE, f_v);
    }
    EXPECT("Read: 12 samples through three Clusters (the second of unknown size)", N, k);
    EXPECT("Read: each sample is the encoder's packet byte for byte", 1, bytes_ok);
    EXPECT("GetSampleTime: Cluster Timestamp + block timestamp, in us", 1, time_ok);
    EXPECT("is-sync-frame: SimpleBlock key flag; BlockGroup with ReferenceBlock is not", 1, sync_ok);
    int64_t t = -1; VCALL(&ex, E_SEEK, f_64, (int64_t)250000); VCALL(&ex, E_TIME, f_p64, &t);
    EXPECT("SeekTo 250 ms starts at the key frame before it (frame 6, 200 ms)", pts_ms(6) * 1000, t);

    // Decoder, no pacing
    obj odd = {0}; mv_Decoder((void **)&odd, DEC_VP9, 4, 1, 16);
    void *avc = NULL; mv_MediaData(&avc); md_str(md_get(&avc), "mime", "video/avc"); md_i32(md_get(&avc), "width", W); md_i32(md_get(&avc), "height", H);
    EXPECT("VP9 decoder configured with video/avc: UnsupportedMediaType", ST_UNSUPPORTED_MEDIA, VCALL(&odd, D_CONFIGURE, f_md, &avc));
    md_str(md_get(&avc), "mime", "video/x-vnd.on2.vp9"); md_i32(md_get(&avc), "width", W + 1);
    EXPECT("VP9 decoder with an odd width: InvalidDimensions", ST_INVALID_DIMENSIONS, VCALL(&odd, D_CONFIGURE, f_md, &avc));
    g_vx_pace = 0; memset(t_events, 0, sizeof t_events);
    obj *dec = new_decoder(md);
    EXPECT("Configure with the extractor's track configuration", 1, dec != NULL);
    if (!dec) { printf("== %d checks, %d failed ==\n", g_n, g_fail + 1); return 1; }
    int32_t osz = 0, oal = 0; VCALL(dec, D_OUTSIZE, f_ii, &osz, &oal);
    EXPECT("GetOutputBufferSize: one NV12 frame, alignment 256", W * H * 3 / 2 * 1000 + 256, osz * 1000 + oal);
    EXPECT("Start signals input available", 1, (VCALL(dec, D_START, f_v), t_events[EV_INPUT] > 0));
    VCALL(&ex, E_SEEK, f_64, (int64_t)0);
    outlog L = {.format_first = 1}; run(&ex, dec, &L, 0, 200);
    EXPECT("12 frames decoded, then the end-of-stream output", N * 10 + 1, L.n * 10 + L.eos);
    EXPECT("output presentation times in order", 1, L.order_ok);
    EXPECT("NV12 values within 2 of the encoder input (Y, then Cb Cr interleaved)", 1, L.values_ok);
    EXPECT("format changed once, before the first output", 11, t_events[EV_FORMAT] * 10 + L.format_first);
    {   // after the end of stream the game sends every input buffer it can take back with the end-of-stream flag (main+0x2b45338); they stay in
        // the decoder (ACodec postFillThisBuffer, subsdk0 0x2bd214), so this ends and no input event is signalled; Flush gives all of them back
        int ev = t_events[EV_INPUT], sent = 0; int32_t idx, rem;
        while (sent < 16 && VCALL(dec, D_ACQIN, f_ii, &idx, &rem) == ST_OK) { VCALL(dec, D_SEND, f_send, idx, 0, 0, 0, BF_EOS); sent++; }
        EXPECT("after the end of stream, inputs sent back with the end-of-stream flag stay in the decoder: no input event, none to take", 0,
               (sent >= 16) * 10 + (t_events[EV_INPUT] != ev));
        int32_t o, orem; int64_t opts; uint32_t ofl;
        EXPECT("... and no second end-of-stream output", ST_OUT_NOT_AVAILABLE, VCALL(dec, D_ACQOUT, f_acq, &o, &opts, &ofl, &orem));
        int back = 0; VCALL(dec, D_FLUSH, f_v);
        while (back < 16 && VCALL(dec, D_ACQIN, f_ii, &idx, &rem) == ST_OK) back++;
        EXPECT("Flush gives every input buffer back and signals input available", 41, back * 10 + (t_events[EV_INPUT] > ev));
    }
    void *of = NULL; VCALL(dec, D_OUTFMT, f_md, &of);
    const char *om = ""; int32_t uvo = 0, cl = -1, ct = -1, cr = -1, cb = -1; mv_FindString(&of, "mime", &om); mv_FindInt32(&of, "nv12-uv-offset", &uvo); mv_FindRect(&of, "crop", &cl, &ct, &cr, &cb);
    EXPECT("output format: video/raw, nv12-uv-offset = w*h, crop inclusive", 1, !strcmp(om, "video/raw") && uvo == W * H && cl == 0 && ct == 0 && cr == W - 1 && cb == H - 1);
    printf("(largest NV12 difference from the encoder input: %lld)\n", (long long)g_maxdiff);

    // Decoder, paced: one game frame = one dm_movie_tick
    g_vx_pace = 1; memset(t_events, 0, sizeof t_events); t_clock = 1000000000ull;
    obj *pd = new_decoder(md); VCALL(pd, D_START, f_v); VCALL(&ex, E_SEEK, f_64, (int64_t)0);
    outlog P = {.format_first = 1}; run(&ex, pd, &P, 33333333ull, 40);
    int on_time = 1; for (int i = 0; i < P.n && i < N; i++) if (P.pts[i] != pts_ms(i) * 1000) on_time = 0;
    EXPECT("paced at 30 game frames per second: every frame, in order", N * 10 + 1, P.n * 10 + on_time);
    EXPECT("... at most one frame handed over per game frame", 0, P.extra);
    obj *sd = new_decoder(md); VCALL(sd, D_START, f_v); VCALL(&ex, E_SEEK, f_64, (int64_t)0); t_clock = 5000000000ull;
    outlog S = {.format_first = 1}; run(&ex, sd, &S, 100000000ull, 40);
    int keeps = 1; for (int i = 1; i < S.n; i++) if (S.pts[i] < (int64_t)i * 100000 - 50000) keeps = 0;   // shown frame i at about i × 100 ms
    EXPECT("at 10 game frames per second late frames are dropped: fewer frames", 1, S.n < N && S.n >= 4);
    EXPECT("... and the picture keeps time (frame shown at tick i is from about i x 100 ms)", 1, keeps);
    printf("(10 fps: %d of %d frames shown, %llu dropped)\n", S.n, N, (unsigned long long)VX((void **)sd)->dropped);
    printf("== %d checks, %d failed ==\n", g_n, g_fail);
    return g_fail != 0;
}
