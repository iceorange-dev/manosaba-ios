// nn::audio AudioRenderer (audren), rendered in process with the renderer the game's SDK ships, no mixer of our own. Newer SDKs link the renderer server
// side (nn::audio::server: InfoUpdater / CommandGenerator ...) and the CPU version of the DSP command processing (nn::audio::dsp::CommandListProcessor:
// ADPCM / PCM decoding, resampling, mixing, reverb / delay / biquad / limiter / compressor ...) into the application, for the "CPU rendering + manual
// execution" mode (AudioRendererParameter +0x2c renderingDevice = 1 CPU, +0x30 executionMode = 1 manual). In that mode OpenAudioRenderer goes through the
// in-process manager, RequestUpdate is parsed in process, and rendering runs on whichever thread calls ExecuteAudioRendererRendering, which then signals
// the game's renderer event. Without it the SDK opens the audren service through sm (TotK: GetAudioRendererWorkBufferSize aborts in sm::Initialize).
// The host:
//   1. takes over GetAudioRendererWorkBufferSize / OpenAudioRenderer: switches the parameter to CPU + manual and calls the SDK original;
//   2. runs a render thread (a guest thread, hle_guestthread.c) that renders one frame whenever the output hands back a buffer, so the sound card
//      clock paces it;
//   3. takes over ExecuteAudioRendererRendering: generates the commands and runs CommandListProcessor like the original, then reads the DeviceSink
//      command's input mix buffers (int32 holding 16-bit range) as int16 stereo for the PCM output (hle_audio.c dm_pcmout_*).
// Command list: header +0x04 command count, +0x08 mix buffer address, +0x10 buffer count, +0x14 samples per frame, +0x18 sample rate, commands from
// +0x20; each command +0x00 magic 0xcafebabe, +0x04 enabled, +0x05 command ID, +0x06 size; DeviceSink +0x128 upsampler buffer, +0x130 input count,
// +0x134 input indices (s16).
// From nxnative's hle_audren.c (seedds, MIT), written for The Legend of Zelda: Breath of the Wild 1.9.0, whose sdk module is the same binary as
// TotK 1.4.3's.
#include "hle.h"
#include <stdatomic.h>
#include <unistd.h>

void *dm_guest_thread_spawn(void (*fn)(void *), void *arg, int prio, size_t stack_size);
void dm_guest_thread_join(void *tt);
void *dm_pcmout_open(int ch);
void dm_pcmout_close(void *k);
void dm_pcmout_start(void *k);
int dm_pcmout_append(void *k, void *b);
void *dm_pcmout_released(void *k);
int dm_pcmout_wait(void *k, int64_t ns);

// P_SIZE: bytes of the game's AudioRendererParameter copied before the two fields are changed (SDK 16's struct is 0x38 bytes, SDK 8's has its
// revision at +0x38; the SDK reads only its own size of the copy)
enum { P_DEVICE = 0x2c, P_MODE = 0x30, P_SIZE = 0x40, FRAME_MAX = 1024, NBUF = 6, PREQ = 3 };
static void *gsym(const char *n) { void *p = guest_sym(n); if (!p) die("audren: no SDK symbol %s", n); return p; }
static void cpu_manual(uint8_t *dst, const uint8_t *src) { memcpy(dst, src, P_SIZE); *(int32_t *)(dst + P_DEVICE) = 1; *(int32_t *)(dst + P_MODE) = 1; }

HLE_FORCE_OPT("_ZN2nn5audio30GetAudioRendererWorkBufferSizeERKNS0_22AudioRendererParameterE", "renderer work buffer size for CPU rendering + manual execution",
              size_t, h_ar_WorkSize, const uint8_t *param) {
    static size_t (*orig)(const uint8_t *); if (!orig) orig = gsym("_ZN2nn5audio30GetAudioRendererWorkBufferSizeERKNS0_22AudioRendererParameterE");
    uint8_t p[P_SIZE]; cpu_manual(p, param); return orig(p);
}
HLE_FORCE_OPT("_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleERKNS0_22AudioRendererParameterEPvm", "open renderer: CPU rendering + manual execution in the SDK",
              Result, h_ar_Open0, void **handle, const uint8_t *param, void *work, size_t sz) {
    static Result (*orig)(void **, const uint8_t *, void *, size_t);
    if (!orig) orig = gsym("_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleERKNS0_22AudioRendererParameterEPvm");
    uint8_t p[P_SIZE]; cpu_manual(p, param); return orig(handle, p, work, sz);
}

