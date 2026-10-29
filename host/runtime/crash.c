#include <TargetConditionals.h>
// Crash state: prints the faulting address, pc, lr (converted to module-relative offsets) and the registers; dispatches page fault hooks.
// Only signal handling itself lives here: diagnostic features (probes / UI messages / sampling) are in diag.c, and this file does not depend on them (the graphics unit tests link only this file).
#include "dm.h"
#include "il2cpp_obj.h"
#include <signal.h>
#include <dlfcn.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include <sys/mman.h>
#include <libkern/OSCacheControl.h>
#include "plat.h"
#include "uc.h"
#include <stdatomic.h>
#include <time.h>

static _Atomic uint64_t g_watch_last_ns;
static _Atomic uint64_t g_watch_count;
static _Atomic uint32_t g_watch_tick;
static _Atomic int g_watch_pregame = 1;
static _Atomic int g_watch_stage_idx;
static char g_watch_stage[2][256] = {"boot", "boot"};
static unsigned g_watch_stall;
static _Atomic uint64_t g_present_ns;   // the last present (0 before the first): the frames-stopped watch below
static unsigned g_freeze_s;
static void (*g_dump_hook)(void);
__attribute__((weak)) void mtl_crash_note(void) {}   // nvn_mtl.m provides the real one; the host's unit tests link crash.c without the graphics layer
static void (*g_wait_dump)(void);
__attribute__((weak)) int guest_clock_frozen_for(int reason) { (void)reason; return 0; }   // hle_os.c; the unit tests link crash.c without it

