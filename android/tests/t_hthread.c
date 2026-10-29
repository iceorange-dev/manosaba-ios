// android/plat_linux.c's host threads (host/plat.h dm_hthread_*), on the phone: a thread spins in assembly with known values in x19-x28 and v8-v15,
// counting in memory; it is stopped where it runs, its registers read, and it stays stopped (the count does not move) until resumed, a second
// suspension needing a second resume as Mach's do. Also its CPU time, that dm_hthreads_all lists it and holds its record past its exit. -DDM_TEST_STOP_NO_V (the vector registers
// not taken from the signal context): 1 fails.
#include "plat.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)

static _Atomic int g_ready; static volatile uint32_t g_stop; static volatile uint64_t g_count; static dm_hthread_t g_h;
extern char spin_begin[], spin_end[];
static void *spinner(void *a) {
    (void)a; g_h = dm_hthread_self(); atomic_store(&g_ready, 1);
    __asm__ volatile(
        "mov x19, #0x1900\n mov x20, #0x2000\n mov x21, #0x2100\n mov x22, #0x2200\n mov x23, #0x2300\n"
        "mov x24, #0x2400\n mov x25, #0x2500\n mov x26, #0x2600\n mov x27, #0x2700\n mov x28, #0x2800\n"
        "movi v8.16b, #0x88\n movi v9.16b, #0x99\n movi v10.16b, #0xa0\n movi v11.16b, #0xb1\n"
        "movi v12.16b, #0xc2\n movi v13.16b, #0xd3\n movi v14.16b, #0xe4\n movi v15.16b, #0xf5\n"
        ".globl spin_begin\n spin_begin:\n"
        "1: ldr x1, [%1]\n add x1, x1, #1\n str x1, [%1]\n ldr w0, [%0]\n cbz w0, 1b\n"
        ".globl spin_end\n spin_end:\n"
        :: "r"(&g_stop), "r"(&g_count)
        : "x0", "x1", "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "memory");
    return NULL;
}
static void ms(int k) { struct timespec t = {0, k * 1000000L}; nanosleep(&t, NULL); }
static int still(void) { uint64_t a = g_count; ms(30); return g_count == a; }   // the count does not move over 30 ms

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    pthread_t th; pthread_create(&th, NULL, spinner, NULL);
    while (!atomic_load(&g_ready)) ms(1);
    ms(20);
    CHECK(!still(), "the spinner counts while it runs");
    CHECK(dm_hthread_suspend(g_h) == 0 && still(), "suspended: it stops where it runs (count %llu, still)", (unsigned long long)g_count);
    dm_regs r; memset(&r, 0, sizeof r);
    int ok = dm_hthread_regs(g_h, &r, 1) == 0;
    int xs = 1; for (int i = 19; i <= 28; i++) xs &= r.x[i] == (uint64_t)((i / 10) << 12 | (i % 10) << 8);   // x19 0x1900 ... x28 0x2800
    CHECK(ok && xs, "x19-x28 as the spinner set them (x19 %#llx, x28 %#llx)", (unsigned long long)r.x[19], (unsigned long long)r.x[28]);
    CHECK(ok && r.pc >= (uint64_t)(uintptr_t)spin_begin && r.pc <= (uint64_t)(uintptr_t)spin_end, "pc in the spin loop (%#llx in %p-%p)", (unsigned long long)r.pc, (void *)spin_begin, (void *)spin_end);
    static const uint8_t vb[8] = {0x88, 0x99, 0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5}; int vs = r.has_v;
    for (int k = 0; k < 8 && vs; k++) for (int b = 0; b < 16; b++) vs &= r.v[8 + k][b] == vb[k];
    CHECK(vs, "v8-v15 as the spinner set them (has_v %d, v8 byte %#x)", r.has_v, r.v[8][0]);
    CHECK(dm_hthread_suspend(g_h) == 0 && dm_hthread_resume(g_h) == 0 && still(), "suspended twice and resumed once: still stopped");
    CHECK(dm_hthread_resume(g_h) == 0 && !still(), "resumed again: it runs");
    CHECK(dm_hthread_resume(g_h) != 0, "a resume of a running thread is refused");
    int64_t c0 = dm_hthread_cpu_us(g_h); ms(50); int64_t c1 = dm_hthread_cpu_us(g_h);
    CHECK(c0 >= 0 && c1 - c0 > 20000, "its CPU time advances as it spins (%lld us over 50 ms)", (long long)(c1 - c0));
    dm_hthread_t all[64]; int na = dm_hthreads_all(all, 64), seen = 0; for (int i = 0; i < na; i++) seen |= all[i] == g_h;
    CHECK(seen, "dm_hthreads_all lists it (%d threads)", na);
    g_stop = 1; pthread_join(th, NULL);
    CHECK(dm_hthread_suspend(g_h) != 0, "listed, then exited before the release: its record stays and a suspend is refused");
    dm_hthreads_release(all, na);
    na = dm_hthreads_all(all, 64); seen = 0; for (int i = 0; i < na; i++) seen |= all[i] == g_h;
    CHECK(!seen, "an exited thread is not listed (%d threads)", na); dm_hthreads_release(all, na);
    printf("t_hthread: %d checks, %d failed\n", n, fail); return fail != 0;
}
