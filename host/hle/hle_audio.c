// nn::audio AudioOut: the game (FMOD's nx output layer) appends PCM buffers in blocks; once played the system puts them back on the "released" queue and signals an event.
// Host: real sound = CoreAudio AudioUnit (Mac DefaultOutput / iOS RemoteIO); silent sink = buffers consumed in real time (no sound, same pacing as the device).
//       iOS plays real sound by default (the product must have sound), tests set DM_AUDIO=0 explicitly for silence; the Mac command-line host defaults to the silent sink (replays / gates are silent), DM_AUDIO=1 for real sound;
//       the Mac App (app_mac.m) is the product and sets DM_AUDIO=1 at launch ("no sound": the App used to take the command-line default and never played sound).
//       If the sound card cannot be opened (no output device etc.) it no longer exits: logs red, and this channel is consumed in real time by the silent sink while the game runs on.
// The contents of AudioOut / AudioOutBuffer / AudioOutParameter are read and written only through the interfaces below; the host defines its own internal layout and writes only the front part of each struct.
#include "audren_pace.h"
#include "hle.h"
#include "plat.h"
#include <TargetConditionals.h>
#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#elif defined(__ANDROID__)   // AAudio (plan item 301d): a stream with a data callback takes the AudioUnit's place, under the AudioUnit's names
#include <aaudio/AAudio.h>
typedef AAudioStream *AudioUnit; typedef int32_t OSStatus;
static inline OSStatus AudioOutputUnitStart(AudioUnit u) { return AAudioStream_requestStart(u); }
static inline OSStatus AudioOutputUnitStop(AudioUnit u) { return AAudioStream_requestStop(u); }
static inline OSStatus AudioUnitUninitialize(AudioUnit u) { (void)u; return 0; }
static inline OSStatus AudioComponentInstanceDispose(AudioUnit u) { return AAudioStream_close(u); }
#else   // no sound card backend here: au_open fails, every output plays to the silent sink, and the AudioUnit calls below never run (a->au stays NULL)
typedef void *AudioUnit; typedef int32_t OSStatus;
static inline OSStatus AudioOutputUnitStart(AudioUnit u) { (void)u; return -1; }
static inline OSStatus AudioOutputUnitStop(AudioUnit u) { (void)u; return 0; }
static inline OSStatus AudioUnitUninitialize(AudioUnit u) { (void)u; return 0; }
static inline OSStatus AudioComponentInstanceDispose(AudioUnit u) { (void)u; return 0; }
#endif
#include <stdatomic.h>
#include <unistd.h>

enum { FMT_PCM16 = 2, STATE_STARTED = 0, STATE_STOPPED = 1, RATE = 48000 };
enum { BK_NONE, BK_AU, BK_NULL };   // what consumes an output's queue right now (aout.backend)

typedef struct { void *ptr; uint64_t cap, size; } gbuf;     // The first 24 bytes written into the guest AudioOutBuffer
#define QMAX 64
#define RELMAX QMAX                    // Released max = number of in-flight buffers (the game must take a buffer back before it can append again), same limit as pending
typedef struct aout {
    pthread_mutex_t m;
    void *event;                       // Guest SystemEvent: signaled whenever a buffer is released
    int ch;
    _Atomic int state;
    gbuf *q[QMAX]; _Atomic uint32_t q_head, q_tail;       // Pending: written by the game side, read by the output side
    gbuf *rel[RELMAX]; _Atomic uint32_t rel_head, rel_tail; // Released: written by the output side, read by the game side
    uint64_t pos;                      // Bytes already played of the current block
    AudioUnit au;
    int mix;                           // 1 = the game gives 5.1 (6 channels), downmixed to stereo by us before handing to CoreAudio (au_render)
    int16_t *mixbuf;                   // 6-channel staging for the downmix (MIXMAX frames)
    pthread_t null_th; _Atomic int null_run;
    int backend;                       // BK_*: the running consumer; Start, Stop and the background suspend change it only under g_aos_lock and act on it
    _Atomic int sinks;                 // silent sinks running on this output (more than one, or one beside the AudioUnit, is the race g_aos_lock prevents)
    int built;                         // its sound card output made (or the silent sink chosen): before it is in g_aos
    dm_sem notify_sem;
    dm_sem rel_sem;      // signaled with every release batch: what dm_pcmout_wait blocks on (the audio renderer used to poll every millisecond)
    pthread_t notify_th; _Atomic int notify_run, notify_pending, notify_armed;
    _Atomic uint64_t under_n, under_b;   // underruns while started: callbacks or sink steps that found the queue empty, and the bytes filled with zeros (plan item 134)
    _Atomic uint64_t cb_n;               // sound card callbacks of this output: the watchdog's evidence that the device still plays it
    uint64_t wd_n, wd_t, said_under;     // watchdog (under g_aos_lock): callback count last seen and when it last moved; underruns already logged
    int stalled, retry_ms; uint64_t retry_at;   // the device stopped calling back (or refused to start): the silent sink stands in, the AudioUnit is retried at retry_at
    _Atomic float vol;                   // SetAudioOutVolume's gain, applied as the game's samples are taken (1 = as they are)
    _Atomic uint64_t played_b;           // bytes of the game's buffers played since the last StartAudioOut (GetAudioOutPlayedSampleCount)
} aout;

// Registry of open outputs: suspended / resumed all at once by the front-end lifecycle (going to background). g_aos_lock also serializes every backend
// change (Start / Stop / suspend / resume): Start used to mark the output started and pick its backend outside it, so a suspend between them started a
// silent sink and Start a second one over the same single-consumer queues (or the AudioUnit beside it), and a Stop racing a suspend joined a sink that
// never existed (nxnative 8530042, MIT; t_audio scenario 4)
#define AOMAX 8
static aout *g_aos[AOMAX];
static pthread_mutex_t g_aos_lock = PTHREAD_MUTEX_INITIALIZER;
struct aout;
static void (*g_test_ao_registered)(struct aout *a);   // t_audio_extras: an output just put in g_aos (under g_aos_lock)
static int g_suspended;
static _Atomic unsigned g_consumer_dupes;   // a second consumer on one output's queues (two silent sinks, or a sink beside the AudioUnit): reported red at the next backend stop
static aout *ao(const void *g) { aout *a = objmap_get(g); if (!a) die("AudioOut %p was not created by the host", g); return a; }

