// The movie library's host backend on Android (host/movie_av.h; plan item 301d): AVFoundation's part (media/movie_av.m) done with the NDK's media
// API. WebM / Matroska files go to movie_webm.c as on Apple (compressed reads only, from the mapped file); anything else to AMediaExtractor (MP4 /
// MOV, H.264 and AAC as Unity's VideoPlayer gives them). A reader has its own extractor on one track:
//   compressed: the samples as stored, in decode order (the extractor gives presentation times only, so dts_us = pts_us)
//   decoded: AMediaCodec on the track's format, its output in buffers (no surface): video asked for as YUV420SemiPlanar (21, NV12) and taken as the
//   decoder reports it (its colour format, stride and slice height: 21, or 19 = planar I420) to NV12 at the track's size (Y, then Cb Cr interleaved,
//   stride = width); audio as the decoder's PCM16, interleaved. Through an AImageReader instead, this phone's H.264 decoder chose its own compressed
//   layout (0x7fa30c06), whose chroma planes the CPU cannot read. Decoding starts at the key frame at or before start_us and what comes before
//   start_us is dropped, as AVAssetReader's time range does.
#include <fcntl.h>
#include <math.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include "dm.h"
#include "movie_av.h"
#include "movie_webm.h"

struct mav_media {
    _Atomic int refs; int n;
    mav_track info[8]; int xtrack[8];   // extractor: the extractor's track of each entry
    char file[1024];
    webm *wm; int wtrack[8];            // WebM: the demuxer and its track of each entry
};
struct mav_reader {
    mav_media *m; int kind, compressed, track; int32_t w, h, ch;
    int fd; AMediaExtractor *x; AMediaCodec *c; int64_t from_us; int in_eos;
    int32_t cf, stride, slice;          // decoded video: the decoder's output layout (AMediaCodec_getOutputFormat)
    uint8_t *buf; size_t cap;
    const webm_frame *frames; int wnext, wcount;
};