static uint64_t watch_now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
uint64_t crash_watch_progress_count(void) { return atomic_load(&g_watch_count); }
void crash_watch_progress(uint32_t tick, const char *stage) {
    int next = 1 - atomic_load(&g_watch_stage_idx);
    if (stage && *stage) snprintf(g_watch_stage[next], sizeof g_watch_stage[next], "%s", stage);
    atomic_store(&g_watch_tick, tick);
    atomic_store(&g_watch_stage_idx, next);
    atomic_fetch_add(&g_watch_count, 1);
    atomic_store(&g_watch_last_ns, watch_now_ns());
}
void crash_watch_frame(void) { atomic_store(&g_present_ns, watch_now_ns()); if (atomic_load(&g_watch_pregame)) crash_watch_progress(0, "frame presented"); }
void crash_watch_pregame_done(void) { atomic_store(&g_watch_pregame, 0); }
void crash_watch_pregame_again(void) { atomic_store(&g_watch_pregame, 1); }   // Back from a run to the front end: count progress by presented frames again (diag.c)
void crash_dump_now(const char *why);
static void *watchdog_main(void *arg) {
    (void)arg;
    for (;;) {
        usleep(250000);
        // Read the last progress time first, then now: in the other order, progress recorded between the two reads makes last newer than now, and
        // now − last underflows into "no progress for 18446744073 s" — a false hang and exit. This way last ≤ now; a clock that goes back is ignored
        uint64_t last = atomic_load(&g_watch_last_ns), now = watch_now_ns();
        if (last > now) continue;
#ifndef DM_TEST_WATCH_NO_BG   // negative control of t_watchdog only
        // In the background (guest clock frozen for reason 1: the iPhone's scene, the Mac's DM_TEST_BG) no frame comes by design: the time away is
        // not "no progress" (a DM_TEST_BG run was killed 15 s into its background). Counting starts again at the return
        if (guest_clock_frozen_for(1)) { atomic_store(&g_watch_last_ns, now); if (atomic_load(&g_present_ns)) atomic_store(&g_present_ns, now); continue; }
#endif
        // Frames stopped (plan item 107; the idea from justr4mzi's mush8 fork 979317e, GPL, nothing taken but the idea): DM_FREEZE_DUMP seconds (6)
        // without a present once the game has presented, in the foreground and with no system keyboard up (the game waits for it by design): one
        // dump of what every thread waits on per freeze, and a line when frames come back; the game goes on. On in the product (the stall watchdog
        // below exits, so only tests turn it on); DM_FREEZE_DUMP=0 turns it off
        if (g_freeze_s) { static uint64_t dumped_for; static int freezes; uint64_t pr = atomic_load(&g_present_ns);
            if (pr && pr <= now && guest_clock_frozen_for(2)) atomic_store(&g_present_ns, pr = now);
            if (pr && pr <= now && now - pr >= (uint64_t)g_freeze_s * 1000000000ull && dumped_for != pr) {
                dumped_for = pr; freezes++;
                LOG("[red] frames stopped: no present for %.1f s (freeze %d of this run); what each thread waits on follows, the game goes on", (now - pr) / 1e9, freezes);
                crash_dump_now("frames stopped");
            } else if (dumped_for && pr != dumped_for && pr > dumped_for) { LOG("frames came back after %.1f s", (pr - dumped_for) / 1e9); dumped_for = 0; } }
        // Second-scale stall (progress has stopped for more than 1 second but has not yet reached the kill threshold): do not exit, dump what each thread is waiting on. When hitting a case of "only one frame every 2 seconds", the 15-second threshold cannot catch it and the log has nothing on what it is waiting for.
        // Dump only after entering a run; at most 3 dumps per process, at least 10 seconds between two dumps (the loading screen and the pause menu also stop ticks, do not let them flood the log)
        { static int dumps; static uint64_t last_dump, dumped_for;
          if (g_watch_stall && !atomic_load(&g_watch_pregame) && atomic_load(&g_watch_tick) > 0 && now - last >= 1000000000ull && now - last < (uint64_t)g_watch_stall * 1000000000ull
              && dumps < 3 && dumped_for != last && (!last_dump || now - last_dump >= 10000000000ull)) { dumps++; last_dump = now; dumped_for = last; crash_dump_now("no progress for over 1 s"); } }
        if (g_watch_stall && now - last >= (uint64_t)g_watch_stall * 1000000000ull) {
            uint64_t sec = (now - last) / 1000000000ull;
            int idx = atomic_load(&g_watch_stage_idx);
            LOG("[red] watchdog: no progress for %llu s, stuck at tick %u / stage %s", (unsigned long long)sec,
                atomic_load(&g_watch_tick), g_watch_stage[idx]);
            if (g_dump_hook) g_dump_hook();
            if (g_wait_dump) g_wait_dump();   // State of all blocking wait points on the graphics side (registered when nvn_mtl.m initializes)
#if TARGET_OS_OSX
            { const char *d = getenv("DM_WATCH_SAMPLE");   // Mac diagnostics: when stuck, sample itself for 1 second and write the thread stacks to this path
              if (d) { char c[1200]; snprintf(c, sizeof c, "/usr/bin/sample %d 1 -f '%s' >/dev/null 2>&1", (int)getpid(), d); if (system(c)) LOG("watchdog: sampling failed"); else LOG("watchdog: thread stacks written to %s", d); } }
#endif
            _exit(128 + SIGALRM);
        }
    }
    return NULL;
}
void crash_off(const char *tag, uint64_t a) {   // Also used by diag.c
#define off crash_off

    module_t *m = module_of((void *)a);
    Dl_info di;
    if (m) fprintf(stderr, " %s%s%s+%#llx", tag, *tag ? "=" : "", m->name, a - (uint64_t)m->base);
    else if (dladdr((void *)a, &di) && di.dli_sname) fprintf(stderr, " %s%s%s+%#lx", tag, *tag ? "=" : "", di.dli_sname, (unsigned long)(a - (uint64_t)di.dli_saddr));
    else fprintf(stderr, " %s%s%#llx", tag, *tag ? "=" : "", a);
}
// Frame pointer backtrace: x29 chain [fp]=previous frame's fp, [fp+8]=return address; only 8-byte aligned, increasing frames are walked, but for one step
// down or up from a host stack to the guest's (dm_fp_step_ok: a host call's record, hstack.S). Each frame is read with dm_mem_read (plat.h; plan item
// 125; totk-mobile does so): it runs in the crash handler, and a corrupt x29 (a memory-corruption crash, as 72) dereferenced there faulted again and
// lost the rest of the report. An unreadable frame ends the walk with a note
void dm_backtrace(uint64_t fp) {
    fprintf(stderr, "[dm] bt:");
    int jumps = 0;
    for (int i = 0; i < 32 && fp && !(fp & 7); i++) {
        uint64_t fr[2];
#ifndef DM_TEST_BT_UNGUARDED   // negative control of t_crash_bt only: read directly, as before
        if (!dm_mem_read(fr, fp, sizeof fr)) { fprintf(stderr, " (frame %#llx unreadable)", fp); break; }
#else
        fr[0] = ((uint64_t *)fp)[0]; fr[1] = ((uint64_t *)fp)[1];
#endif
        uint64_t nfp = fr[0], ra = fr[1];
        off("", ra);
        if (!dm_fp_step_ok(fp, nfp, &jumps)) break;
        fp = nfp;
    }
    fputc('\n', stderr);
}
// ---------- Page fault hooks (write-protect dirty page tracking) ----------
// Called in registration order before on_crash prints the crash state; returning 1 = handled (return directly and re-execute the faulting instruction); the crash path is taken only when all return 0.
// Hooks run in signal handler context: only table lookups / atomic operations / mprotect are allowed, no memory allocation and no locking.
#define MAX_FAULT_HOOK 4
static int (*g_fault_hook[MAX_FAULT_HOOK])(void *addr, int is_write, void *uc);
static void (*g_write_prep[MAX_FAULT_HOOK])(void *addr, size_t len);
static int g_nfault_hook, g_nwrite_prep;
void dm_fault_hook_register(int (*fn)(void *addr, int is_write, void *uc)) {
    if (g_nfault_hook >= MAX_FAULT_HOOK) die("too many fault hooks");
    g_fault_hook[g_nfault_hook++] = fn;
}
static void (*g_res_report)(uint32_t tick, const char *why);   // Resource count callback (registered by the graphics layer, called by diag.c per tick / event)
void dm_res_report_register(void (*fn)(uint32_t tick, const char *why)) { g_res_report = fn; }
void (*dm_res_report_cb(void))(uint32_t, const char *) { return g_res_report; }
void dm_guest_write_prep_register(void (*fn)(void *addr, size_t len)) {
    if (g_nwrite_prep >= MAX_FAULT_HOOK) die("too many host write-prepare hooks");
    g_write_prep[g_nwrite_prep++] = fn;
}
// Called before the host itself writes guest memory (read() straight into a guest buffer, etc.): for a system call a write-protected page does not trigger a page fault but returns EFAULT
void dm_guest_write_prepare(void *addr, size_t len) { for (int i = 0; i < g_nwrite_prep; i++) g_write_prep[i](addr, len); }