static void notify_release(aout *a) {
    dm_sem_post(a->rel_sem);
    atomic_store_explicit(&a->notify_pending, 1, memory_order_release);
    if (atomic_exchange_explicit(&a->notify_armed, 1, memory_order_acq_rel) == 0)
        dm_sem_post(a->notify_sem);
}

static void *event_notify(void *p) {
    aout *a = p;
    for (;;) {
        dm_sem_wait(a->notify_sem, -1);
        if (atomic_exchange_explicit(&a->notify_pending, 0, memory_order_acq_rel) && a->event)
            hle_signal_system_event(a->event);
        atomic_store_explicit(&a->notify_armed, 0, memory_order_release);
        if (!atomic_load_explicit(&a->notify_run, memory_order_acquire)) break;
        if (atomic_load_explicit(&a->notify_pending, memory_order_acquire) &&
            atomic_exchange_explicit(&a->notify_armed, 1, memory_order_acq_rel) == 0)
            dm_sem_post(a->notify_sem);
    }
    return NULL;
}

static void gain16(uint8_t *p, uint64_t n, float v) {   // PCM16 samples times v, saturated
    for (uint64_t i = 0; i + 2 <= n; i += 2) { int16_t x; memcpy(&x, p + i, 2); float y = x * v; x = (int16_t)(y > 32767.0f ? 32767 : y < -32768.0f ? -32768 : y); memcpy(p + i, &x, 2); }
}
// Take n bytes from the pending queue and hand them to the output side; once a whole block has played, move it to released and notify the ordinary thread.
static void pull(aout *a, uint8_t *dst, uint64_t n) {
    int released = 0;
    uint32_t qh = atomic_load_explicit(&a->q_head, memory_order_relaxed);
    while (n) {
        uint32_t qt = atomic_load_explicit(&a->q_tail, memory_order_acquire);
        if (qh == qt || atomic_load_explicit(&a->state, memory_order_acquire) != STATE_STARTED) {
#ifndef DM_TEST_AUDIO_NO_UNDERRUN   // negative control of t_audio only: underruns not counted, as before
            if (qh == qt && atomic_load_explicit(&a->state, memory_order_relaxed) == STATE_STARTED) {   // started with nothing queued: the game is late
                atomic_fetch_add_explicit(&a->under_n, 1, memory_order_relaxed); atomic_fetch_add_explicit(&a->under_b, n, memory_order_relaxed); }
#endif
            memset(dst, 0, n); break; }
        gbuf *b = a->q[qh & (QMAX - 1)];
        uint64_t k = b->size - a->pos; if (k > n) k = n;
        memcpy(dst, (uint8_t *)b->ptr + a->pos, k);
#ifndef DM_TEST_AUDIO_VOLUME_IGNORED   // negative control of t_audio_extras only: the volume not applied
        { float v = atomic_load_explicit(&a->vol, memory_order_relaxed); if (v != 1.0f) gain16(dst, k, v >= 0 ? v : 0); }
#endif
        atomic_fetch_add_explicit(&a->played_b, k, memory_order_relaxed);
        dst += k; n -= k; a->pos += k;
        if (a->pos >= b->size) {
            uint32_t rw = atomic_load_explicit(&a->rel_tail, memory_order_relaxed);
            uint32_t rr = atomic_load_explicit(&a->rel_head, memory_order_acquire);
            if (rw - rr == RELMAX) die("AudioOut released queue overflow");
            a->rel[rw & (RELMAX - 1)] = b;
            atomic_store_explicit(&a->rel_tail, rw + 1, memory_order_release);
            qh++;
            atomic_store_explicit(&a->q_head, qh, memory_order_release);
            a->pos = 0; released = 1;
        }
    }
    if (released) notify_release(a);
}