// ---------- Rendering and output ----------
static void *g_handle[2];                    // AudioRendererHandle (renderer object, client state)
static _Atomic int g_open;
static void *g_render_tt;                    // the render thread (ThreadType from dm_guest_thread_spawn): Close joins it before the SDK tears the renderer down
static int16_t g_frame[FRAME_MAX * 2]; static int g_frame_n;   // the latest frame (stereo int16)
static _Atomic uint64_t g_frames, g_nonsilent;
enum { CQ = 4 }; static int16_t g_cq[CQ][FRAME_MAX * 2]; static int g_cq_n[CQ]; static _Atomic uint32_t g_cq_head, g_cq_tail;   // catch-up frames waiting for output

typedef struct { void *ptr; uint64_t cap, size; } gbuf;       // the front of hle_audio.c's AudioOutBuffer
static uint64_t g_frame_ns = 5000000;   // one frame's duration (frame samples / rate, from OpenAudioRenderer)
#include "audren_pace.h"
static void render_main(void *arg) {
    (void)arg;
    void (*exec)(void *, void *) = gsym("_ZN2nn5audio29ExecuteAudioRendererRenderingENS0_19AudioRendererHandleE");
    void *out = dm_pcmout_open(2);
    static int16_t data[NBUF][FRAME_MAX * 2]; static gbuf bufs[NBUF]; gbuf *free_list[NBUF]; int nfree = NBUF;
    for (int i = 0; i < NBUF; i++) { bufs[i] = (gbuf){data[i], sizeof data[i], 0}; free_list[i] = &bufs[i]; }
    dm_pcmout_start(out);
    int pre = (int)(g_frame_ns * 48000 / 1000000000ull); if (pre < 1 || pre > FRAME_MAX) pre = 240;
    for (int i = 0; i < PREQ; i++) { gbuf *b = free_list[--nfree]; memset(b->ptr, 0, (size_t)pre * 4); b->size = (uint64_t)pre * 4; if (!dm_pcmout_append(out, b)) free_list[nfree++] = b; }   // the device's buffering
    LOG("audren: render thread started (%d buffers in flight, a frame every %.2f ms on its own clock)", NBUF, g_frame_ns / 1e6);
    audren_pace pace = {0};
    while (atomic_load(&g_open)) {
        for (gbuf *b; nfree < NBUF && (b = dm_pcmout_released(out)); ) free_list[nfree++] = b;   // what the output handed back
        uint64_t w = pace_wait(&pace, now_ns(), g_frame_ns, NBUF - nfree);
        if (w) { dm_pcmout_wait(out, (int64_t)w); continue; }   // until the deadline, or earlier when the output hands one back
        while (!nfree) {   // the output holds all of them: wait for one (silent sink / sound card)
            gbuf *b = dm_pcmout_released(out);
            if (b) { free_list[nfree++] = b; break; }
            dm_pcmout_wait(out, 5000000);   // until the output hands one back (5 ms: g_open is checked again)
            if (!atomic_load(&g_open)) break;
        }
        if (!nfree) break;
        const int16_t *src; int got;
        uint32_t h = atomic_load(&g_cq_head);
        if (h != atomic_load(&g_cq_tail)) { src = g_cq[h % CQ]; got = g_cq_n[h % CQ]; atomic_store(&g_cq_head, h + 1); }   // queued catch-up output first
        else { g_frame_n = 0; exec(g_handle[0], g_handle[1]); src = g_frame; got = g_frame_n; }   // SDK: commands → CPU processing (output taken in the takeover) → renderer event
        int n = got ? got : 240;
        gbuf *b = free_list[--nfree];
        if (got) memcpy(b->ptr, src, (size_t)n * 4); else memset(b->ptr, 0, (size_t)n * 4);
        b->size = (uint64_t)n * 4;
        if (!dm_pcmout_append(out, b)) free_list[nfree++] = b;
    }
    dm_pcmout_close(out);   // the output would otherwise keep its sound card callback or silent sink running and hold one of the AudioOut slots
}