// The diagnostics module (the probes in diag.c) and the thread state dump (hle_thread.c) each register themselves here: a unit test that links only this file does not depend on them
static int (*g_trap_hook)(ucontext_t *uc);
void crash_set_trap_hook(int (*fn)(ucontext_t *uc)) { g_trap_hook = fn; }
void crash_set_dump_hook(void (*fn)(void)) { g_dump_hook = fn; }
void crash_set_wait_dump(void (*fn)(void)) { g_wait_dump = fn; }
// Dump the current state on demand (without exiting): the iOS app has no no-progress watchdog, so when the smoke test fails it sends dump through DM_LOG_PORT to trigger this -- what each guest thread is waiting on + the graphics-side wait points, the same thing the watchdog prints
void crash_dump_now(const char *why) { fprintf(stderr, "[dm] state dump (%s): tick %u / stage %s\n", why, atomic_load(&g_watch_tick), g_watch_stage[atomic_load(&g_watch_stage_idx)]); if (g_dump_hook) g_dump_hook(); if (g_wait_dump) g_wait_dump(); }
// DM_CRASH_MEM=reg[+-off]:len[,...] (diagnostics): on a crash, also print guest memory at a register plus an offset, e.g. "x27-0x28:0x80,sp+0x130:0x40"
// (reg = x0..x30 or sp; len in bytes, up to 0x400); a leading * reads the pointer stored at the register first ("*x28+0x28:0x8": the 8 bytes at
// offset 0x28 of the object x28 points to). Unreadable addresses are reported as such (read through dm_mem_read, no second fault).
static void crash_mem(ucontext_t *uc) {
    const char *e = getenv("DM_CRASH_MEM"); if (!e) return;
    char spec[512]; strlcpy(spec, e, sizeof spec);
    for (char *save, *t = strtok_r(spec, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
        char *end, *p = t; uint64_t base; int r, deref = *p == '*';
        if (deref) p++;
        if (p[0] == 's' && p[1] == 'p') { base = UC_SP(uc); end = p + 2; }
        else if (p[0] == 'x' && (r = (int)strtol(p + 1, &end, 10)) >= 0 && r <= 30 && end != p + 1)
            base = r == 29 ? UC_FP(uc) : r == 30 ? UC_LR(uc) : UC_X(uc, r);
        else { fprintf(stderr, "[dm] DM_CRASH_MEM: cannot parse \"%s\"\n", t); continue; }
        if (deref) { uint64_t v = 0;
            if (!dm_mem_read(&v, base, 8)) { fprintf(stderr, "[dm] mem %s: pointer at %#llx not readable\n", t, base); continue; }
            base = v; }
        int64_t off = 0; if (*end == '+' || *end == '-') off = strtoll(end, &end, 0);
        size_t len = *end == ':' ? strtoul(end + 1, 0, 0) : 0x40; if (len > 0x400) len = 0x400;
        uint64_t a = base + (uint64_t)off; uint8_t b[0x400];
        if (!dm_mem_read(b, a, len)) { fprintf(stderr, "[dm] mem %s = %#llx: not readable\n", t, a); continue; }
        for (size_t k = 0; k < len; k += 32) {
            fprintf(stderr, "[dm] mem %s %#llx:", k ? "" : t, a + k);
            for (size_t j = k; j < k + 32 && j < len; j += 8) { uint64_t v = 0; memcpy(&v, b + j, 8 <= len - j ? 8 : len - j); fprintf(stderr, " %016llx", v); }
            fputc('\n', stderr);
        }
    }
}
// DM_CRASH_HEAP=reg[,reg...] (diagnostics): on a crash, the sead heap holding each register's address (x0..x30 or sp) and its use list (sead.c)
__attribute__((weak)) void sead_heap_of(uint64_t addr) { fprintf(stderr, "[dm] DM_CRASH_HEAP: %#llx (no sead.c in this build)\n", (unsigned long long)addr); }   // sead.c
static void crash_heap(ucontext_t *uc) {
    const char *e = getenv("DM_CRASH_HEAP"); if (!e) return;
    char spec[256]; strlcpy(spec, e, sizeof spec);
    for (char *save, *t = strtok_r(spec, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
        char *end; int r;
        if (!strcmp(t, "sp")) { fprintf(stderr, "[dm] heap of sp:\n"); sead_heap_of(UC_SP(uc)); }
        else if (t[0] == 'x' && (r = (int)strtol(t + 1, &end, 10)) >= 0 && r <= 30 && end != t + 1 && !*end) {
            fprintf(stderr, "[dm] heap of %s:\n", t);
            sead_heap_of(r == 29 ? UC_FP(uc) : r == 30 ? UC_LR(uc) : UC_X(uc, r)); }
        else fprintf(stderr, "[dm] DM_CRASH_HEAP: cannot parse \"%s\"\n", t);
    }
}
// IL2CPP class names of the objects the registers point to (plan item 141, il2cpp_obj.h), for a Unity game only (dm_engine_il2cpp, game.c): in
// another game a register can point at something that only looks like an object. Read with dm_mem_read: no second fault
__attribute__((weak)) int dm_engine_il2cpp(void) { return 0; }   // game.c; the unit tests link crash.c without it
static int crash_rd(uint64_t a, void *d, size_t n) { return dm_mem_read(d, a, n); }
void crash_objs(const uint64_t x[29]) {
    if (!dm_engine_il2cpp()) return;
    for (int i = 0; i < 29; i++) { char c[200]; if (il2cpp_class_of(x[i], crash_rd, c, sizeof c)) fprintf(stderr, "[dm] x%d → an IL2CPP %s\n", i, c); }
}
static void on_crash(int sig, siginfo_t *si, void *uc_) {
    ucontext_t *uc = uc_;
    if (sig == SIGTRAP && g_trap_hook && g_trap_hook(uc)) return;
    if (sig == SIGSEGV || sig == SIGBUS) {
        int is_write = (int)((uc_esr(uc) >> 6) & 1);   // The WnR bit of ESR
        for (int i = 0; i < g_nfault_hook; i++) if (g_fault_hook[i](si->si_addr, is_write, uc)) return;
    }
    // Another thread may hold the stderr lock (stuck, or stopped in the middle of a write): the fprintf below would then hang with it, the
    // process would neither exit nor say why, and the log would end mid-run with nothing. If the lock cannot be taken within 1 s, write a single
    // line with write() (pc / lr as module offsets) and exit. ftrylockfile succeeds when this thread holds the lock itself (file locks are
    // recursive). (From seedds/nxnative 03e7cfc, MIT.) The second is measured, not counted in sleeps: on a busy machine a 10 ms sleep takes longer
    // (GitHub's macOS runner: 100 of them took 5.4 s)
    uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW); int tries = 0; (void)t0; (void)tries;
    while (ftrylockfile(stderr)) {
#ifndef DM_TEST_CRASH_COUNTED_WAIT   // negative control of t_crash_stderr only: 100 sleeps, as before
        if (clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0 < 1000000000ull) { usleep(10000); continue; }
#else
        if (tries++ < 100) { usleep(10000); continue; }
#endif
        char b[256]; int n = snprintf(b, sizeof b, "[dm] CRASH sig=%d (stderr held by another thread for 1 s: no register dump)", sig);
        uint64_t at[2] = {UC_PC(uc), UC_LR(uc)};
        for (int k = 0; k < 2; k++) { module_t *m = module_of((void *)at[k]);
            n += m ? snprintf(b + n, sizeof b - n, " %s=%s+%#llx", k ? "lr" : "pc", m->name, at[k] - (uint64_t)m->base) : snprintf(b + n, sizeof b - n, " %s=%#llx", k ? "lr" : "pc", at[k]); }
        n += snprintf(b + n, sizeof b - n, "\n");
        if (write(2, b, (size_t)n) < 0) {}
        _exit(128 + sig);
    }
    funlockfile(stderr);
    fprintf(stderr, "[dm] CRASH sig=%d", sig);
    off("addr", (uint64_t)si->si_addr); off("pc", UC_PC(uc)); off("lr", UC_LR(uc));
    fprintf(stderr, " sp=%#llx\n", UC_SP(uc));
    for (int i = 0; i < 29; i++) fprintf(stderr, "x%d=%#llx%s", i, UC_X(uc, i), i % 6 == 5 ? "\n" : " ");
    fprintf(stderr, "\n");
    crash_objs((const uint64_t *)UC_XS(uc));
    crash_mem(uc);
    crash_heap(uc);
    dm_backtrace(UC_FP(uc));
    mtl_crash_note();   // the graphics layer's last copy handed to the driver (plain values recorded beforehand; no Objective-C here)
    if (g_dump_hook) g_dump_hook();
    _exit(128 + sig);
}
// The signal alternate stack is per thread: every guest thread must call this when it starts running (only then can the crash state be printed on the alternate stack on a stack overflow)
// The alternate stack is released when the thread exits: the pthread key destructor is called at thread exit (disable the alternate stack first, then free). The game frequently starts short-lived threads, and without releasing each one leaks 64 KiB.
static pthread_key_t g_altstack_key;
static pthread_once_t g_altstack_once = PTHREAD_ONCE_INIT;
static void altstack_free(void *p) { stack_t off = {.ss_flags = SS_DISABLE}; sigaltstack(&off, 0); free(p); }
static void altstack_key_init(void) { if (pthread_key_create(&g_altstack_key, altstack_free)) die("altstack key"); }
void crash_thread_init(void) {
    pthread_once(&g_altstack_once, altstack_key_init);
    if (pthread_getspecific(g_altstack_key)) return;                 // Repeated calls on the same thread do not allocate again
    stack_t ss = {.ss_sp = malloc(65536), .ss_size = 65536}; sigaltstack(&ss, 0);
    pthread_setspecific(g_altstack_key, ss.ss_sp);
}
void crash_install(void) {
    crash_thread_init();
    struct sigaction sa = {.sa_sigaction = on_crash, .sa_flags = SA_SIGINFO | SA_ONSTACK};
    sigaction(SIGALRM, &sa, 0);
    // Both watchdogs that exit are turned on only when a test explicitly sets DM_WATCH / DM_WATCH_STALL: the product (the player launches from the desktop, no environment variables) turns on neither.
    // The frames-stopped watch (watchdog_main), which only dumps, runs in the product too (DM_FREEZE_DUMP=0 turns it off).
    // Found by reading the code: iOS main.m did setenv DM_WATCH=30 by default → the player's launch was killed by SIGALRM at 30 s; the no-progress watchdog defaulted to 15 s → the pause menu (ticks stopped) was killed at 15 s
    const char *watch = getenv("DM_WATCH"), *stall = getenv("DM_WATCH_STALL"), *fz = getenv("DM_FREEZE_DUMP");
    if (watch) alarm(atoi(watch));
#ifndef DM_TEST_FREEZE_NONE   // negative control of t_freeze only: no frames-stopped watch, the thread only for the test watchdogs (as before)
    g_freeze_s = fz ? (unsigned)atoi(fz) : 6;
#else
    (void)fz;
#endif
    if (watch || stall) g_watch_stall = stall && atoi(stall) > 0 ? (unsigned)atoi(stall) : 15;
    if (watch || stall || g_freeze_s) {
        atomic_store(&g_watch_last_ns, watch_now_ns());
        pthread_t th; if (pthread_create(&th, NULL, watchdog_main, NULL)) die("cannot start the watchdog thread");
        pthread_detach(th);
    }
    sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0); sigaction(SIGILL, &sa, 0); sigaction(SIGTRAP, &sa, 0);
}