static _Atomic uint64_t g_render_calls;   // Cumulative sound card callbacks (lifecycle verification: must keep growing after resume)
uint64_t dm_audio_render_calls(void) { return atomic_load_explicit(&g_render_calls, memory_order_relaxed); }
// 5.1 -> stereo (previously: the game's 6 channels were handed to the AudioUnit as is without a channel layout; the system's implicit conversion took only the first two, and the centre - NPC dialogue - was dropped entirely).
// The game's channel order (measured in the Mac App by per-channel energy during the half second Akara starts speaking: only channel 3 jumps): front left, front right, centre, LFE, rear left, rear right.
// Coefficients from the ITU-R BS.775 Lo/Ro downmix: L = front left + 0.707*centre + 0.707*rear left, R symmetric; LFE is not mixed in (the standard downmix excludes LFE, and mixing it in muddies the low end);
// the whole is multiplied by 0.707 again for 3 dB of headroom, and anything still beyond 16 bits is saturated. Q15 fixed point: 0.707 = 23170, 0.707 x 0.707 = 16384
#define MIXMAX 4096
static void downmix51(const int16_t *in, int16_t *out, unsigned frames) {
    for (unsigned i = 0; i < frames; i++, in += 6, out += 2) {
        int32_t l = (23170 * in[0] + 16384 * in[2] + 16384 * in[4]) >> 15, r = (23170 * in[1] + 16384 * in[2] + 16384 * in[5]) >> 15;
        out[0] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l); out[1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r); }
}
// DM_AUDIO_MUTE=1 (tests on the Mac, at night): the sound card is opened and fed as usual and silence goes out (read at the first OpenAudioOut)
static int g_audio_mute = -1;
// The sound card's callback, whatever the backend: n bytes of the output's format (stereo after the 5.1 downmix) into out, on the audio thread
static void render_into(aout *a, void *out, uint32_t n) {
#ifdef DM_TEST_MUTE_NO_PULL   // negative control of t_audio only: muted without taking the game's blocks
    if (g_audio_mute > 0) { memset(out, 0, n); return; }
#endif
    atomic_fetch_add_explicit(&g_render_calls, 1, memory_order_relaxed); atomic_fetch_add_explicit(&a->cb_n, 1, memory_order_relaxed);
    if (atomic_load_explicit(&a->sinks, memory_order_relaxed)) atomic_fetch_add_explicit(&g_consumer_dupes, 1, memory_order_relaxed);   // counted only: no logging on the audio thread
    if (!a->mix) pull(a, out, n);
    else { int16_t *o = out; unsigned left = n / 4;   // stereo 16-bit: 4 bytes per frame
        while (left) { unsigned k = left < MIXMAX ? left : MIXMAX; pull(a, (uint8_t *)a->mixbuf, (uint64_t)k * 12); downmix51(a->mixbuf, o, k); o += 2 * k; left -= k; } }
    // Diagnostic switch DM_AUDIO_OUT_DUMP=<file>: append the data handed to the sound card as is (stereo after the downmix). This machine has no loopback device and cannot record the system output; to tell whether the dialogue was handed over, look at this file
    { static FILE *df; static int on = -1; if (on < 0) { const char *e = getenv("DM_AUDIO_OUT_DUMP"); on = e != NULL; if (on && !(df = fopen(e, "wb"))) LOG("[red] DM_AUDIO_OUT_DUMP: cannot open %s", e); }
      if (df) fwrite(out, 1, n, df); }
    if (g_audio_mute > 0) memset(out, 0, n);   // (after taking the blocks: the game's pacing goes on)
}
#ifdef __APPLE__
static OSStatus au_render(void *ctx, AudioUnitRenderActionFlags *f, const AudioTimeStamp *ts, UInt32 bus, UInt32 frames, AudioBufferList *bl) {
    (void)f; (void)ts; (void)bus; (void)frames;
    render_into(ctx, bl->mBuffers[0].mData, bl->mBuffers[0].mDataByteSize); return noErr;
}
#if !TARGET_OS_IPHONE
static void dev_name(AudioObjectID id, char *o, size_t n) {   // output device name (for the log)
    CFStringRef nm = NULL; UInt32 sz = sizeof nm; AudioObjectPropertyAddress pa = { kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    o[0] = 0; if (!AudioObjectGetPropertyData(id, &pa, 0, NULL, &sz, &nm) && nm) { CFStringGetCString(nm, o, (CFIndex)n, kCFStringEncodingUTF8); CFRelease(nm); } }
#endif
// The open runs on the thread that calls OpenAudioOut. Measured on an M5 (plan item 55): 47 ms for a process's first AudioUnit (the component
// loads), 0.5-1 ms after; tomodachi-native once saw CoreAudio take about 40 s and moved the open to a queue (d7f52e1). The time is logged; above 1 s
// in red (the iPhone's is still to be seen, item 70)
__attribute__((weak)) const char *thr_cur_name(char *nb, size_t n) { (void)nb; (void)n; return NULL; }   // hle_thread.c; the unit tests link without it
static int au_open(aout *a) {   // 0 = the sound card is open; -1 = it cannot be opened (a->au set to null, the caller uses the silent sink)
    uint64_t t0 = now_ns();
    AudioComponentDescription d = {.componentType = kAudioUnitType_Output,
#if TARGET_OS_IPHONE
                                   .componentSubType = kAudioUnitSubType_RemoteIO,
#else
                                   .componentSubType = kAudioUnitSubType_DefaultOutput,
#endif
                                   .componentManufacturer = kAudioUnitManufacturer_Apple};
    AudioComponent c = AudioComponentFindNext(NULL, &d);
    if (!c || AudioComponentInstanceNew(c, &a->au)) { a->au = NULL; LOG("[red] cannot create the AudioUnit: this output uses a silent sink"); return -1; }
    a->mix = a->ch == 6; if (a->mix && !a->mixbuf) a->mixbuf = malloc(MIXMAX * 12);   // 5.1: we downmix ourselves and the AudioUnit only takes stereo (mono / stereo are passed through as before)
    if (a->mix && !a->mixbuf) { AudioComponentInstanceDispose(a->au); a->au = NULL; LOG("[red] cannot allocate the downmix buffer: this output uses a silent sink"); return -1; }
    int och = a->mix ? 2 : a->ch;
    AudioStreamBasicDescription f = {.mSampleRate = RATE, .mFormatID = kAudioFormatLinearPCM,
                                     .mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked,
                                     .mBytesPerPacket = 2u * och, .mFramesPerPacket = 1, .mBytesPerFrame = 2u * och,
                                     .mChannelsPerFrame = (UInt32)och, .mBitsPerChannel = 16};
    if (a->mix) LOG("audio: the game gives 6 channels (5.1) → downmixed to stereo (center / surround ×0.707 into left and right, no LFE, −3 dB overall, saturating)");
    AURenderCallbackStruct cb = {au_render, a};
    if (AudioUnitSetProperty(a->au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f) ||
        AudioUnitSetProperty(a->au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) ||
        AudioUnitInitialize(a->au)) { AudioComponentInstanceDispose(a->au); a->au = NULL; LOG("[red] AudioUnit setup failed (no output device?): this output uses a silent sink"); return -1; }
#if !TARGET_OS_IPHONE
    {   // the default output unit follows the system default output device: record both, to check that "the sound comes out of the device the system chose"
        AudioObjectID cur = 0, def = 0; UInt32 sz = sizeof cur; char cn[128], dn[128];
        AudioUnitGetProperty(a->au, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &cur, &sz);
        AudioObjectPropertyAddress pa = { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain }; sz = sizeof def;
        AudioObjectGetPropertyData(kAudioObjectSystemObject, &pa, 0, NULL, &sz, &def); dev_name(cur, cn, sizeof cn); dev_name(def, dn, sizeof dn);
        LOG("audio: output device \"%s\" (%u), system default output \"%s\" (%u)%s", cn, (unsigned)cur, dn, (unsigned)def, cur == def ? ", same" : " [red] different"); }
#endif
    { double ms = (now_ns() - t0) / 1e6; char tn[64] = ""; const char *gn = thr_cur_name(tn, sizeof tn); if (!gn) { pthread_getname_np(pthread_self(), tn, sizeof tn); gn = tn; }
      LOG("audio: CoreAudio output opened in %.1f ms on %s thread \"%s\"%s", ms, thr_cur_name(tn, 1) ? "the guest" : "a host", gn, ms > 1000 ? " [red] slow: that thread waited (tomodachi-native once saw 40 s)" : ""); }
    return 0;
}
#elif defined(__ANDROID__)
static aaudio_data_callback_result_t aa_render(AAudioStream *s, void *ctx, void *out, int32_t frames) {
    (void)s; aout *a = ctx; render_into(a, out, (uint32_t)frames * 2u * (a->mix ? 2u : (uint32_t)a->ch)); return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
// The stream lost its device (a headset unplugged, the audio server restarted): it stops calling back, the watchdog hands the output to the silent
// sink, and au_go opens a new stream when it retries (au_revive). This runs on a thread of AAudio's, not the audio one
static void aa_error(AAudioStream *s, void *ctx, aaudio_result_t err) { (void)s; (void)ctx; LOG("audio: AAudio stream error %s", AAudio_convertResultToText(err)); }
static int au_open(aout *a) {   // 0 = the stream is open; -1 = it cannot be (a->au null, the caller uses the silent sink)
    uint64_t t0 = now_ns();
    a->mix = a->ch == 6; if (a->mix && !a->mixbuf) a->mixbuf = malloc(MIXMAX * 12);   // 5.1: downmixed to stereo here, as on Apple
    if (a->mix && !a->mixbuf) { a->au = NULL; LOG("[red] cannot allocate the downmix buffer: this output uses a silent sink"); return -1; }
    int och = a->mix ? 2 : a->ch; AAudioStreamBuilder *b = NULL; aaudio_result_t r = AAudio_createStreamBuilder(&b);
    if (r == AAUDIO_OK) {
        AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16); AAudioStreamBuilder_setChannelCount(b, och); AAudioStreamBuilder_setSampleRate(b, RATE);
        AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY); AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
        AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_GAME); AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);
        AAudioStreamBuilder_setDataCallback(b, aa_render, a); AAudioStreamBuilder_setErrorCallback(b, aa_error, a);
        r = AAudioStreamBuilder_openStream(b, &a->au); AAudioStreamBuilder_delete(b);
    }
    if (r != AAUDIO_OK) { a->au = NULL; LOG("[red] audio: AAudio refused the stream (%s): this output uses a silent sink", AAudio_convertResultToText(r)); return -1; }
    // two bursts queued: the low-latency path's usual depth (the game queues 4 x 240 frames ahead)
    int32_t burst = AAudioStream_getFramesPerBurst(a->au); if (burst > 0) AAudioStream_setBufferSizeInFrames(a->au, 2 * burst);
    if (a->mix) LOG("audio: the game gives 6 channels (5.1) → downmixed to stereo (center / surround ×0.707 into left and right, no LFE, −3 dB overall, saturating)");
    LOG("audio: AAudio output opened in %.1f ms: %d Hz, %d channels, burst %d frames, buffer %d of %d frames, %s, %s", (now_ns() - t0) / 1e6,
        AAudioStream_getSampleRate(a->au), AAudioStream_getChannelCount(a->au), burst, AAudioStream_getBufferSizeInFrames(a->au), AAudioStream_getBufferCapacityInFrames(a->au),
        AAudioStream_getPerformanceMode(a->au) == AAUDIO_PERFORMANCE_MODE_LOW_LATENCY ? "low latency" : "[red] not the low-latency path",
        AAudioStream_getSharingMode(a->au) == AAUDIO_SHARING_MODE_EXCLUSIVE ? "exclusive" : "shared");
    return 0;
}
#else
static int au_open(aout *a) {   // no sound card backend on this host: the silent sink plays every output
    static int said; if (!said++) LOG("audio: no sound card backend on this host: outputs play to the silent sink");
    a->au = NULL; return -1;
}
#endif
static int ao_index(const aout *a);
// Before a start: an AAudio stream that lost its device cannot start again, a new one is opened (Apple's units stay valid)
static void au_revive(aout *a) {
#ifdef __ANDROID__
    if (a->au && AAudioStream_getState(a->au) == AAUDIO_STREAM_STATE_DISCONNECTED) {
        AAudioStream_close(a->au); a->au = NULL;
        LOG("audio #%d: the stream lost its device: %s", ao_index(a), au_open(a) ? "[red] no new one" : "a new one opened");
    }
#else
    (void)a;
#endif
}
// Silent sink: consumes in real time at 48 kHz every 5 ms; more than 100 ms behind (the thread was frozen in an iOS suspension), it starts again
// from now instead of catching up in a burst (audren_pace.h sink_step, plan item 122)
static void *null_sink(void *p) {
    aout *a = p; uint8_t scratch[RATE / 200 * 2 * 8];
    uint64_t bytes = (uint64_t)RATE / 200 * 2 * a->ch, t = now_ns(); unsigned resyncs = 0, said = 0;
    if (atomic_fetch_add_explicit(&a->sinks, 1, memory_order_acq_rel)) atomic_fetch_add_explicit(&g_consumer_dupes, 1, memory_order_relaxed);
    while (atomic_load_explicit(&a->null_run, memory_order_acquire)) {
        uint64_t w = sink_step(&t, now_ns(), 5000000, 100000000, &resyncs); if (w) usleep((useconds_t)(w / 1000));
        if (resyncs != said && said < 4) { said = resyncs; LOG("audio: silent sink more than 100 ms behind (a stall or a suspension): started again from now (%u times)", resyncs); }
        pull(a, scratch, bytes);
    }
    atomic_fetch_sub_explicit(&a->sinks, 1, memory_order_acq_rel);
    return NULL;
}
// Backend changes; callers hold g_aos_lock. pull() takes no lock, so stopping the AudioUnit or joining a sink under it cannot deadlock
static void null_start(aout *a) { atomic_store_explicit(&a->null_run, 1, memory_order_release); pthread_create(&a->null_th, NULL, null_sink, a); a->backend = BK_NULL; }
static void null_stop(aout *a) { atomic_store_explicit(&a->null_run, 0, memory_order_release); pthread_join(a->null_th, NULL); a->backend = BK_NONE; }
static void backend_stop(aout *a) {
    if (a->backend == BK_AU) { AudioOutputUnitStop(a->au); a->backend = BK_NONE; }
    else if (a->backend == BK_NULL) null_stop(a);
    static unsigned seen; unsigned d = atomic_load_explicit(&g_consumer_dupes, memory_order_relaxed);
    if (d != seen) { seen = d; LOG("[red] audio output: a second consumer ran on one output (two silent sinks, or a sink beside the AudioUnit; %u times so far)", d); }
}
static int real_audio(void) { const char *e = getenv("DM_AUDIO"); return e ? atoi(e) != 0 : TARGET_OS_IPHONE; }
static int ao_index(const aout *a) { for (int i = 0; i < AOMAX; i++) if (g_aos[i] == a) return i; return -1; }
// Start the AudioUnit of an output (callers hold g_aos_lock; no backend running). When the device refuses (the session is held by a call or being
// reset) the silent sink stands in and the watchdog retries after 1, 2, 4, then every 8 s (nxnative b301431, MIT; it used to drop the unit for good)
static void au_go(aout *a, uint64_t now) {
    au_revive(a);
    OSStatus st = a->au ? AudioOutputUnitStart(a->au) : -1;
    a->wd_n = atomic_load_explicit(&a->cb_n, memory_order_relaxed); a->wd_t = now;
    if (!st) { a->backend = BK_AU; a->stalled = 0; return; }
    null_start(a);
    if (!a->stalled) { a->stalled = 1; a->retry_ms = 1000; LOG("[red] audio #%d: the AudioUnit did not start (status %d): the silent sink stands in, retrying", ao_index(a), (int)st); }
    else if (a->retry_ms < 8000) a->retry_ms *= 2;
    a->retry_at = now + (uint64_t)a->retry_ms * 1000000;
}
// Watchdog (plan item 134, after nxnative b301431): a started AudioUnit must keep calling back. iOS stops calling back without telling the unit when
// another session takes over (Siri, an alarm, a call) or the media services reset; the game's buffers were then never released and a game that waits
// for them stayed silent. After 200 ms without a callback the silent sink takes over (the game's audio keeps its pace) and the unit is retried after
// 1, 2, 4, 8 s. A late wake-up of the watchdog itself (the process was stopped) only restarts the count. It also logs each output's new underruns,
// at most every 5 s (the audio thread never logs). DM_TEST_AUDIO_NO_WATCHDOG: negative control of t_audio only
enum { WD_TICK_MS = 50, WD_STALL_MS = 200, WD_UNDER_MS = 5000 };
static void *au_watchdog(void *p) {
    (void)p; uint64_t last = now_ns(), said = last;
    for (;;) {
        usleep(WD_TICK_MS * 1000); uint64_t t = now_ns(), gap = t - last; last = t;
        int log_under = t - said >= WD_UNDER_MS * 1000000ull; if (log_under) said = t;
        pthread_mutex_lock(&g_aos_lock);
        for (int i = 0; i < AOMAX; i++) {
            aout *a = g_aos[i]; if (!a) continue;
            if (log_under) { uint64_t u = atomic_load_explicit(&a->under_n, memory_order_relaxed);
                if (u != a->said_under) { LOG("audio #%d: %llu underruns so far (+%llu in the last %d s; %.1f ms of zeros in all): the game's blocks came late", i, (unsigned long long)u,
                                             (unsigned long long)(u - a->said_under), WD_UNDER_MS / 1000, atomic_load_explicit(&a->under_b, memory_order_relaxed) / (2.0 * a->ch * RATE / 1000)); a->said_under = u; } }
#ifndef DM_TEST_AUDIO_NO_WATCHDOG
            if (!a->au) continue;
            uint64_t n = atomic_load_explicit(&a->cb_n, memory_order_relaxed);
            if (a->backend != BK_AU || g_suspended || gap > 4ull * WD_TICK_MS * 1000000 || n != a->wd_n) {
                if (a->backend == BK_AU && n != a->wd_n) a->retry_ms = 0;   // callbacks seen: a later stall starts the back-off from 1 s again
                a->wd_n = n; a->wd_t = t;
            } else if (t - a->wd_t > WD_STALL_MS * 1000000ull) {
                LOG("[red] audio #%d: the device has not called back for %.0f ms while playing: the silent sink stands in, retrying the AudioUnit", i, (t - a->wd_t) / 1e6);
                AudioOutputUnitStop(a->au); a->backend = BK_NONE; null_start(a);
                a->stalled = 1; a->retry_ms = a->retry_ms ? (a->retry_ms < 8000 ? a->retry_ms * 2 : 8000) : 1000; a->retry_at = t + (uint64_t)a->retry_ms * 1000000;
            }
            if (a->stalled && a->backend == BK_NULL && !g_suspended && t >= a->retry_at && atomic_load_explicit(&a->state, memory_order_relaxed) == STATE_STARTED) {
                null_stop(a); au_go(a, t);
                if (a->backend == BK_AU) LOG("audio #%d: AudioUnit restarted", i);
            }
#endif
        }
        pthread_mutex_unlock(&g_aos_lock);
    }
    return NULL;
}
static void au_watchdog_start(void) { pthread_t t; if (!pthread_create(&t, NULL, au_watchdog, NULL)) pthread_detach(t); }