HLE_FORCE_OPT("_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleEPNS_2os11SystemEventERKNS0_22AudioRendererParameterEPvm",
              "open renderer (with event): CPU rendering + manual execution, host render thread", Result, h_ar_Open, void **handle, void *ev, const uint8_t *param, void *work, size_t sz) {
    static Result (*orig)(void **, void *, const uint8_t *, void *, size_t);
    if (!orig) orig = gsym("_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleEPNS_2os11SystemEventERKNS0_22AudioRendererParameterEPvm");
    uint8_t p[P_SIZE]; cpu_manual(p, param);
    LOG("audren: OpenAudioRenderer rate %d, frame %d, mix buffers %d, submixes %d, voices %d, sinks %d, effects %d, splitters %d (device %d mode %d → CPU / manual)",
        *(int32_t *)param, *(int32_t *)(param + 4), *(int32_t *)(param + 8), *(int32_t *)(param + 0xc), *(int32_t *)(param + 0x10), *(int32_t *)(param + 0x14),
        *(int32_t *)(param + 0x18), *(int32_t *)(param + 0x24), *(int32_t *)(param + P_DEVICE), *(int32_t *)(param + P_MODE));
    Result r = orig(handle, ev, p, work, sz);
    if (r) { LOG("[red] audren: SDK OpenAudioRenderer failed %#x", r); return r; }
    if (atomic_load(&g_open)) { LOG("[red] audren: a second renderer (the host drives only one)"); return r; }
    int rate = *(int32_t *)param, fr = *(int32_t *)(param + 4);
    g_frame_ns = rate > 0 && fr > 0 && fr <= FRAME_MAX ? (uint64_t)fr * 1000000000ull / (uint64_t)rate : 5000000;
    g_handle[0] = handle[0]; g_handle[1] = handle[1]; atomic_store(&g_open, 1);
    g_render_tt = dm_guest_thread_spawn(render_main, NULL, 9, 0x10000);
    return r;
}
// The render thread is joined (nxnative 8530042, MIT): it used to be given 20 ms, and a round still running (or a wait on the output) could outlast it
// into the SDK's teardown; it now ends its round, closes its output and exits before the SDK closes the renderer
HLE_FORCE_OPT("_ZN2nn5audio18CloseAudioRendererENS0_19AudioRendererHandleE", "close renderer: stop the host render thread, then the SDK", void, h_ar_Close, void *obj, void *state) {
    static void (*orig)(void *, void *); if (!orig) orig = gsym("_ZN2nn5audio18CloseAudioRendererENS0_19AudioRendererHandleE");
    if (obj == g_handle[0]) { atomic_store(&g_open, 0); if (g_render_tt) { dm_guest_thread_join(g_render_tt); g_render_tt = NULL; } }
    orig(obj, state);
}

