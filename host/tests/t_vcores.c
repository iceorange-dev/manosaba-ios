// Virtual cores (hle_thread.c core_take / core_migrate, plan item 306; MacNativeGames 3a48acf, MIT): thread A, allowed on cores 0-2, holds core 0
// and keeps running guest code; thread B, allowed on core 0 only (the main thread's mask), asks for a core while cores 1 and 2 are free. A moves to
// a free core and B gets core 0 at once (GetCurrentProcessorNumber 0), the move counted. No move, as before (-DDM_TEST_NO_MIGRATE): B still waits
// after 300 ms, 2 fail.
#include "../dm.h"
#include "../game.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>
#include <mach/mach_time.h>
int svc_thread(int num, uint64_t *regs);
uint32_t svc_thread_main_init(void);
uint64_t dm_core_migrations(void);
void (*dm_test_cv_after_release)(void);
struct dm_flags g_flag;
const dm_game_profile dm_game = { .id = "test", .virtual_cores = 1 };
pthread_key_t g_key18, g_keytls;
uint64_t g_t0_ns;
void crash_thread_init(void) {}
void guest_thread_init(void) {}
void dm_thread_enter(uint64_t entry, uint64_t arg, uint64_t sp, uint64_t *top) { (void)entry; (void)arg; (void)sp; (void)top; abort(); }
const uint64_t *dm_hstack_record(uint64_t tsd, uint64_t top) { (void)tsd; (void)top; return NULL; }
void *guest_sym(const char *name) { (void)name; return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
void dm_prof_add(int id, uint64_t ns, uint64_t bytes) { (void)id; (void)ns; (void)bytes; }
uint64_t now_ns(void) { static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb); return mach_absolute_time() * tb.numer / tb.denom; }
uint64_t guest_ns(void) { return now_ns(); }
int dm_detclock_enabled(void) { return 0; }
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static _Atomic int g_a_ready, g_b_got, g_done; static uint64_t g_b_core = 9, g_a_core = 9;
static void *thread_a(void *x) {   // takes core 0, may run on 0-2, then runs guest code without a system call
    (void)x; uint64_t r[32] = {0}; uint32_t h = svc_thread_main_init();
    r[0] = h; r[1] = (uint32_t)-1; r[2] = 7; svc_thread(0x0F, r);
    memset(r, 0, sizeof r); svc_thread(0x10, r); g_a_core = r[0];
    atomic_store(&g_a_ready, 1);
    while (!atomic_load(&g_done)) usleep(1000);   // (no system call: B's registration made a new thread key)
    return NULL;
}
static void *thread_b(void *x) {   // allowed on core 0 only: registers (takes a core) while A holds core 0
    (void)x; svc_thread_main_init(); uint64_t r[32] = {0}; svc_thread(0x10, r); g_b_core = r[0]; atomic_store(&g_b_got, 1);
    while (!atomic_load(&g_done)) usleep(1000);
    return NULL;
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    pthread_t a, b; pthread_create(&a, NULL, thread_a, NULL); while (!atomic_load(&g_a_ready)) usleep(1000);
    CHECK(g_a_core == 0, "A holds core 0 (cores 1 and 2 free)");
    pthread_create(&b, NULL, thread_b, NULL);
    for (int i = 0; i < 300 && !atomic_load(&g_b_got); i++) usleep(1000);
    int got = atomic_load(&g_b_got);
    CHECK(got && g_b_core == 0, "B, allowed on core 0 only, runs on core 0 within 300 ms while A keeps running [-DDM_TEST_NO_MIGRATE]");
    CHECK(dm_core_migrations() == 1, "... A moved to a free core: one move counted (%llu)", (unsigned long long)dm_core_migrations());
    printf("t_vcores: %d checks, %d failed\n", n, fail);
    if (!got) { fflush(stdout); _exit(1); }   // B still waits for core 0: no clean end for it
    atomic_store(&g_done, 1); pthread_join(a, NULL); pthread_join(b, NULL);
    return fail != 0;
}