// AudioOutParameter = { int32 sampleRate; uint16 channelCount; uint16 _r; ... }, 0 = system default (48 kHz / 2 channels)
HLE_FORCE("_ZN2nn5audio27InitializeAudioOutParameterEPNS0_17AudioOutParameterE", "audio output parameter defaults", void, h_au_InitParam, uint8_t *p) { memset(p, 0, 8); }
// AudioOutInfo = { char name[256] }
HLE_FORCE("_ZN2nn5audio13ListAudioOutsEPNS0_12AudioOutInfoEi", "audio output list: one", int, h_au_List, char *info, int n) {
    if (n < 1) return 0;
    memset(info, 0, 256); strcpy(info, "DeviceOut"); return 1;
}
HLE_FORCE("_ZN2nn5audio12OpenAudioOutEPNS0_8AudioOutEPNS_2os11SystemEventEPKcRKNS0_17AudioOutParameterE", "connects to the audout service; host AudioUnit / silent sink",
          Result, h_au_Open, void *g, void *ev, const char *name, const uint8_t *param) {
    int ch = *(const uint16_t *)(param + 4); int rate = *(const int32_t *)param;
    if (!ch) ch = 2;
    if (rate && rate != RATE) die("AudioOut sample rate %d not supported (48 kHz only)", rate);
    aout *a = calloc(1, sizeof *a);
    pthread_mutex_init(&a->m, NULL); a->event = ev; a->ch = ch;
    atomic_init(&a->state, STATE_STOPPED);
    atomic_init(&a->q_head, 0); atomic_init(&a->q_tail, 0);
    atomic_init(&a->rel_head, 0); atomic_init(&a->rel_tail, 0);
    atomic_init(&a->null_run, 0); atomic_init(&a->notify_run, 1);
    atomic_init(&a->notify_pending, 0); atomic_init(&a->notify_armed, 0);
    atomic_init(&a->vol, 1.0f); atomic_init(&a->played_b, 0);
    a->notify_sem = dm_sem_new(); a->rel_sem = dm_sem_new();
    // The thread that signals the game's buffer event at USER_INTERACTIVE (upstream d2r-mobile afad65c): at the default QoS (21) 16 of its wake-ups
    // in 240 s came more than 2 ms late on an iPhone 17 Pro Max, none after; a player heard gaps while moving (PR #52)
    { pthread_attr_t at; pthread_attr_init(&at); dm_attr_interactive(&at);
      pthread_create(&a->notify_th, &at, event_notify, a); pthread_attr_destroy(&at); }
    if (ev) hle_bind_system_event(ev, 1);   // Buffer release event: bind a kernel event on the guest thread (auto-clear), afterwards signaled by the audio thread
    objmap_put(g, a);
    if (g_audio_mute < 0) { const char *e = getenv("DM_AUDIO_MUTE"); g_audio_mute = e && atoi(e); if (g_audio_mute) LOG("audio: DM_AUDIO_MUTE=1: the sound card is fed, silence goes out"); }
    LOG("audio::OpenAudioOut %s, %d channels, %s", name ? name : "(default)", ch, real_audio() ? "real output" : "silent sink");
    // The sound card's output is made before the output is in g_aos, whose readers (the lifecycle's suspend / resume, the watchdog) read a->au under
    // g_aos_lock: it was put there first and made after without the lock (seedds/nxnative ce99604, MIT; plan item 296)
#ifndef DM_TEST_AUDIO_PUBLISH_EARLY   // negative control of t_audio_extras only: in g_aos first, as before
    if (real_audio()) au_open(a);   // if it cannot be opened, a->au is null, and playback goes to the silent sink when it starts
    a->built = 1;
#endif
    pthread_mutex_lock(&g_aos_lock);
    int slot = -1; for (int i = 0; i < AOMAX; i++) if (!g_aos[i]) { slot = i; break; }
    if (slot < 0) die("more than %d AudioOuts open at once", AOMAX);
    g_aos[slot] = a;
    if (g_test_ao_registered) g_test_ao_registered(a);
    pthread_mutex_unlock(&g_aos_lock);
#ifdef DM_TEST_AUDIO_PUBLISH_EARLY
    if (real_audio()) au_open(a);
    a->built = 1;
#endif
    { static pthread_once_t wd = PTHREAD_ONCE_INIT; pthread_once(&wd, au_watchdog_start); }   // the AudioUnit watchdog and the underrun lines
    return RES_OK;
}
// Default output device variants (Unity): same as OpenAudioOut with no device name; the overload without an event has the game poll GetReleasedAudioOutBuffer
HLE_FORCE_OPT("_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutEPNS_2os11SystemEventERKNS0_17AudioOutParameterE", "connects to the audout service (default device); host AudioUnit / silent sink",
              Result, h_au_OpenDef, void *g, void *ev, const uint8_t *param) { return h_au_Open(g, ev, NULL, param); }
