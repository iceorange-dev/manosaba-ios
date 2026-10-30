// The rest of AudioOut (plan item 64j), on a silent output whose queue is taken by hand (pull, as the sound card callback does): three stereo
// buffers of 480 frames appended (1000, 1000, 30000 in every sample)
//   GetAudioOutBufferCount 3; ContainsAudioOutBuffer the second yes, a buffer never appended no
//   started, 480 frames taken: played 480, the first released and still contained (not taken back), the count 2
//   volume 0.5: the next frame's samples 500; GetAudioOutVolume 0.5
//   volume 2: the second buffer's 479 frames at 2000 and the third's first frame saturated at 32767; played 961
//   a negative volume kept as given and played as silence
//   FlushAudioOutBuffers stopped: false, nothing moved; started: true, the third (part played) released unplayed after the two played, the count 0
//   started again with nothing queued: played 0 (since this StartAudioOut)
// Volume not applied (-DDM_TEST_AUDIO_VOLUME_IGNORED): 3 fail; the played count not started again (-DDM_TEST_AUDIO_PLAYED_KEPT): 1 fails.
// The output is in the registry the lifecycle and the watchdog read only once it is made (plan item 296; nxnative ce99604): in it first, as
// before (-DDM_TEST_AUDIO_PUBLISH_EARLY): 1 fails.
#include "../hle/hle_audio.c"
#include <time.h>
uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
static struct { const void *k; void *v; } om[8];
void *objmap_get(const void *k) { for (int i = 0; i < 8; i++) if (om[i].k == k) return om[i].v; return NULL; }
void objmap_put(const void *k, void *v) { for (int i = 0; i < 8; i++) if (!om[i].k) { om[i].k = k; om[i].v = v; return; } }
void *objmap_del(const void *k) { for (int i = 0; i < 8; i++) if (om[i].k == k) { om[i].k = 0; return om[i].v; } return NULL; }
void hle_signal_system_event(void *g) { (void)g; }
void hle_bind_system_event(void *g, int autoclear) { (void)g; (void)autoclear; }
static int n, fail;
static int g_built_when_registered = -1;
static void on_registered(struct aout *a) { g_built_when_registered = a->built; }
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static int all(const int16_t *s, size_t k, int16_t v) { for (size_t i = 0; i < k; i++) if (s[i] != v) return 0; return 1; }

int main(void) {
    setenv("DM_AUDIO", "0", 1);
    char out[64], ev[64]; uint8_t prm[16]; gbuf b[3], never = {0}; static int16_t pcm[3][480 * 2], got[480 * 2];
    h_au_InitParam(prm);
    g_test_ao_registered = on_registered;
    if (h_au_Open(out, ev, NULL, prm)) return 1;
    check("the output is in the registry the lifecycle and the watchdog read only once it is made", g_built_when_registered == 1);
    aout *a = ao(out);
    for (int i = 0; i < 3; i++) { for (int j = 0; j < 960; j++) pcm[i][j] = i < 2 ? 1000 : 30000; h_au_SetBuf(&b[i], pcm[i], sizeof pcm[i], sizeof pcm[i]); h_au_Append(out, &b[i]); }
    check("three appended: count 3, the second contained, one never appended not", h_au_Count(out) == 3 && h_au_Contains(out, &b[1]) && !h_au_Contains(out, &never));
    atomic_store(&a->state, STATE_STARTED);   // started without a consumer: the test takes the queue itself
    pull(a, (uint8_t *)got, 480 * 4);
    check("480 frames taken: played 480, the first released and still contained (not taken back), count 2",
          h_au_Played(out) == 480 && h_au_Contains(out, &b[0]) && h_au_Count(out) == 2);
    h_au_SetVol(out, 0.5f); pull(a, (uint8_t *)got, 4);
    check("volume 0.5: the next frame 500 500; GetAudioOutVolume 0.5", got[0] == 500 && got[1] == 500 && h_au_GetVol(out) == 0.5f);
    h_au_SetVol(out, 2.0f); pull(a, (uint8_t *)got, 479 * 4 + 4);
    check("volume 2: the second buffer's 479 frames at 2000, the third's first frame saturated at 32767", all(got, 479 * 2, 2000) && got[958] == 32767 && got[959] == 32767);
    check("... played 961", h_au_Played(out) == 961);
    h_au_SetVol(out, -1.0f); pull(a, (uint8_t *)got, 8);
    check("a negative volume: kept as given, played as silence", h_au_GetVol(out) == -1.0f && all(got, 4, 0));
    atomic_store(&a->state, STATE_STOPPED);
    int f0 = h_au_Flush(out), c0 = h_au_Count(out);
    atomic_store(&a->state, STATE_STARTED);
    int f1 = h_au_Flush(out);
    gbuf *r0 = h_au_Released(out), *r1 = h_au_Released(out), *r2 = h_au_Released(out), *r3 = h_au_Released(out);
    check("flush stopped: false, nothing moved; started: true, the third released after the two played, count 0",
          !f0 && c0 == 1 && f1 && r0 == &b[0] && r1 == &b[1] && r2 == &b[2] && !r3 && h_au_Count(out) == 0 && a->pos == 0);
    atomic_store(&a->state, STATE_STOPPED); h_au_Start(out);
    check("started again with nothing queued: played 0 (since this StartAudioOut)", h_au_Played(out) == 0);
    h_au_Close(out);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