enum { COLOR_YUV420_PLANAR = 19, COLOR_YUV420_SEMIPLANAR = 21 };   // MediaCodecInfo.CodecCapabilities COLOR_FormatYUV420Planar / SemiPlanar
static int is_ebml(const char *file) {
    FILE *f = fopen(file, "rb"); uint8_t b[4] = {0}; size_t n = f ? fread(b, 1, sizeof b, f) : 0;
    if (f) fclose(f);
    return n == 4 && b[0] == 0x1A && b[1] == 0x45 && b[2] == 0xDF && b[3] == 0xA3;
}
static const char *webm_mime(const webm_track *t) {
    if (t->type == WEBM_VIDEO) return !strcmp(t->codec, "V_VP9") ? "video/x-vnd.on2.vp9" : !strcmp(t->codec, "V_VP8") ? "video/x-vnd.on2.vp8" : "video/unknown";
    return !strcmp(t->codec, "A_VORBIS") ? "audio/vorbis" : "audio/unknown";
}
static mav_media *open_webm(const char *file) {   // as movie_av.m's
    char why[200]; webm *w = webm_open(file, why, sizeof why);
    if (!w) { LOG("[movie] [red] %s: %s", file, why); return NULL; }
    mav_media *m = calloc(1, sizeof *m); m->wm = w;
    for (int i = 0; i < webm_track_count(w) && m->n < 8; i++) {
        const webm_track *t = webm_track_at(w, i); const webm_frame *f = webm_frames(w, i);
        if ((t->type != WEBM_VIDEO && t->type != WEBM_AUDIO) || !t->nframes) continue;
        if (t->laced) LOG("[movie] [red] %s track %d: %d laced blocks left out (lacing is not split)", file, t->number, t->laced);
        mav_track *ti = &m->info[m->n];
        ti->kind = t->type == WEBM_VIDEO ? MAV_VIDEO : MAV_AUDIO; ti->mime = webm_mime(t);
        int64_t span = f[t->nframes - 1].pts_us - f[0].pts_us, bytes = 0;
        for (int k = 0; k < t->nframes; k++) bytes += f[k].size;
        ti->duration_us = webm_duration_us(w) > 0 ? webm_duration_us(w) : span + t->default_ns / 1000;
        ti->bit_rate = ti->duration_us > 0 ? (int32_t)(bytes * 8 * 1000000 / ti->duration_us) : 0;
        ti->max_sample = (int32_t)t->max_size;
        if (t->type == WEBM_VIDEO) {
            ti->width = t->width; ti->height = t->height;
            ti->frame_rate = t->default_ns > 0 ? (int32_t)llround(1e9 / (double)t->default_ns) : span > 0 ? (int32_t)llround((t->nframes - 1) * 1e6 / (double)span) : 0;
        } else { ti->channels = t->channels; ti->sample_rate = (int32_t)llround(t->sample_rate); }
        strlcpy(ti->lang, t->lang, sizeof ti->lang);
        m->wtrack[m->n++] = i;
    }
    if (!m->n) { LOG("[movie] [red] %s: no video or audio frames", file); webm_close(w); free(m); return NULL; }
    atomic_store(&m->refs, 1); return m;
}
static AMediaExtractor *extractor(const char *file, int *fd) {
    *fd = open(file, O_RDONLY | O_CLOEXEC); struct stat st;
    if (*fd < 0 || fstat(*fd, &st)) { if (*fd >= 0) close(*fd); *fd = -1; return NULL; }
    AMediaExtractor *x = AMediaExtractor_new();
    media_status_t s = x ? AMediaExtractor_setDataSourceFd(x, *fd, 0, st.st_size) : AMEDIA_ERROR_UNKNOWN;
    if (s != AMEDIA_OK) { LOG("[movie] [red] %s: the extractor cannot read it (%d)", file, (int)s); if (x) AMediaExtractor_delete(x); close(*fd); *fd = -1; return NULL; }
    return x;
}
mav_media *mav_open(const char *file) {
    if (is_ebml(file)) return open_webm(file);
    int fd; AMediaExtractor *x = extractor(file, &fd); if (!x) return NULL;
    mav_media *m = calloc(1, sizeof *m); snprintf(m->file, sizeof m->file, "%s", file);
    for (size_t i = 0; i < AMediaExtractor_getTrackCount(x) && m->n < 8; i++) {
        AMediaFormat *f = AMediaExtractor_getTrackFormat(x, i); const char *mime = NULL;
        if (!f || !AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &mime) || !mime) { if (f) AMediaFormat_delete(f); continue; }
        int video = !strncmp(mime, "video/", 6), audio = !strncmp(mime, "audio/", 6);
        if (!video && !audio) { AMediaFormat_delete(f); continue; }
        mav_track *ti = &m->info[m->n]; int32_t v; int64_t d; const char *lang = NULL;
        ti->kind = video ? MAV_VIDEO : MAV_AUDIO;
        ti->mime = video ? (!strcmp(mime, "video/avc") ? "video/avc" : "video/unknown") : (!strcmp(mime, "audio/mp4a-latm") ? "audio/mp4a-latm" : "audio/unknown");
        if (AMediaFormat_getInt64(f, AMEDIAFORMAT_KEY_DURATION, &d)) ti->duration_us = d;
        if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_BIT_RATE, &v)) ti->bit_rate = v;
        if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, &v)) ti->max_sample = v;
        if (video) {
            if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_WIDTH, &v)) ti->width = v;
            if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_HEIGHT, &v)) ti->height = v;
            float fr; if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_FRAME_RATE, &v)) ti->frame_rate = v; else if (AMediaFormat_getFloat(f, AMEDIAFORMAT_KEY_FRAME_RATE, &fr)) ti->frame_rate = (int32_t)lroundf(fr);
        } else {
            if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &v)) ti->channels = v;
            if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, &v)) ti->sample_rate = v;
        }
        if (AMediaFormat_getString(f, AMEDIAFORMAT_KEY_LANGUAGE, &lang) && lang && strcmp(lang, "und")) strlcpy(ti->lang, lang, sizeof ti->lang);
        m->xtrack[m->n++] = (int)i; AMediaFormat_delete(f);
    }
    AMediaExtractor_delete(x); close(fd);
    if (!m->n) { free(m); return NULL; }
    atomic_store(&m->refs, 1); return m;
}
void mav_retain(mav_media *m) { atomic_fetch_add(&m->refs, 1); }
void mav_release(mav_media *m) { if (m && atomic_fetch_sub(&m->refs, 1) == 1) { webm_close(m->wm); free(m); } }
int mav_track_count(const mav_media *m) { return m->n; }
const mav_track *mav_track_at(const mav_media *m, int i) { return i >= 0 && i < m->n ? &m->info[i] : NULL; }