HLE_FORCE_OPT("_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutERKNS0_17AudioOutParameterE", "connects to the audout service (default device, no release event); host AudioUnit / silent sink",
              Result, h_au_OpenDefNoEv, void *g, const uint8_t *param) { return h_au_Open(g, NULL, NULL, param); }
HLE_FORCE("_ZN2nn5audio13StartAudioOutEPNS0_8AudioOutE", "start playback", Result, h_au_Start, void *g) {
    aout *a = ao(g);
    pthread_mutex_lock(&g_aos_lock);
    if (atomic_load_explicit(&a->state, memory_order_acquire) != STATE_STARTED) {
#ifndef DM_TEST_AUDIO_PLAYED_KEPT   // negative control of t_audio_extras only: the played count not started again
        atomic_store_explicit(&a->played_b, 0, memory_order_relaxed);   // samples played since this StartAudioOut (the SDK reference)
#endif
        atomic_store_explicit(&a->state, STATE_STARTED, memory_order_release);
        if (a->au && !g_suspended) { a->stalled = 0; au_go(a, now_ns()); }   // a refusal: the silent sink, and the watchdog retries the unit
        else null_start(a);
    }
    pthread_mutex_unlock(&g_aos_lock);
    return RES_OK;
}
// The buffers still queued are released unplayed and the event signalled, as Ryujinx's AudioDeviceSession.Stop does (MIT, written anew; the SDK
// reference keeps them appended, and its event may be signalled whenever a buffer may have been released). Unity's FMOD output stops the output
// and then waits for its output thread, which waits for that event: on an operation mode change (plan item 302) the game hung there for good
static int release_pending(aout *a);
HLE_FORCE("_ZN2nn5audio12StopAudioOutEPNS0_8AudioOutE", "stop playback: the queued buffers released", void, h_au_Stop, void *g) {
    aout *a = ao(g);
    pthread_mutex_lock(&g_aos_lock);
    atomic_store_explicit(&a->state, STATE_STOPPED, memory_order_release);
    backend_stop(a);   // the AudioUnit, or the silent sink (including one that took over during suspension)
#ifndef DM_TEST_AUDIO_STOP_KEEPS   // negative control of t_audio_extras only: the queue left as it is, nothing signalled
    int moved = release_pending(a);
#else
    int moved = 0;
#endif
    pthread_mutex_unlock(&g_aos_lock);
    if (moved) notify_release(a);
}
HLE_FORCE("_ZN2nn5audio13CloseAudioOutEPNS0_8AudioOutE", "close the audio output", void, h_au_Close, void *g) {
    h_au_Stop(g);
    aout *a = objmap_del(g);
    atomic_store_explicit(&a->notify_run, 0, memory_order_release);
    dm_sem_post(a->notify_sem);
    pthread_join(a->notify_th, NULL);
    pthread_mutex_lock(&g_aos_lock);
    for (int i = 0; i < AOMAX; i++) if (g_aos[i] == a) g_aos[i] = NULL;
    pthread_mutex_unlock(&g_aos_lock);
    if (a->au) { AudioUnitUninitialize(a->au); AudioComponentInstanceDispose(a->au); }
    dm_sem_free(a->notify_sem); dm_sem_free(a->rel_sem);
    free(a->mixbuf); pthread_mutex_destroy(&a->m); free(a);
}
HLE_FORCE("_ZN2nn5audio16GetAudioOutStateEPKNS0_8AudioOutE", "playback state", int, h_au_State, const void *g) { return atomic_load_explicit(&ao(g)->state, memory_order_acquire); }
HLE_FORCE("_ZN2nn5audio21GetAudioOutSampleRateEPKNS0_8AudioOutE", "sample rate", int, h_au_Rate, const void *g) { (void)ao(g); return RATE; }
HLE_FORCE("_ZN2nn5audio23GetAudioOutChannelCountEPKNS0_8AudioOutE", "channel count", int, h_au_Ch, const void *g) { return ao(g)->ch; }
HLE_FORCE("_ZN2nn5audio23GetAudioOutSampleFormatEPKNS0_8AudioOutE", "sample format: PCM16 only", int, h_au_Fmt, const void *g) { (void)ao(g); return FMT_PCM16; }

