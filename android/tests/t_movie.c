// android/movie_android.c, the movie library's backend on Android (plan item 301d), on the phone, with clips ffmpeg made on the Mac (android/Makefile):
// movie.mp4 = 1 s of red (H.264, 320 x 240, 30 fps, a key frame every 10) and a 440 Hz sine at half scale (AAC, 48 kHz stereo); movie.webm = the same red as VP9;
// movie1080.mp4 = 0.2 s of blue at 1920 x 1080 (a decoder may pad its rows past 1080, its chroma then below them: this phone's reports 1080).
// The tracks and their formats; compressed reads (30 samples, the first a key frame, in order); decoded video as NV12 at the track's size with red's
// BT.601 video-range values (Y 81, Cb 90, Cr 240; blue Y 41, Cb 240, Cr 110); decoded audio as PCM16 with the sine in it; a read from 0.5 s starting there; WebM compressed only.
// -DDM_TEST_MOVIE_UV_SWAPPED (Cr before Cb): 2 fail.
#include "movie_av.h"
#include "binder_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (dm_binder_pool_start()) printf("  (no binder thread pool: the decoders will not answer)\n");   // binder_pool.h
    const char *dir = argc > 1 ? argv[1] : "."; char mp4[512], webm[512];
    snprintf(mp4, sizeof mp4, "%s/movie.mp4", dir); snprintf(webm, sizeof webm, "%s/movie.webm", dir);
    mav_media *m = mav_open(mp4);
    CHECK(m && mav_track_count(m) == 2, "movie.mp4: two tracks (%d)", m ? mav_track_count(m) : -1);
    if (!m) { printf("t_movie: %d checks, %d failed\n", n, fail); return 1; }
    int vt = mav_track_at(m, 0)->kind == MAV_VIDEO ? 0 : 1, at = 1 - vt; const mav_track *v = mav_track_at(m, vt), *a = mav_track_at(m, at);
    CHECK(!strcmp(v->mime, "video/avc") && v->width == 320 && v->height == 240 && v->frame_rate == 30 && v->duration_us > 900000 && v->duration_us < 1100000,
          "video: video/avc 320 x 240, 30 fps, about 1 s (%s %d x %d, %d fps, %lld us)", v->mime, v->width, v->height, v->frame_rate, (long long)v->duration_us);
    CHECK(!strcmp(a->mime, "audio/mp4a-latm") && a->channels == 2 && a->sample_rate == 48000, "audio: audio/mp4a-latm, 2 channels, 48 kHz (%s, %d, %d)", a->mime, a->channels, a->sample_rate);
    mav_reader *r = mav_reader_new(m, vt, 0, 1); mav_sample s; int k = 0, sync0 = 0, order = 1; int64_t last = -1;
    while (r && mav_reader_next(r, &s) == 1) { if (!k) sync0 = s.sync; if (s.pts_us <= last) order = 0; last = s.pts_us; k++; }
    mav_reader_free(r);
    CHECK(k == 30 && sync0 && order, "compressed video: 30 samples, the first a key frame, in order (%d, key %d)", k, sync0);
    r = mav_reader_new(m, vt, 0, 0); k = 0; int size_ok = 1, y = 0, u = 0, w = 0;
    while (r && mav_reader_next(r, &s) == 1) {
        if (s.size != 320 * 240 * 3 / 2) size_ok = 0;
        if (k == 15) { y = s.data[120 * 320 + 160]; u = s.data[320 * 240 + 60 * 320 + 160]; w = s.data[320 * 240 + 60 * 320 + 161]; }
        k++;
    }
    mav_reader_free(r);
    CHECK(k == 30 && size_ok, "decoded video: 30 frames of NV12 at 320 x 240 (%d, sizes %s)", k, size_ok ? "right" : "wrong");
    CHECK(abs(y - 81) <= 6 && abs(u - 90) <= 6 && abs(w - 240) <= 6, "  red: Y %d Cb %d Cr %d (81, 90, 240 within 6)", y, u, w);
    r = mav_reader_new(m, at, 0, 0); long bytes = 0; int peak = 0;
    while (r && mav_reader_next(r, &s) == 1) { bytes += (long)s.size; for (size_t i = 0; i + 1 < s.size; i += 2) { int x = abs((int16_t)(s.data[i] | s.data[i + 1] << 8)); if (x > peak) peak = x; } }
    mav_reader_free(r);
    CHECK(bytes > 4 * 44000 && bytes < 4 * 52000 && peak > 8000, "decoded audio: about 1 s of 16-bit stereo with the sine in it (%ld bytes, peak %d)", bytes, peak);
    r = mav_reader_new(m, vt, 500000, 0); int64_t first = -1;
    if (r && mav_reader_next(r, &s) == 1) first = s.pts_us;
    mav_reader_free(r);
    CHECK(first >= 500000 && first < 540000, "decoded from 0.5 s: the first frame there (%lld us)", (long long)first);
    mav_release(m);
    snprintf(mp4, sizeof mp4, "%s/movie1080.mp4", dir); m = mav_open(mp4); k = 0; y = u = w = 0; size_ok = 1;
    r = m ? mav_reader_new(m, 0, 0, 0) : NULL;
    while (r && mav_reader_next(r, &s) == 1) {
        if (s.size != 1920 * 1080 * 3 / 2) size_ok = 0;
        if (!k) { y = s.data[1079 * 1920 + 960]; u = s.data[1920 * 1080 + 539 * 1920 + 960]; w = s.data[1920 * 1080 + 539 * 1920 + 961]; }
        k++;
    }
    mav_reader_free(r); if (m) mav_release(m);
    CHECK(k == 6 && size_ok && abs(y - 41) <= 6 && abs(u - 240) <= 6 && abs(w - 110) <= 6, "1920 x 1080: 6 frames, blue to the last row (Y %d Cb %d Cr %d; 41, 240, 110)", y, u, w);
    mav_media *wm = mav_open(webm); int ok = wm && mav_track_count(wm) == 1 && !strcmp(mav_track_at(wm, 0)->mime, "video/x-vnd.on2.vp9");
    k = 0; r = ok ? mav_reader_new(wm, 0, 0, 1) : NULL; while (r && mav_reader_next(r, &s) == 1) k++;
    mav_reader_free(r);
    CHECK(ok && k == 30 && wm && !mav_reader_new(wm, 0, 0, 0), "movie.webm: VP9, 30 frames compressed, no decoded reader (%d)", k);
    if (wm) mav_release(wm);
    printf("t_movie: %d checks, %d failed\n", n, fail); return fail != 0;
}
