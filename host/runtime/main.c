// Bootstrap: load the modules, initialize the SDK runtime in the order of nn::init::Start in the SDK, then enter nnMain.
#include "cloud.h"
#include "dm.h"
#include "front.h"
#include "game.h"
#include "logstamp.h"
#include <stdatomic.h>
#include "plat.h"
#include <unistd.h>
void probe_install(void); void hid_keys_init(void); void stubs_report(void); void crash_install(void); void guest_thread_init(void); uint32_t svc_thread_main_init(void);
void crash_sample_self(void);
typedef void (*fn0)(void); typedef void (*fn2)(uint64_t, uint64_t); typedef void (*fnp)(void *);
static void *req(const char *n) { void *p = guest_sym(n); if (!p) die("missing symbol %s", n); return p; }

// Game arguments: argv[0] = the game id, whatever follows "--" on the command line is appended (on the console nn::init takes them from the process
// argument area and hands them to the SDK through SetHostArgc / Argv)
static char *g_argv[64]; static int g_argc = 1;

static void guest_main(void) {
    guest_thread_init();
    crash_sample_self();   // DM_SAMPLE sampling (diagnostics)
    uint32_t hmain = svc_thread_main_init();   // main thread handle: x0 of nnosInitialize (kept in an SDK global for ThreadManagerImplByHorizon)
    // The steps of nn::init::Start: _init_libc1 = __nnmusl_init_locks (makes the stdio etc. locks recursive; without it nested locking in putc deadlocks)
    // + __init_ssp. Of the three callbacks rtld passes: A only sets an rtld flag (skipped), B = each module's DT_INIT (before nnMain), C = destructors (not run)
    ((fn0)req("_init_libc0"))();
    LOG(">>> nnosInitialize"); ((fn2)req("nnosInitialize"))(hmain, 0);
    ((fn0)req("_init_libc1"))();
    LOG(">>> nninitInitializeSdkModule"); ((fn0)req("nninitInitializeSdkModule"))();
    LOG(">>> nninitStartup"); ((fn0)req("nninitStartup"))();
    ((fn0)req("_init_libc2"))();
    dm_prof_mark("2 SDK initialization (nnosInitialize -> nninitStartup)");
    run_module_inits();   // rtld callback B: each module's DT_INIT (global constructors)
    dm_prof_mark("3 module init_array");
    ((fnp)req("_ZN2nn2os11SetHostArgcEi"))((void *)(uint64_t)g_argc);
    ((fnp)req("_ZN2nn2os11SetHostArgvEPPc"))(g_argv);
    dm_game_boot();   // game hook (game.h)
    LOG(">>> nnMain"); ((fn0)req("nnMain"))();
    LOG("nnMain returned");
}

struct dm_flags g_flag;
uint64_t g_t0_ns;
static void flags_init(void) {
    g_flag.thr = !!getenv("DM_THR"); g_flag.svc = !!getenv("DM_SVC"); g_flag.fs = !!getenv("DM_FS");
    g_flag.movie = !!getenv("DM_MOVIE"); g_flag.dbg = !!getenv("DM_DBG"); g_flag.imports = !!getenv("DM_IMPORTS");
    g_flag.bootprof = !!getenv("DM_BOOTPROF");
}

// Zero of the boot timeline = process creation (plat.h dm_proc_age_ns) converted to the now_ns monotonic base
static uint64_t proc_start_mono_ns(void) { uint64_t mono = now_ns(), age = dm_proc_age_ns(); return age && age < mono ? mono - age : mono; }