HLE_FORCE("_ZN2nn5audio21SetAudioOutBufferInfoEPNS0_14AudioOutBufferEPvmm", "buffer description, written into the first 24 bytes of the guest struct", void, h_au_SetBuf, gbuf *b, void *p, uint64_t cap, uint64_t size) {
    b->ptr = p; b->cap = cap; b->size = size;
}
HLE_FORCE("_ZN2nn5audio28GetAudioOutBufferDataPointerEPKNS0_14AudioOutBufferE", "buffer data pointer", void *, h_au_BufPtr, const gbuf *b) { return b->ptr; }
HLE_FORCE("_ZN2nn5audio25GetAudioOutBufferDataSizeEPKNS0_14AudioOutBufferE", "buffer data size", uint64_t, h_au_BufSize, const gbuf *b) { return b->size; }
HLE_FORCE("_ZN2nn5audio20AppendAudioOutBufferEPNS0_8AudioOutEPNS0_14AudioOutBufferE", "append a buffer to play", unsigned, h_au_Append, void *g, gbuf *b) {
    aout *a = ao(g);
    // Diagnostic switch DM_AUDIO_DUMP=<file>: every PCM block the game appends is written out as is (16-bit, 48 kHz, channel count per the OpenAudioOut log, usually 6). Used to judge "did a given sound play or not":
    // cross-correlate the original game's audio against it (this is how the tier-one drop sound was verified). The file is opened only when the variable is set; when unset this costs only one extra cached check
    { static FILE *df; static int on = -1; if (on < 0) { const char *e = getenv("DM_AUDIO_DUMP"); on = e != NULL; if (on && !(df = fopen(e, "wb"))) LOG("[red] DM_AUDIO_DUMP: cannot open %s", e); }
      if (df && b && b->ptr && b->size) fwrite(b->ptr, 1, (size_t)b->size, df); }
    pthread_mutex_lock(&a->m);
    uint32_t qt = atomic_load_explicit(&a->q_tail, memory_order_relaxed);
    uint32_t qh = atomic_load_explicit(&a->q_head, memory_order_acquire);
    int ok = qt - qh < QMAX;
    if (ok) { a->q[qt & (QMAX - 1)] = b; atomic_store_explicit(&a->q_tail, qt + 1, memory_order_release); }
    pthread_mutex_unlock(&a->m);
    return ok;      // bool: false when the queue is full (the real device has a limit too)
}
// The rest of AudioOut (plan item 64j; after acnh-mobile's hle_audio.c, MIT, written anew, with the semantics of the SDK reference and yuzu's
// audio_out_system.cpp, facts only). Both games' subsdk0 import GetAudioOutPlayedSampleCount; the SDK's own looks the AudioOut up in its session
// table, finds none for a host output and reads through a null pointer (manosaba's sdk+0x1f8630; the others the same)
HLE_FORCE_OPT("_ZN2nn5audio17SetAudioOutVolumeEPNS0_8AudioOutEf", "output gain, applied as the samples are taken", void, h_au_SetVol, void *g, float v) {
    atomic_store_explicit(&ao(g)->vol, v, memory_order_relaxed);   // kept as given (GetAudioOutVolume returns it); a negative or NaN gain plays as 0
}
HLE_FORCE_OPT("_ZN2nn5audio17GetAudioOutVolumeEPKNS0_8AudioOutE", "output gain", float, h_au_GetVol, const void *g) { return atomic_load_explicit(&ao(g)->vol, memory_order_relaxed); }
HLE_FORCE_OPT("_ZN2nn5audio28GetAudioOutPlayedSampleCountEPKNS0_8AudioOutE", "sample frames of the game's buffers played since StartAudioOut", int64_t, h_au_Played, const void *g) {
    aout *a = ao(g); return (int64_t)(atomic_load_explicit(&a->played_b, memory_order_relaxed) / (2u * (unsigned)a->ch));
}
HLE_FORCE_OPT("_ZN2nn5audio22GetAudioOutBufferCountEPKNS0_8AudioOutE", "buffers appended and not played yet, the one playing included", int, h_au_Count, const void *g) {
    aout *a = ao(g); return (int)(atomic_load_explicit(&a->q_tail, memory_order_acquire) - atomic_load_explicit(&a->q_head, memory_order_acquire));
}
HLE_FORCE_OPT("_ZN2nn5audio22ContainsAudioOutBufferEPKNS0_8AudioOutEPKNS0_14AudioOutBufferE", "appended and not taken back yet (pending or released)", bool, h_au_Contains, const void *g, const gbuf *b) {
    aout *a = ao(g); int found = 0;
    pthread_mutex_lock(&a->m);
    for (uint32_t i = atomic_load_explicit(&a->rel_head, memory_order_acquire), e = atomic_load_explicit(&a->rel_tail, memory_order_acquire); i != e && !found; i++) found = a->rel[i & (RELMAX - 1)] == b;
    for (uint32_t i = atomic_load_explicit(&a->q_head, memory_order_acquire), e = atomic_load_explicit(&a->q_tail, memory_order_acquire); i != e && !found; i++) found = a->q[i & (QMAX - 1)] == b;
    pthread_mutex_unlock(&a->m);
    return found;
}
// Every pending buffer moved to the released queue unplayed (Flush, Stop; under g_aos_lock with the output's consumer stopped, so the queue has one
// writer); 1 when any was there. The caller signals the release after unlocking
static int release_pending(aout *a) {
    pthread_mutex_lock(&a->m);
    uint32_t h = atomic_load_explicit(&a->q_head, memory_order_relaxed), t = atomic_load_explicit(&a->q_tail, memory_order_acquire), rt = atomic_load_explicit(&a->rel_tail, memory_order_relaxed);
    for (uint32_t i = h; i != t; i++) a->rel[rt++ & (RELMAX - 1)] = a->q[i & (QMAX - 1)];   // pending + released never exceed QMAX (Append and the release move keep it)
    atomic_store_explicit(&a->rel_tail, rt, memory_order_release); atomic_store_explicit(&a->q_head, t, memory_order_release); a->pos = 0;
    pthread_mutex_unlock(&a->m);
    return t != h;
}
// Every pending buffer released at once, unplayed, and the event signalled: only while started (false otherwise, as yuzu). The output's consumer is
// held off by g_aos_lock's backend stop and start around the move, so the queue has one writer
HLE_FORCE_OPT("_ZN2nn5audio20FlushAudioOutBuffersEPNS0_8AudioOutE", "pending buffers released unplayed (started outputs only)", bool, h_au_Flush, void *g) {
    aout *a = ao(g);
    pthread_mutex_lock(&g_aos_lock);
    if (atomic_load_explicit(&a->state, memory_order_acquire) != STATE_STARTED) { pthread_mutex_unlock(&g_aos_lock); return false; }
    int was = a->backend; backend_stop(a);
    int moved = release_pending(a);
    if (was != BK_NONE) { if (was == BK_AU && a->au && !g_suspended) au_go(a, now_ns()); else null_start(a); }
    pthread_mutex_unlock(&g_aos_lock);
    if (moved) notify_release(a);
    return true;
}
HLE_FORCE("_ZN2nn5audio25GetReleasedAudioOutBufferEPNS0_8AudioOutE", "next played buffer, NULL if none", gbuf *, h_au_Released, void *g) {
    aout *a = ao(g); gbuf *b = NULL;
    pthread_mutex_lock(&a->m);
    uint32_t rr = atomic_load_explicit(&a->rel_head, memory_order_relaxed);
    uint32_t rw = atomic_load_explicit(&a->rel_tail, memory_order_acquire);
    if (rr != rw) { b = a->rel[rr & (RELMAX - 1)]; atomic_store_explicit(&a->rel_head, rr + 1, memory_order_release); }
    pthread_mutex_unlock(&a->m);
    return b;
}

