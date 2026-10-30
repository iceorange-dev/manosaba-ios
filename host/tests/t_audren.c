// Closing the audio renderer (hle_audren.c h_ar_Close, nxnative 8530042): the host render thread has ended its round, closed its PCM output and exited
// before the SDK's CloseAudioRenderer runs, even when a round takes 30 ms. The close used to give the thread 20 ms: the SDK tore the renderer down
// under a round still running, and the output was never closed (it kept its sink and one of the AudioOut slots). SDK functions, guest threads and the
// PCM output are stand-ins here. AcquireAudioDeviceNotificationForOutput's Result (plan item 288); answering the event's address as a void function
// leaves it (-DDM_TEST_ADNOTIFY_VOID): 1 fails. The command list's walk (capture, plan item 296; nxnative ce99604): a command of an odd size before
// the DeviceSink (its header then unaligned) and the sink's two inputs mixed out; a command whose size runs past the list's end, the list ending at
// an inaccessible page: the walk stops there, reading nothing past it (-DDM_TEST_AUDREN_UNBOUNDED, the extent not checked: 1 fails, the child
// faults).
#include "../hle/hle_audren.c"
#include <pthread.h>
#include <sys/mman.h>
#include <sys/wait.h>
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
    // the command list's walk: header 0x20 bytes, a command of 0x13 bytes, then the DeviceSink (0x148 bytes, 2 inputs: mix buffers 0 and 1)
    enum { SMP = 4 }; static int32_t mixb[2 * SMP] = {100, 200, 300, 400, -5, -6, -7, -8}; static uint8_t cl[0x20 + 0x13 + 0x148];
    uint32_t v32; int16_t v16; int32_t *mp = mixb;
    v32 = 2; memcpy(cl + 4, &v32, 4); memcpy(cl + 8, &mp, 8); v32 = SMP; memcpy(cl + 0x14, &v32, 4); v32 = 48000; memcpy(cl + 0x18, &v32, 4);
    uint8_t *c1 = cl + 0x20, *c2 = c1 + 0x13; v32 = 0xcafebabeu;
    memcpy(c1, &v32, 4); c1[4] = 1; c1[5] = 0x2; v16 = 0x13; memcpy(c1 + 6, &v16, 2);
    memcpy(c2, &v32, 4); c2[4] = 1; c2[5] = (uint8_t)g_sink_id; v16 = 0x148; memcpy(c2 + 6, &v16, 2); v32 = 2; memcpy(c2 + 0x130, &v32, 4);
    v16 = 0; memcpy(c2 + 0x134, &v16, 2); v16 = 1; memcpy(c2 + 0x136, &v16, 2);
    int16_t out[2 * SMP] = {0}; int got = capture(cl, sizeof cl, out);
    CHECK(got == SMP && out[0] == 100 && out[1] == -5 && out[6] == 400 && out[7] == -8, "a command of an odd size, then the DeviceSink unaligned: its two inputs mixed out (%d samples, %d %d)", got, out[0], out[1]);
    long pg = sysconf(_SC_PAGESIZE); uint8_t *two = mmap(NULL, (size_t)pg * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t *ed = two + pg - (0x20 + 0x10), *e1 = ed + 0x20;   // the list's last command at the page's end, claiming 0x200 bytes
    memcpy(ed, cl, 0x20); v32 = 1; memcpy(ed + 4, &v32, 4); v32 = 0xcafebabeu; memcpy(e1, &v32, 4); e1[4] = 1; e1[5] = (uint8_t)g_sink_id; v16 = 0x200; memcpy(e1 + 6, &v16, 2);
    mprotect(two + pg, (size_t)pg, PROT_NONE);
    pid_t pid = fork();
    if (!pid) { int16_t o2[2 * SMP]; _exit(capture(ed, 0x30, o2) == 0 ? 0 : 3); }
    int st = 0; waitpid(pid, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "a command whose size runs past the list's end (at an inaccessible page): the walk stops, nothing read past it");
    printf("t_audren: %d checks, %d failed\n", n, fail); return fail != 0;
}
