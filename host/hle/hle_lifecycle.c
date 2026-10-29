// Host lifecycle: graceful exit (SIGTERM; on iOS the app termination callback calls dm_host_shutdown).
// Waits for a save in progress to commit and for the game to leave its exit-request section (quit_wait), pauses audio and suspends graphics
// (waits for in-flight NVN calls and drains GPU work), then _exit(0); 1.5 s after that wait it exits regardless.
// The default SIGTERM action would also exit quickly (code 143); this gives an orderly stop, exit code 0, and an entry for the iOS lifecycle.
#include "hle.h"
#include <TargetConditionals.h>
#include "plat.h"
#include <stdatomic.h>
#include <signal.h>
#include <unistd.h>
void hlecount_flush(void);
int dm_fs_save_pending(void);   // hle_fs.c: a save transaction written and not committed yet
int dm_oe_exit_section(void);   // hle_oe.c: the game is inside its exit-request section
__attribute__((weak)) void dm_exit_note(void) {}   // the iOS shell's (ios/Sources/main.m): an orderly exit clears its run marker (host/rundiag.h)
// Waits until no save is pending and the game is outside its exit-request section (manosaba brackets its saves with one: Enter, writes, commit,
// Leave), at most max_s; returns the seconds waited, *save / *sec = what still held then. Unit test tests/t_quitwait.c
static double quit_wait(double max_s, int *save, int *sec) {
    uint64_t t0 = clock_gettime_nsec_np(CLOCK_MONOTONIC), lim = (uint64_t)(max_s * 1e9);
    for (;;) {
        *save = dm_fs_save_pending();
#ifndef DM_TEST_QUIT_NO_SECTION   // negative control of t_quitwait only: the save alone, as before
        *sec = dm_oe_exit_section();
#else
        *sec = 0;
#endif
        if ((!*save && !*sec) || clock_gettime_nsec_np(CLOCK_MONOTONIC) - t0 >= lim) break;
        usleep(10000);
    }
    return (clock_gettime_nsec_np(CLOCK_MONOTONIC) - t0) / 1e9;
}
static void exit_late(void) { LOG("[red] exit did not finish within 1.5 s, exiting now"); _exit(0); }
void dm_host_shutdown(const char *why) {
    static _Atomic int once; if (atomic_exchange(&once, 1)) return;
    dm_exit_note();
    uint64_t t0 = now_ns(); LOG("%s received: exiting", why);
    // A save in progress (written, not committed) and the exit-request section are waited for first: up to 15 s on the Mac, 3 s on iOS, which ends
    // a terminating app soon after
#if TARGET_OS_OSX
    double max_s = 15;
#else
    double max_s = 3;
#endif
    int save = 0, sec = 0; double w = quit_wait(max_s, &save, &sec);
    if (w > 0.02) LOG("%s after waiting %.2f s for the save in progress to commit and the game's exit-request section", save ? "[red] exiting without the uncommitted save" :
                      sec ? "[red] exiting inside the game's exit-request section" : "exiting", w);
    dm_after_ms(1500, exit_late);
    hlecount_flush();                               // DM_HLECOUNT: _exit skips atexit, so write it here
    dm_audio_suspend(1);
    dm_gfx_suspend(1);
    LOG("exit done (%.3f s)", (now_ns() - t0) / 1e9);
    _exit(0);
}
static void on_term(void) { dm_host_shutdown("SIGTERM"); }
void dm_term_install(void) { dm_on_signal(SIGTERM, on_term); }   // on an ordinary thread, not in a handler (plat.h)