// Host-internal PCM output for the in-process audio renderer (hle_audren.c): the same AudioOut machinery (real output / silent sink / lifecycle
// take-over), keyed by a host-allocated object instead of a guest AudioOut, without a release event. From nxnative's hle_audio.c (seedds, MIT)
void *dm_pcmout_open(int ch) {
    void *key = calloc(1, 64); uint8_t param[8] = {0}; *(uint16_t *)(param + 4) = (uint16_t)ch;
    if (!key || h_au_Open(key, NULL, "AudioRenderer", param)) die("host PCM output cannot be opened");
    return key;
}
void dm_pcmout_start(void *k) { h_au_Start(k); }
void dm_pcmout_close(void *k) { h_au_Close(k); free(k); }   // stop, leave the registry (frees its AOMAX slot), free it and its key (nxnative 8530042)
int dm_pcmout_append(void *k, void *b) { return (int)h_au_Append(k, b); }
void *dm_pcmout_released(void *k) { return h_au_Released(k); }
// Block until the output releases a buffer or ns pass (nxnative e06a8ec's dm_pcmout_wait, MIT): 1 = something was released (take it with
// dm_pcmout_released), 0 = timed out
int dm_pcmout_wait(void *k, int64_t ns) { return dm_sem_wait(ao(k)->rel_sem, ns); }