// DeviceSink input mix buffers → int16 stereo. The command ID is 0x13 in SDK 16 and 0x14 in SDK 20 (written at sdk+0x233f44); SDK 8 has 0x10. The
// layout is the same: 0x148 bytes, +0x20 device name ("MainAudioOut"), +0x128 upsample buffer, +0x130 input count, +0x134 input indices. When the
// first full command list has no command with the expected ID, its only 0x148-byte command with 1 to 6 inputs is taken as the sink.
// The list's n bytes are read field by field without typed loads (a command of an odd size leaves the next one unaligned) and each command must lie
// within them: one running past the end ends the walk (seedds/nxnative ce99604, MIT; plan item 296)
static int g_sink_id = 0x13;
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static int16_t rd16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
static int cmd_fits(const uint8_t *c, const uint8_t *end) {   // a command header there and its size within the list
#ifndef DM_TEST_AUDREN_UNBOUNDED   // negative control of t_audren only: the extent not checked, as before
    if (c > end || (size_t)(end - c) < 8) return 0;
    int16_t sz = rd16(c + 6); return rd32(c) == 0xcafebabeu && sz >= 8 && (size_t)sz <= (size_t)(end - c);
#else
    (void)end; return rd32(c) == 0xcafebabeu && rd16(c + 6) > 0;
#endif
}
static int capture(const uint8_t *cl, size_t n, int16_t *dst) {
    if (n < 0x20) return 0;
    const uint8_t *end = cl + n;
    uint32_t ncmd = rd32(cl + 4), samples = rd32(cl + 0x14), rate = rd32(cl + 0x18);
    const int32_t *mix; memcpy(&mix, cl + 8, sizeof mix);
    static int logged, hist_logged;
    if (!hist_logged && ncmd > 4) {   // first full command list: command ID histogram, and the sink ID check
        hist_logged = 1;
        int hist[64] = {0}, cand = -1, ncand = 0; const uint8_t *q = cl + 0x20; char line[512]; size_t o = 0;
        for (uint32_t i = 0; i < ncmd && cmd_fits(q, end); i++) {
            int16_t sz = rd16(q + 6); hist[q[5] & 63]++;
            if (sz == 0x148 && rd32(q + 0x130) - 1 < 6) { cand = q[5]; ncand++; }
            q += sz;
        }
        for (int k = 0; k < 64; k++) if (hist[k]) o += snprintf(line + o, sizeof line - o, " %d×%d", k, hist[k]);
        LOG("audren: first command list has %u commands, by ID:%s", ncmd, line);
        if (!hist[g_sink_id & 63] && ncand == 1) { LOG("audren: no command %#x; the only 0x148-byte command is %#x, taken as DeviceSink", g_sink_id, cand); g_sink_id = cand; }
        else if (!hist[g_sink_id & 63]) LOG("[red] audren: no DeviceSink (command %#x) in the list, %d commands of 0x148 bytes: the game's sound cannot come out", g_sink_id, ncand);
    }
    const uint8_t *c = cl + 0x20;
    for (uint32_t i = 0; i < ncmd; i++) {
        if (!cmd_fits(c, end)) { if (!logged++) LOG("[red] audren: command %u malformed or past the list's end (offset %#zx of %#zx)", i, (size_t)(c - cl), n); return 0; }
        int16_t size = rd16(c + 6);
        if (c[5] == g_sink_id && c[4]) {
            if (size < 0x134 + 12) { if (!logged++) LOG("[red] audren: DeviceSink command %u of %d bytes, too short", i, size); return 0; }
            uint32_t nin = rd32(c + 0x130); int16_t in[6]; for (int j = 0; j < 6; j++) in[j] = rd16(c + 0x134 + 2 * j);
            const int32_t *up; memcpy(&up, c + 0x128, sizeof up);
            if (!logged++) LOG("audren: DeviceSink %u inputs (%d %d ...) rate %u frame %u mix buffers %p upsample buffer %p", nin, nin ? in[0] : -1, nin > 1 ? in[1] : -1, rate, samples, (void *)mix, (void *)up);
            if (!nin || samples > FRAME_MAX) return 0;
            // Inputs: the mix buffers at 48 kHz; when rendering at 32 kHz DeviceSink reads the upsampled buffer (inputs in order)
            const int32_t *ch[6]; uint32_t k = nin > 6 ? 6 : nin;
            for (uint32_t j = 0; j < k; j++) ch[j] = up && rate != 48000 ? up + (size_t)j * samples : mix + (size_t)in[j] * samples;
            // Channels front left, front right, center, LFE, back left, back right; 6 → 2 with the system's default downmix (front 1.0, center 0.707,
            // LFE 0.251, back 0.707, Q15)
            enum { CF = 32768, CC = 23167, CL = 8224, CB = 23167 };
            uint64_t loud = 0;
            for (uint32_t s = 0; s < samples; s++) {
                int32_t l, r;
                if (k >= 6) { int64_t c2 = (int64_t)ch[2][s] * CC + (int64_t)ch[3][s] * CL;
                              l = (int32_t)(((int64_t)ch[0][s] * CF + c2 + (int64_t)ch[4][s] * CB) >> 15); r = (int32_t)(((int64_t)ch[1][s] * CF + c2 + (int64_t)ch[5][s] * CB) >> 15); }
                else { l = ch[0][s]; r = k > 1 ? ch[1][s] : l; }
                l = l > 32767 ? 32767 : l < -32768 ? -32768 : l; r = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
                dst[2 * s] = (int16_t)l; dst[2 * s + 1] = (int16_t)r; loud |= (uint64_t)(l | r);
            }
            atomic_fetch_add(&g_frames, 1); if (loud) atomic_fetch_add(&g_nonsilent, 1);
            uint64_t f = atomic_load(&g_frames); if (f == 200 || f % 12000 == 0) LOG("audren: %llu frames rendered, %llu with sound", (unsigned long long)f, (unsigned long long)atomic_load(&g_nonsilent));
            return (int)samples;
        }
        c += size;
    }
    return 0;
}
// Render one frame (commands + CPU processing) into dst (stereo int16, *nout samples). The render thread and the catch-up path share one command
// buffer, so a host lock serializes them.
static pthread_mutex_t g_render_mu = PTHREAD_MUTEX_INITIALIZER;
static _Atomic uint64_t g_updates, g_renders, g_catchups;
// SDK 20's AudioRenderSystem has the mode / device / command buffer members inlined (sdk+0x22a70c ExecuteAudioRendererRendering: +0x48 rendering device,
// +0x4c execution mode, both must be 1; +0xf8 / +0x100 command buffer start / length), GenerateCommand takes and returns a Span (x0 start, x1 length),
// and UpdateImpl's mutex is at +0x2d0 (+0x1f0 in SDK 16). The layout is told by whether the Span version of GenerateCommand exists.
typedef struct { uint8_t *p; size_t n; } span_t;
static Result render_once(uint8_t *sys, int16_t *dst, int *nout) {
    static int (*mode)(void *), (*device)(void *); static uint8_t *(*cbuf)(void *); static size_t (*cbsz)(void *), (*gen)(void *, void *, size_t);
    static span_t (*gen20)(void *, void *, size_t); static size_t mu_off;
    static void (*tmax)(void *, uint32_t), (*setup)(void *, const void *, size_t), (*process)(void *), (*lock)(void *), (*unlock)(void *);
    if (!process) {
        lock = gsym("_ZN2nn2os12SdkMutexType4LockEv"); unlock = gsym("_ZN2nn2os12SdkMutexType6UnlockEv");
        gen20 = guest_sym("_ZN2nn5audio6server17AudioRenderSystem15GenerateCommandENS_4SpanISt4byteEE");
        if (!gen20) {
            mode = gsym("_ZNK2nn5audio6server17AudioRenderSystem16GetExecutionModeEv"); device = gsym("_ZNK2nn5audio6server17AudioRenderSystem18GetRenderingDeviceEv");
            cbuf = gsym("_ZNK2nn5audio6server17AudioRenderSystem26GetCommandBufferWorkBufferEv"); cbsz = gsym("_ZNK2nn5audio6server17AudioRenderSystem30GetCommandBufferWorkBufferSizeEv");
            gen = gsym("_ZN2nn5audio6server17AudioRenderSystem15GenerateCommandEPvm");
        }
        mu_off = gen20 ? 0x2d0 : 0x1f0; g_sink_id = gen20 ? 0x14 : 0x13;
        LOG("audren: AudioRenderSystem with the SDK %s layout (update lock +%#zx)", gen20 ? "20" : "16", mu_off);
        tmax = gsym("_ZN2nn5audio3dsp20CommandListProcessor17SetProcessTimeMaxEj"); setup = gsym("_ZN2nn5audio3dsp20CommandListProcessor5SetupEPKvm");
        process = gsym("_ZN2nn5audio3dsp20CommandListProcessor7ProcessEv");
    }
    int md = gen20 ? *(int32_t *)(sys + 0x4c) : mode(sys), dv = gen20 ? *(int32_t *)(sys + 0x48) : device(sys);
    if (md != 1 || dv != 1) return 0x40299;   // as the original: manual rendering needs CPU + manual mode
    pthread_mutex_lock(&g_render_mu);
    uint64_t t0 = now_ns();
    // Command generation excludes the game thread's RequestUpdate (AudioRenderSystem::Update holds the mutex at mu_off), as the original system thread
    // generates under the same lock; processing (the CPU DSP) runs outside it, as in the original
    uint8_t *cl; size_t n;
    lock(sys + mu_off);
    if (gen20) { span_t sp = gen20(sys, *(uint8_t **)(sys + 0xf8), *(size_t *)(sys + 0x100)); cl = sp.p; n = sp.n; }
    else { cl = cbuf(sys); n = gen(sys, cl, cbsz(sys)); }
    unlock(sys + mu_off);
    atomic_fetch_add(&g_renders, 1);
    uint64_t proc[16] __attribute__((aligned(16))) = {0};   // CommandListProcessor (0x58 bytes)
    tmax(proc, 0xffffffffu); setup(proc, cl, n); process(proc);
    if (dst) *nout = capture(cl, n, dst);
    // render time per frame (budget 5 ms): mean / max every 2000 frames; and the gaps between frame starts (the renderer event's pacing: the real
    // renderer makes one every 5 ms; SkyRecomp 0620de9 found a sound card callback handing back several buffers at once, 53 of 79 gaps under 1 ms)
    static uint64_t sum, mx, cnt, last, gaps[5]; uint64_t d = now_ns() - t0; sum += d; if (d > mx) mx = d;
    if (last) { uint64_t g = t0 - last; gaps[g < 1000000 ? 0 : g < 3000000 ? 1 : g < 7000000 ? 2 : g < 12000000 ? 3 : 4]++; } last = t0;
    if (++cnt == 2000) { LOG("audren: render time mean %.2f ms / max %.2f ms (5 ms budget per frame, last 2000 frames); catch-up frames so far %llu; gaps between frames: "
                             "< 1 ms %llu, 1-3 %llu, 3-7 %llu, 7-12 %llu, > 12 ms %llu", sum / 2000 / 1e6, mx / 1e6, (unsigned long long)atomic_load(&g_catchups),
                             gaps[0], gaps[1], gaps[2], gaps[3], gaps[4]); sum = mx = cnt = 0; memset(gaps, 0, sizeof gaps); }
    pthread_mutex_unlock(&g_render_mu);
    return RES_OK;
}
HLE_FORCE_OPT("_ZN2nn5audio6server24AudioRendererManagerImpl17AudioRendererImpl29ExecuteAudioRendererRenderingEv", "render one frame: SDK commands + CPU processing, DeviceSink output to the host",
              Result, h_ar_ImplExec, uint8_t *impl) { return render_once(impl + 0x60, g_frame, &g_frame_n); }   // the AudioRenderSystem is at +0x60 in AudioRendererImpl