// Host loading (independent of UIKit / Metal): read the NSOs, relocate, take over imports, probes / keys. By default it runs in main;
// the iOS app calls dm_host_preload_start (same arguments) when it starts the host, so loading runs on a background thread alongside the UI setup.
static pthread_key_t g_race_keys[512];
static unsigned g_race_n, g_race_want;
static void *keyrace_thread(void *unused);
static void host_preload(int argc, char **argv) {
    g_t0_ns = proc_start_mono_ns();
    flags_init();
    dm_prof_mark("0 process creation -> host main (dyld)");
    dm_game_env_defaults();
    { void dm_mods_apply(void); dm_mods_apply(); }   // DM_MODS user mod folders in front of the RomFS (mods.c), after the game's own overlay default
    g_argv[0] = (char *)dm_game.id;
    LOG("game %s %s (%s), main build %s", dm_game.id, dm_game.version, dm_game.engine, dm_game.main_build_id);
    // Time zone: the game converts absolute to local time through the host libc (nn::time::ToCalendarTime / ToPosixTime in hle_time.c, and the guest
    // libc's localtime_r / mktime wired to the host), so the host follows the system time zone. Only deterministic runs (DM_FIXED_TIME) pin it to UTC.
    if (getenv("DM_FIXED_TIME")) setenv("TZ", "UTC", 1);
    crash_install();
    const char *race = getenv("DM_TEST_KEYRACE");   // test: another thread allocates pthread keys while the loader claims its own
    if (race && *race) {
        g_race_want = (unsigned)strtoul(race, NULL, 10);
        if (g_race_want > 512) g_race_want = 512;
        pthread_t th;
        if (pthread_create(&th, NULL, keyrace_thread, NULL)) die("DM_TEST_KEYRACE: cannot start its thread");
        pthread_detach(th);
    }
    { void dm_term_install(void); dm_term_install(); }   // SIGTERM: graceful exit (hle_lifecycle.c)
    { void crash_set_dump_hook(void (*)(void)); void svc_thread_dump(void); crash_set_dump_hook(svc_thread_dump); }   // dump guest threads on a crash
    // dmhost [<product dir> [module...]] [-- game arguments...]: without a directory, the profile's workspace.product under DM_ROOT
    const char *names[MAX_MOD]; int n = 0; char prod[1024]; const char *dir;
    int i = 1;
    if (argc >= 2 && strcmp(argv[1], "--")) dir = argv[i++];
    else if (dm_game.ws.product) dir = dm_game_ws_path(dm_game.ws.product, prod, sizeof prod);
    else die("usage: dmhost <product dir> [module...] [-- game arguments...] (the game profile has no workspace.product)");
    for (; i < argc && strcmp(argv[i], "--") && n < MAX_MOD; i++) names[n++] = argv[i];
    if (i < argc && !strcmp(argv[i], "--")) for (i++; i < argc && g_argc < 63; i++) g_argv[g_argc++] = argv[i];
    static char mods[256];   // no modules on the command line: the profile's list
    if (!n) { strlcpy(mods, dm_game.modules, sizeof mods); for (char *save, *t = strtok_r(mods, " ", &save); t && n < MAX_MOD; t = strtok_r(NULL, " ", &save)) names[n++] = t; }
    load_modules(dir, names, n);
    probe_install();   // DM_PROBE probes (diagnostics)
    hid_keys_init();   // DM_KEYS scripted input
    { void dm_cmd_start(void); dm_cmd_start(); }   // DM_CMD live command channel (cmdchan.c)
    { void dm_touchpad_selftest_start(void); dm_touchpad_selftest_start(); }   // DM_TEST_TOUCHPAD on-screen controls checks (touchpad.c)
    { void sead_list_watch_start(void); sead_list_watch_start(); }   // DM_LIST_WATCH (sead.c)
    { void dm_memsnap_init(void); dm_memsnap_init(); }   // DM_MEMSNAP memory snapshots (memsnap.c)
    stubs_report();
    dm_prof_mark("1 host loading (read NSO / relocate / take over imports)");
    { void loader_hash_log(void); loader_hash_log(); }   // DM_LOADHASH=1: address-independent hash of the load result (loader.c)
}
static struct { int argc; char **argv; pthread_t th; _Atomic int started; } g_pre;
static void *preload_thread(void *a) { (void)a; host_preload(g_pre.argc, g_pre.argv); return NULL; }
static void *keyrace_thread(void *unused) {
    (void)unused;
    for (unsigned i = 0; i < g_race_want; i++) if (pthread_key_create(&g_race_keys[g_race_n++], NULL)) break;
    return NULL;
}
// Start host loading early. Call it as one of the first things in the process (iOS: in the app's main() before UIApplicationMain), with the environment
// loading reads (DM_ROMFS / DM_SAVE / DM_TEMP etc.) already set; the arguments must equal those of the later main call item by item (otherwise it exits).
// Only once; without it loading runs synchronously in main. Does not depend on UIKit / Metal.
void dm_host_preload_start(int argc, char **argv) {
    if (atomic_exchange(&g_pre.started, 1)) return;
    g_pre.argc = argc; g_pre.argv = calloc((size_t)argc + 1, sizeof(char *));
    for (int i = 0; i < argc; i++) g_pre.argv[i] = strdup(argv[i]);
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 8 << 20);
    if (pthread_create(&g_pre.th, &a, preload_thread, NULL)) die("dm_host_preload_start: cannot start its thread");
}
#include <TargetConditionals.h>
int dm_app_bootstrap(int *argc, char ***argv);   // app_mac.m: as a double-clicked .app, prepare the environment and arguments; command line starts are unchanged
int main(int argc, char **argv) {
#if TARGET_OS_OSX   // the iOS app compiles this file too (its shell calls the entry); there is no app_mac.m there
    { const char *e = getenv("DM_LOG_TIME"); if (e && strcmp(e, "0")) logstamp_stderr(); }   // UTC milliseconds on log lines (host/logstamp.h; the iOS shell's default)
    if (dm_app_bootstrap(&argc, &argv)) dm_cloud_boot();   // .app: one iCloud sync before the save is mounted (as in the iOS shell)
    dm_game_slot_acquire(dm_game.id);   // one game at a time on this Mac (game_slot.c)
#endif
    if (getenv("DM_PRELOAD")) dm_host_preload_start(argc, argv);   // exercise the early-start path on the Mac (on iOS the app's main() calls it)
    if (atomic_load(&g_pre.started)) {
        pthread_join(g_pre.th, NULL);
        int same = argc == g_pre.argc; for (int i = 0; same && i < argc; i++) same = !strcmp(argv[i], g_pre.argv[i]);
        if (!same) die("[red] dm_host_preload_start got different arguments than main");
        void crash_thread_init(void); crash_thread_init();   // the signal alternate stack is per thread: loading installed it on another thread
        LOG("host loading already finished in the background (dm_host_preload_start)");
    } else host_preload(argc, argv);
    front_run(guest_main);
    return 0;
}