// Front-end lifecycle / audio session interruption: suspend real audio output, with the silent sink taking over real-time consumption meanwhile (pacing unbroken); switch back to the AudioUnit on resume.
// The front end calls with 1 on going to background / interruption began, and with 0 on returning to foreground / interruption ended (shouldResume).
void dm_audio_suspend(int on) {
    pthread_mutex_lock(&g_aos_lock);
    if (g_suspended != on) {
        g_suspended = on; int failed = 0;
        for (int i = 0; i < AOMAX; i++) {
            aout *a = g_aos[i];
            if (!a || !a->au) continue;
            // While suspended the silent sink consumes in real time instead: otherwise buffers are no longer released, the FMOD output thread waits forever and the game's audio pacing stalls (same for an incoming call interruption)
            if (on) { if (a->backend == BK_AU) { AudioOutputUnitStop(a->au); null_start(a); } }
            else if (a->backend == BK_NULL && atomic_load_explicit(&a->state, memory_order_relaxed) == STATE_STARTED) {
                // The session may still be held by someone else (back in the game with a call not yet hung up): the silent sink keeps covering and the
                // watchdog retries (it stayed "suspended" until the next resume before)
                null_stop(a); a->stalled = 0; au_go(a, now_ns()); failed |= a->backend != BK_AU; }
        }
        LOG("audio %s", on ? "paused (background / interruption)" : failed ? "resumed, an AudioUnit still refuses to start (the watchdog retries)" : "resumed (foreground / interruption ended)");
    }
    pthread_mutex_unlock(&g_aos_lock);
}
// The media services were reset (iOS, AVAudioSessionMediaServicesWereResetNotification): every AudioUnit is invalid. Each output's unit is made
// again and the playing ones restart; the front end has set the audio session up again first (plan item 134, after nxnative b301431, MIT)
void dm_audio_reset(void) {
    pthread_mutex_lock(&g_aos_lock);
    for (int i = 0; i < AOMAX; i++) {
        aout *a = g_aos[i]; if (!a || !a->au) continue;
        int playing = atomic_load_explicit(&a->state, memory_order_relaxed) == STATE_STARTED;
        backend_stop(a); AudioUnitUninitialize(a->au); AudioComponentInstanceDispose(a->au); a->au = NULL;
        if (au_open(a)) LOG("[red] audio #%d: no AudioUnit after the media services reset: this output stays on the silent sink", i);
        if (playing) { if (a->au && !g_suspended) { a->stalled = 0; au_go(a, now_ns()); } else null_start(a); }
    }
    LOG("audio outputs set up again after the media services reset");
    pthread_mutex_unlock(&g_aos_lock);
}