// SDK 20: the public ExecuteAudioRendererRendering (sdk+0x221e90) reaches the AudioRendererImpl version through the vtable (sdk+0x22ca88: x0 += 0x60 and a
// jump through the PLT to here), so the takeover above is never reached. The original generates commands without the update lock; with catch-up frames
// rendered from RequestUpdate, both sides wrote the same command buffer and CommandListProcessor::Process read a half-written list (nxnative, BotW).
HLE_FORCE_OPT("_ZN2nn5audio6server17AudioRenderSystem29ExecuteAudioRendererRenderingEv", "render one frame (SDK 20, through the PLT): same, with the host's lock",
              Result, h_ars_Exec, uint8_t *sys) { return render_once(sys, g_frame, &g_frame_n); }

// At least one rendered frame between two RequestUpdates: an effect's "needs initialization" is set only by the update in which the game flags it as
// new, and the next update overwrites it; without commands in between the DSP never receives the initialization (a null pointer later). The real renderer
// produces a frame every 5 ms on its own; at startup a game updates twice in a row. When nothing was rendered since the last update, one catch-up frame
// is rendered on this thread first; the render thread plays it in place of its next render.
HLE_FORCE_OPT("_ZN2nn5audio6server17AudioRenderSystem6UpdateEPvmS3_mPKvm", "renderer update: one catch-up frame first when none was rendered since the last update",
              Result, h_ars_Update, uint8_t *sys, void *o, size_t on, void *p, size_t pn, const void *in, size_t inn) {
    static Result (*orig)(void *, void *, size_t, void *, size_t, const void *, size_t); if (!orig) orig = gsym("_ZN2nn5audio6server17AudioRenderSystem6UpdateEPvmS3_mPKvm");
    static uint64_t last_r = ~0ull;
    uint64_t u = atomic_fetch_add(&g_updates, 1) + 1, r = atomic_load(&g_renders);
    if (last_r != ~0ull && r == last_r) {
        uint64_t c = atomic_fetch_add(&g_catchups, 1) + 1;
        if (c <= 3) LOG("audren: no frame rendered before update %llu, one catch-up frame first (%llu so far)", (unsigned long long)u, (unsigned long long)c);
        uint32_t t = atomic_load(&g_cq_tail);
        if (t - atomic_load(&g_cq_head) < CQ) { render_once(sys, g_cq[t % CQ], &g_cq_n[t % CQ]); atomic_store(&g_cq_tail, t + 1); }
        else render_once(sys, NULL, NULL);   // queue full (should not happen): rendered, output dropped
    }
    Result res = orig(sys, o, on, p, pn, in, inn);
    last_r = atomic_load(&g_renders);
    return res;
}