void mav_reader_free(mav_reader *r) {
    if (!r) return;
    if (r->c) { AMediaCodec_stop(r->c); AMediaCodec_delete(r->c); }
    if (r->x) AMediaExtractor_delete(r->x);
    if (r->fd >= 0) close(r->fd);
    if (r->m) mav_release(r->m);
    free(r->buf); free(r);
}
mav_reader *mav_reader_new(mav_media *m, int track, int64_t start_us, int compressed) {
    if (track < 0 || track >= m->n) return NULL;
    const mav_track *ti = &m->info[track];
    mav_reader *r = calloc(1, sizeof *r); r->fd = -1; r->kind = ti->kind; r->compressed = compressed; r->track = track;
    r->w = ti->width; r->h = ti->height; r->ch = ti->channels; r->from_us = start_us > 0 ? start_us : 0;
    mav_retain(m); r->m = m;
    if (m->wm) {   // WebM: compressed only (its VP8 / VP9 are decoded by hle_movie.c's libvpx decoder from what the game sends)
        if (!compressed) { mav_reader_free(r); return NULL; }
        r->frames = webm_frames(m->wm, m->wtrack[track]); r->wcount = webm_track_at(m->wm, m->wtrack[track])->nframes;
        r->wnext = start_us > 0 ? webm_seek_frame(m->wm, m->wtrack[track], start_us) : 0;
        return r;
    }
    if (!(r->x = extractor(m->file, &r->fd)) || AMediaExtractor_selectTrack(r->x, (size_t)m->xtrack[track]) != AMEDIA_OK) { mav_reader_free(r); return NULL; }
    if (start_us > 0) AMediaExtractor_seekTo(r->x, start_us, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
    if (compressed) return r;
    AMediaFormat *f = AMediaExtractor_getTrackFormat(r->x, (size_t)m->xtrack[track]); const char *fm = NULL; char mime[64] = "";
    if (f && AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &fm) && fm) snprintf(mime, sizeof mime, "%s", fm);   // (the format owns its string)
    if (!*mime || !(r->c = AMediaCodec_createDecoderByType(mime))) { LOG("[movie] [red] %s: no decoder for %s", m->file, *mime ? mime : "?"); if (f) AMediaFormat_delete(f); mav_reader_free(r); return NULL; }
    if (r->kind == MAV_VIDEO) { AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_COLOR_FORMAT, COLOR_YUV420_SEMIPLANAR); r->cf = COLOR_YUV420_SEMIPLANAR; r->stride = r->w; r->slice = r->h; }
    media_status_t s = AMediaCodec_configure(r->c, f, NULL, NULL, 0); AMediaFormat_delete(f);
    if (s != AMEDIA_OK || AMediaCodec_start(r->c) != AMEDIA_OK) { LOG("[movie] [red] %s: the %s decoder did not start (%d)", m->file, mime, (int)s); mav_reader_free(r); return NULL; }
    return r;
}
static uint8_t *room(mav_reader *r, size_t n) {
    if (n > r->cap) { free(r->buf); r->cap = n; r->buf = malloc(n); }
    return r->buf;
}
static void feed(mav_reader *r) {   // the decoder's free input buffers filled with the next samples, end of stream after the last
    for (int k = 0; k < 4 && !r->in_eos; k++) {
        ssize_t i = AMediaCodec_dequeueInputBuffer(r->c, 0); if (i < 0) return;
        size_t cap; uint8_t *p = AMediaCodec_getInputBuffer(r->c, (size_t)i, &cap);
        ssize_t n = p ? AMediaExtractor_readSampleData(r->x, p, cap) : -1;
        if (n < 0) { AMediaCodec_queueInputBuffer(r->c, (size_t)i, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM); r->in_eos = 1; return; }
        AMediaCodec_queueInputBuffer(r->c, (size_t)i, 0, (size_t)n, (uint64_t)AMediaExtractor_getSampleTime(r->x), 0);
        AMediaExtractor_advance(r->x);
    }
}
// A decoded frame (the decoder's layout: r->cf, r->stride, r->slice) → NV12 at the track's size: Y rows, then each chroma row's Cb Cr pairs
static int copy_nv12(mav_reader *r, const uint8_t *p, size_t n, mav_sample *out) {
    size_t W = (size_t)r->w, H = (size_t)r->h, CH = (H + 1) / 2, CW = (W + 1) / 2, st = (size_t)r->stride, sl = (size_t)r->slice;
    if (st < W || sl < H) { LOG("[movie] [red] %s: stride %zu / slice height %zu below the frame's %zu x %zu", r->m->file, st, sl, W, H); return -1; }
    if (r->cf != COLOR_YUV420_PLANAR && r->cf != COLOR_YUV420_SEMIPLANAR) { LOG("[movie] [red] %s: the decoder's colour format %#x is not read here", r->m->file, (unsigned)r->cf); return -1; }
    int planar = r->cf == COLOR_YUV420_PLANAR; size_t cst = planar ? st / 2 : st, csl = (sl + 1) / 2;   // a chroma plane's stride and height
    size_t need = st * sl + (planar ? cst * csl : 0) + cst * (CH - 1) + (planar ? CW : W);              // (its last row unpadded)
    if (n < need) { LOG("[movie] [red] %s: a decoded frame of %zu bytes, %zu expected", r->m->file, n, need); return -1; }
    uint8_t *d = room(r, W * (H + CH)); if (!d) return -1;
    for (size_t y = 0; y < H; y++) memcpy(d + y * W, p + y * st, W);
    const uint8_t *c = p + st * sl;
    for (size_t y = 0; y < CH; y++) {
        uint8_t *o = d + W * H + y * W;
        if (!planar) {
#ifndef DM_TEST_MOVIE_UV_SWAPPED   // negative control of t_movie only: Cr before Cb
            memcpy(o, c + y * st, W);
#else
            for (size_t x = 0; x + 1 < W; x += 2) { o[x] = c[y * st + x + 1]; o[x + 1] = c[y * st + x]; }
#endif
        } else {   // I420: a Cb plane then a Cr plane, each at half the stride
            const uint8_t *u = c + y * cst, *v = c + cst * csl + y * cst;
            for (size_t x = 0; x < CW; x++) { o[2 * x] = u[x]; if (2 * x + 1 < W) o[2 * x + 1] = v[x]; }
        }
    }
    out->data = d; out->size = W * (H + CH); return 1;
}
static int decoded_next(mav_reader *r, mav_sample *out) {
    for (int waited = 0; waited < 5000;) {   // up to 5 s for the next unit (a decoder stalled that long is an error)
        feed(r);
        AMediaCodecBufferInfo bi; ssize_t o = AMediaCodec_dequeueOutputBuffer(r->c, &bi, 10000);
        if (o == AMEDIACODEC_INFO_TRY_AGAIN_LATER) { waited += 10; continue; }
        if (o == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat *f = AMediaCodec_getOutputFormat(r->c); int32_t v;
            if (f && r->kind == MAV_AUDIO && AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &v)) r->ch = v;
            if (f && r->kind == MAV_VIDEO) {
                if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_COLOR_FORMAT, &v)) r->cf = v;
                r->stride = AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_STRIDE, &v) && v >= r->w ? v : r->w;
                r->slice = AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_SLICE_HEIGHT, &v) && v >= r->h ? v : r->h;
                LOG("[movie] decoded video: colour format %#x, stride %d, slice height %d for %d x %d", (unsigned)r->cf, r->stride, r->slice, r->w, r->h);
            }
            if (f) AMediaFormat_delete(f);
            continue;
        }
        if (o < 0) continue;   // (output buffers changed)
        int eos = bi.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM, keep = bi.size > 0 && bi.presentationTimeUs >= r->from_us;
        if (r->kind == MAV_AUDIO) {
            size_t cap; uint8_t *p = AMediaCodec_getOutputBuffer(r->c, (size_t)o, &cap); int ok = 0;
            if (keep && p && (uint8_t *)room(r, (size_t)bi.size)) { memcpy(r->buf, p + bi.offset, (size_t)bi.size); out->data = r->buf; out->size = (size_t)bi.size; ok = 1; }
            AMediaCodec_releaseOutputBuffer(r->c, (size_t)o, false);
            if (ok) { out->pts_us = out->dts_us = bi.presentationTimeUs; out->sync = 1; return 1; }
        } else {
            size_t cap; uint8_t *p = AMediaCodec_getOutputBuffer(r->c, (size_t)o, &cap); int ok = 0;
            if (keep && p) { ok = copy_nv12(r, p + bi.offset, (size_t)bi.size, out); if (ok == 1) { out->pts_us = out->dts_us = bi.presentationTimeUs; out->sync = 1; } }
            AMediaCodec_releaseOutputBuffer(r->c, (size_t)o, false);
            if (keep) return ok;
        }
        if (eos) return 0;
    }
    LOG("[movie] [red] %s: the decoder gave nothing for 5 s", r->m->file); return -1;
}
int mav_reader_next(mav_reader *r, mav_sample *out) {
    memset(out, 0, sizeof *out);
    if (r->m->wm) {   // WebM: the next indexed frame, from the mapping
        if (r->wnext >= r->wcount) return 0;
        const webm_frame *f = &r->frames[r->wnext++];
        out->data = webm_frame_data(r->m->wm, f); out->size = f->size; out->pts_us = out->dts_us = f->pts_us; out->sync = f->key;
        return 1;
    }
    if (!r->compressed) return decoded_next(r, out);
    ssize_t n = AMediaExtractor_getSampleSize(r->x); if (n < 0) return 0;
    uint8_t *d = room(r, n ? (size_t)n : 1); if (!d) return -1;
    if (AMediaExtractor_readSampleData(r->x, d, (size_t)n) != n) return -1;
    out->data = d; out->size = (size_t)n; out->pts_us = out->dts_us = AMediaExtractor_getSampleTime(r->x);
    out->sync = (AMediaExtractor_getSampleFlags(r->x) & AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC) != 0;
    AMediaExtractor_advance(r->x);
    return 1;
}
