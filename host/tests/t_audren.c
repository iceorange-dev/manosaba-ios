// Closing the audio renderer (hle_audren.c h_ar_Close, nxnative 8530042): the host render thread has ended its round, closed its PCM output and exited
// before the SDK's CloseAudioRenderer runs, even when a round takes 30 ms. The close used to give the thread 20 ms: the SDK tore the renderer down
// under a round still running, and the output was never closed (it kept its sink and one of the AudioOut slots). SDK functions, guest threads and the
// PCM output are stand-ins here. AcquireAudioDeviceNotificationForOutput's Result (plan item 288); answering the event's address as a void function
// leaves it (-DDM_TEST_ADNOTIFY_VOID): 1 fails.
#include "../hle/hle_audren.c"
#include <pthread.h>
struct dm_flags g_flag;
void hle_bind_system_event(void *ev, int autoclear) { (void)ev; (void)autoclear; }
uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static _Atomic int g_in_exec, g_rounds, g_thread_done, g_out_closed, g_out_open;
static int g_close_in_exec = -1, g_close_thread_done = -1, g_close_out_closed = -1;
static Result fake_open(void **h, void *ev, const uint8_t *p, void *w, size_t sz) { (void)ev; (void)p; (void)w; (void)sz; h[0] = (void *)0x1000; h[1] = (void *)0x2000; return 0; }
static void fake_exec(void *a, void *b) { (void)a; (void)b; g_in_exec = 1; usleep(30000); g_rounds++; g_in_exec = 0; }
static void fake_close(void *a, void *b) { (void)a; (void)b; g_close_in_exec = g_in_exec; g_close_thread_done = g_thread_done; g_close_out_closed = g_out_closed; }
void *guest_sym(const char *name) {
    if (!strcmp(name, "_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleEPNS_2os11SystemEventERKNS0_22AudioRendererParameterEPvm")) return (void *)fake_open;
    if (!strcmp(name, "_ZN2nn5audio29ExecuteAudioRendererRenderingENS0_19AudioRendererHandleE")) return (void *)fake_exec;
    if (!strcmp(name, "_ZN2nn5audio18CloseAudioRendererENS0_19AudioRendererHandleE")) return (void *)fake_close;
    return NULL;
}
typedef struct { void (*fn)(void *); void *arg; pthread_t th; } gt;
static void *gt_main(void *p) { gt *t = p; t->fn(t->arg); g_thread_done = 1; return NULL; }
void *dm_guest_thread_spawn(void (*fn)(void *), void *arg, int prio, size_t stack_size) { (void)prio; (void)stack_size; gt *t = calloc(1, sizeof *t); t->fn = fn; t->arg = arg; pthread_create(&t->th, NULL, gt_main, t); return t; }
void dm_guest_thread_join(void *tt) { gt *t = tt; pthread_join(t->th, NULL); free(t); }
static void *g_ring[8]; static _Atomic unsigned g_rh, g_rt;   // the stand-in output hands every appended buffer straight back
void *dm_pcmout_open(int ch) { (void)ch; g_out_open = 1; return (void *)0x3000; }
void dm_pcmout_close(void *k) { (void)k; g_out_closed = 1; }
void dm_pcmout_start(void *k) { (void)k; }
int dm_pcmout_append(void *k, void *b) { (void)k; g_ring[g_rt++ % 8] = b; return 1; }
void *dm_pcmout_released(void *k) { (void)k; return g_rh != g_rt ? g_ring[g_rh++ % 8] : NULL; }
int dm_pcmout_wait(void *k, int64_t ns) { (void)k; usleep((useconds_t)(ns / 1000)); return 0; }
int main(void) {
    void *h[2]; uint8_t param[0x40] = {0};
    CHECK(h_ar_Open(h, NULL, param, NULL, 0) == 0, "the renderer opens and its render thread starts");
    while (g_rounds < 3) usleep(1000);
    while (!g_in_exec) usleep(100);   // close in the middle of a 30 ms round
    h_ar_Close(h[0], h[1]);
    CHECK(g_close_in_exec == 0, "no round is running when the SDK closes the renderer");
    CHECK(g_close_thread_done == 1, "the render thread has exited by then");
    CHECK(g_close_out_closed == 1, "and has closed its PCM output");
    // AcquireAudioDeviceNotificationForOutput returns a Result (plan item 288): success, the event bound
    uint8_t ev[0x40] = {0};
    CHECK(h_ad_Notify(ev) == 0, "AcquireAudioDeviceNotificationForOutput: success, as a Result");
    printf("t_audren: %d checks, %d failed\n", n, fail); return fail != 0;
}