// Output device: one built-in speaker, stereo (the audio device service would go through IPC; the host answers directly)
enum { DEVNAME = 0x100 };
static void out_devname(char *out) { memset(out, 0, DEVNAME); strlcpy(out, "AudioBuiltInSpeakerOutput", DEVNAME); }
HLE_FORCE_OPT("_ZN2nn5audio25ListAudioOutputDeviceNameEPNS0_15AudioDeviceNameEi", "output devices: one built-in speaker", int, h_ad_List, char *names, int n) { if (n <= 0 || !names) return 0; out_devname(names); return 1; }
HLE_FORCE_OPT("_ZN2nn5audio30GetActiveAudioOutputDeviceNameEPNS0_15AudioDeviceNameE", "active output device: built-in speaker", void, h_ad_Active, char *name) { out_devname(name); }
HLE_FORCE_OPT("_ZN2nn5audio41GetActiveAudioDeviceChannelCountForOutputEv", "output channels: 2", int, h_ad_Channels, void) { return 2; }
HLE_FORCE_OPT("_ZN2nn5audio26SetAudioDeviceOutputVolumeEPKNS0_15AudioDeviceNameEf", "device volume: ignored", void, h_ad_SetVol, const char *name, float v) { (void)name; (void)v; }
// It returns a Result (the SDK reference; plan item 288, after seedds/nxnative 971d336, MIT): as a void function it left x0 as it came, the event's
// address, which the caller took for a failed Result. -DDM_TEST_ADNOTIFY_VOID answers that address the same way
HLE_FORCE_OPT("_ZN2nn5audio39AcquireAudioDeviceNotificationForOutputEPNS_2os11SystemEventE", "output device change notification: bound, never signalled", Result, h_ad_Notify, void *ev) {
    hle_bind_system_event(ev, 0);
#ifdef DM_TEST_ADNOTIFY_VOID
    return (Result)(uintptr_t)ev;
#else
    return RES_OK;
#endif
}
