// The movie container reader (host/movie_av.h; AVFoundation's on Apple, media/movie_av.m) for the Android host before it has one (plan item 301c):
// no file opens, so hle_movie.c treats the movie as missing. WebM movies (VP8 / VP9, media/movie_webm.c) are read as on Apple; MediaCodec's
// H.264 / AAC comes with 301d.
#include "movie_av.h"
#include "dm.h"

mav_media *mav_open(const char *file) { static int said; if (!said++) LOG("[red] movie %s: no container reader on this host yet (MP4 / MOV)", file); return NULL; }
void mav_retain(mav_media *m) { (void)m; }
void mav_release(mav_media *m) { (void)m; }
int mav_track_count(const mav_media *m) { (void)m; return 0; }
const mav_track *mav_track_at(const mav_media *m, int i) { (void)m; (void)i; return NULL; }
mav_reader *mav_reader_new(mav_media *m, int track, int64_t start_us, int compressed) { (void)m; (void)track; (void)start_us; (void)compressed; return NULL; }
void mav_reader_free(mav_reader *r) { (void)r; }
int mav_reader_next(mav_reader *r, mav_sample *out) { (void)r; (void)out; return -1; }
