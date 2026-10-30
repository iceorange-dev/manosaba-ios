// iOS app shell: the log, finding the game data, starting the host on its own thread, the app and scene delegates.
//   Log: stderr (the host's LOG) goes to Documents/boot.launch.txt; when the scene first comes to the foreground it becomes boot.txt and the previous
//   boot.txt is kept as boot.previous.txt (a launch by the system in the background must not replace the last session's log).
//   DM_LOG_PORT=<port>: also serve the log on 127.0.0.1:<port> (tools/logport.py through iproxy); "dump" dumps state, "quit" exits.
//   Game data (the player's own dump): RomFS in Library/Application Support/romfs (ios/run.sh push-data), Documents/RomFS (Files app) or the
//   app bundle's RomFS (ios/run.sh ipa with DM_IPA_DATA=1); the console's shared fonts in sysfont next to it, and the game's RomFS overlay in
//   romfs_overlay (served when made from this RomFS). Checked against the game profile (romfs_validate.c); when missing, a page says what is
//   missing and where it goes.
//   The host starts only when the scene comes to the foreground: a launch by the system in the background never starts it.
//   Mods (host/game/mods.c): Documents/mods when it exists (Files app; one folder per mod, or an SD card's atmosphere folder).
//   Run diagnostics (plan item 106): log lines carry UTC milliseconds (host/logstamp.h; DM_LOG_TIME=0 in env.txt turns it off); Documents/boot.running
//   marks a session in the foreground, so a launch knows when the last one ended there without an orderly exit and keeps its log as
//   boot.interrupted.txt (host/rundiag.h); the home screen quick action Export Diagnostics shares the logs with the device's details (diag_export.m).
#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#include "codesig.h"
#include "envfile.h"
#include "logstamp.h"
#include "rundiag.h"
#include "shadercache.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <netinet/in.h>
#include <os/proc.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include "cloud.h"
#include "dm.h"
#include "front.h"
#include "game.h"
#include "lang.h"
#include "romfs_validate.h"
#include "romfs_parts.h"
#include "coolstart.h"
#include "nvn_pace.h"   // nvn_fps_policy (plan item 135)
#include "native_size.h"   // dm_native_size (the setting "Native")

int dm_main(int argc, char **argv);   // host/runtime/main.c, compiled with -Dmain=dm_main
UIView *front_ios_view(CGRect frame, CGFloat scale);   // scale = the screen's native scale
void front_ios_lifecycle_install(void);
void front_ios_focus_event(int ev);
unsigned long long mtl_gpu_allocated_mb(void);
void plog(const char *fmt, ...);
void dm_log_overlay_attach(UIView *view);   // log_overlay.m
void dm_log_overlay_refresh(void);
void dm_diag_export(UIViewController *vc, void (^done)(void));   // diag_export.m

// ---------- Log ----------
static int g_log = -1, g_log_tee, g_log_pipe[2] = {-1, -1};
static char g_log_path[1024];
int dm_log_fd(void) { return g_log; }   // log_overlay.m reads the same file
static void write_all(int fd, const char *p, size_t n) {
    while (n) { ssize_t w = write(fd, p, n); if (w > 0) { p += w; n -= (size_t)w; } else if (w < 0 && errno == EINTR) continue; else break; }
}
void plog(const char *fmt, ...) {   // through stderr, so the line is stamped like the host's
    char b[1024]; va_list ap; va_start(ap, fmt); int n = vsnprintf(b, sizeof b - 1, fmt, ap); va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof b - 2) n = sizeof b - 2;
    b[n++] = '\n'; fwrite(b, 1, (size_t)n, stderr);
}
// DM_LOG_PORT: one client at a time; on connect it gets the log so far, then every new line. The tee thread writes the file and the client under one
// lock, so the replay neither misses nor repeats a line
static pthread_mutex_t g_client_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_client = -1;
static void *log_port_reader(void *arg) {
    int c = (int)(intptr_t)arg; char b[64], acc[16] = {0}; ssize_t r;
    while ((r = read(c, b, sizeof b - 1)) > 0) {
        b[r] = 0; size_t n = strlen(acc); char cat[sizeof acc + sizeof b]; memcpy(cat, acc, n); memcpy(cat + n, b, (size_t)r + 1);   // a command split over two reads
        if (strstr(cat, "dump")) { crash_dump_now("DM_LOG_PORT dump"); cat[0] = 0; }
        if (strstr(cat, "quit")) {   // acknowledge on the same connection before exiting: iproxy drops everything once it sees EOF
            static const char ack[] = "DM_LOG_PORT: quit received, exiting\n";
            pthread_mutex_lock(&g_client_mu); write_all(c, ack, sizeof ack - 1); pthread_mutex_unlock(&g_client_mu);
            fprintf(stderr, "%s", ack); dm_host_shutdown("DM_LOG_PORT quit"); _exit(0);
        }
        size_t m = strlen(cat), keep = m < sizeof acc - 1 ? m : sizeof acc - 1; memcpy(acc, cat + m - keep, keep); acc[keep] = 0;
    }
    return NULL;
}
static int log_port_open(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    struct sockaddr_in a = {.sin_len = sizeof a, .sin_family = AF_INET, .sin_port = htons((uint16_t)port), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    if (s < 0 || setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) || bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 1)) {
        fprintf(stderr, "[red] DM_LOG_PORT: cannot listen on 127.0.0.1:%d: %s\n", port, strerror(errno)); if (s >= 0) close(s); return -1;
    }
    return s;
}
static void *log_port_main(void *arg) {
    int port = (int)(intptr_t)arg, one = 1, s = log_port_open(port);
    if (s < 0) return NULL;
    fprintf(stderr, "DM_LOG_PORT: listening on 127.0.0.1:%d\n", port);
    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) {   // iOS takes back the socket when it suspends the app: listen again
            if (errno == EINTR) continue;
            close(s); while ((s = log_port_open(port)) < 0) sleep(1);
            continue;
        }
        setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
        pthread_mutex_lock(&g_client_mu);
        if (g_client >= 0) close(g_client);
        char lp[PATH_MAX]; int f = fcntl(g_log, F_GETPATH, lp) == 0 ? open(lp, O_RDONLY) : -1; char b[16384]; ssize_t r;   // the file may have been renamed
        if (f >= 0) { while ((r = read(f, b, sizeof b)) > 0) write_all(c, b, (size_t)r); close(f); }
        g_client = c;
        pthread_mutex_unlock(&g_client_mu);
        pthread_t rd; if (!pthread_create(&rd, NULL, log_port_reader, (void *)(intptr_t)c)) pthread_detach(rd);
    }
    return NULL;
}
static void *log_tee_main(void *unused) {
    (void)unused; char buf[4096], line[8192]; size_t used = 0;
    for (;;) {
        ssize_t n = read(g_log_pipe[0], buf, sizeof buf);
        if (n == 0) break;
        if (n < 0) { if (errno == EINTR) continue; break; }
        for (ssize_t i = 0; i < n; i++) {
            if (used < sizeof line) line[used++] = buf[i];
            if (buf[i] != '\n' && used < sizeof line) continue;
            pthread_mutex_lock(&g_client_mu);
            write_all(g_log, line, used);
            if (g_client >= 0) { ssize_t w = write(g_client, line, used); if (w <= 0) { close(g_client); g_client = -1; } }
            pthread_mutex_unlock(&g_client_mu);
            used = 0;
        }
    }
    if (used) write_all(g_log, line, used);
    return NULL;
}
static void log_open(void) {
    NSString *doc = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)[0];
    strlcpy(g_log_path, [doc stringByAppendingPathComponent:@"boot.launch.txt"].fileSystemRepresentation, sizeof g_log_path);
    g_log = open(g_log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    const char *port = getenv("DM_LOG_PORT");
    if (port && !pipe(g_log_pipe)) {
        pthread_t t; if (!pthread_create(&t, NULL, log_tee_main, NULL)) { pthread_detach(t); g_log_tee = 1; dup2(g_log_pipe[1], 2); close(g_log_pipe[1]); }
        pthread_t lp; if (g_log_tee && !pthread_create(&lp, NULL, log_port_main, (void *)(intptr_t)atoi(port))) pthread_detach(lp);
    }
    if (!g_log_tee) dup2(g_log, 2);   // the host's LOG / die write stderr
    setvbuf(stderr, NULL, _IOLBF, 0);
}
// The scene comes to the foreground for the first time: this launch is a real session, its log becomes boot.txt
static void log_promote(void) {
    static int done; if (done) return; done = 1;
    NSString *doc = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)[0];
    const char *cur = [doc stringByAppendingPathComponent:@"boot.txt"].fileSystemRepresentation, *prev = [doc stringByAppendingPathComponent:@"boot.previous.txt"].fileSystemRepresentation;
    if (rename(cur, prev) != 0 && errno != ENOENT) { plog("boot: could not keep the previous log (%s); this log stays boot.launch.txt", strerror(errno)); return; }
    if (rename(g_log_path, cur) != 0) plog("boot: could not rename the log to boot.txt (%s)", strerror(errno));
}
// The run marker (host/rundiag.h): written while the session is in the foreground, naming its log as it is called now
static char g_docs[1024], g_launch_note[700] = "the last session ended in order, or this is the first launch";
const char *dm_diag_launch_note(void) { return g_launch_note; }
static void run_mark(int on) {
    if (!on) { rundiag_unmark(g_docs); return; }
    char lp[PATH_MAX]; const char *name = "boot.launch.txt";
    if (g_log >= 0 && fcntl(g_log, F_GETPATH, lp) == 0) { const char *b = strrchr(lp, '/'); name = b ? b + 1 : lp; }
    if (rundiag_mark(g_docs, name)) plog("[red] boot: could not write the run marker: %s", strerror(errno));
}
void dm_exit_note(void) { run_mark(0); }   // host/hle/hle_lifecycle.c dm_host_shutdown: an orderly exit (the scene, SIGTERM, a quit command)

// ---------- Game data ----------
static NSString *support_dir(void) { return NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES)[0]; }
static NSString *docs_dir(void) { return NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)[0]; }
static NSString *bundle_dir(void) { return NSBundle.mainBundle.resourcePath; }   // read-only; holds the data only when packaged with it
// The first RomFS that passes the profile's check; why = the reason of the last one tried
static __weak UILabel *g_prep_label; static NSString *g_prep_head;   // the screen shown while the data is located (SceneDelegate showGameOrMissing)
static void parts_line(const char *s) {
    plog("boot: %s", s);
    NSString *t = @(s); dispatch_async(dispatch_get_main_queue(), ^{ g_prep_label.text = [NSString stringWithFormat:@"%@\n\n%@", g_prep_head ?: @"", t]; });
}
static NSString *romfs_locate(char *why, size_t n) {
    // RomFS parts copied into Documents (plan item 62e, tools/romfs_parts.py: a few 1 GiB files instead of tens of thousands): unpacked into
    // Application Support/romfs first, each part checked, then deleted (host/runtime/romfs_parts.c)
    { char pw[512]; [NSFileManager.defaultManager createDirectoryAtPath:support_dir() withIntermediateDirectories:YES attributes:nil error:nil];
      int pr = dm_romfs_parts_unpack(docs_dir().fileSystemRepresentation, [support_dir() stringByAppendingPathComponent:@"romfs"].fileSystemRepresentation, pw, sizeof pw, parts_line);
      if (pr >= 0) plog("%sboot: RomFS parts: %s", pr ? "" : "[red] ", pw); }
    NSArray<NSString *> *cand = @[[support_dir() stringByAppendingPathComponent:@"romfs"], [docs_dir() stringByAppendingPathComponent:@"RomFS"],
                                  [bundle_dir() stringByAppendingPathComponent:@"RomFS"]];
    snprintf(why, n, "no RomFS folder");
    [NSFileManager.defaultManager createDirectoryAtPath:support_dir() withIntermediateDirectories:YES attributes:nil error:nil];
    for (NSUInteger i = 0; i < cand.count; i++) { NSString *d = cand[i];
        if (![NSFileManager.defaultManager fileExistsAtPath:d]) continue;
        // the tree walked once, then a marker (plan item 175; Tears of the Kingdom's RomFS has tens of thousands of files); the key files every time
        NSString *mk = [support_dir() stringByAppendingPathComponent:[NSString stringWithFormat:@"romfs-verified-%lu.txt", (unsigned long)i]]; int walked = 0;
        uint64_t t0 = clock_gettime_nsec_np(CLOCK_MONOTONIC);
        int ok = dm_romfs_validate_marked(d.fileSystemRepresentation, mk.fileSystemRepresentation, why, n, &walked);
        plog("boot: %s: %s (%s, %.0f ms)", d.fileSystemRepresentation, why, walked ? "walked" : "marker", (clock_gettime_nsec_np(CLOCK_MONOTONIC) - t0) / 1e6);
        if (ok) return d;
    }
    return nil;
}
static NSString *sysfont_locate(void) {
    for (NSString *d in @[[support_dir() stringByAppendingPathComponent:@"sysfont"], [docs_dir() stringByAppendingPathComponent:@"sysfont"],
                          [bundle_dir() stringByAppendingPathComponent:@"sysfont"]])
        if ([NSFileManager.defaultManager fileExistsAtPath:d]) return d;
    return nil;
}

// The game data is the player's own dump, copied in again at will and gigabytes large (Tears of the Kingdom's RomFS is 17 GB): kept out of iCloud and
// computer backups (BotWRecomp does the same). The bundle is never backed up; the Files app still shows the folders. Logged with the value read back
static void exclude_from_backup(NSString *d) {
    if (!d || [d hasPrefix:bundle_dir()]) return;
    NSURL *u = [NSURL fileURLWithPath:d isDirectory:YES]; NSError *e = nil; NSNumber *v = nil;
    BOOL set = [u setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&e];
    [u getResourceValue:&v forKey:NSURLIsExcludedFromBackupKey error:nil];
    plog("boot: %s excluded from backups: %s%s%s", d.fileSystemRepresentation, set && v.boolValue ? "yes" : "no", e ? " — " : "", e ? e.localizedDescription.UTF8String : "");
}
// The game's RomFS overlay (Tears of the Kingdom: games/totk/romfs_overlay.py's quick-menu backgrounds and game over flow at 60 fps and up; on the
// Mac games/totk/defaults.c serves the workspace's): the first romfs_overlay folder, in the RomFS's places, made from this RomFS
// (dm_romfs_overlay_check), unless DM_ROMFS_OVERLAY is set (env.txt; empty turns it off). ios/run.sh push-data copies the workspace's in, as
// MacNativeGames ships its overlays with the data. Without it the 60 fps product drew the ability wheel (hold L) over black on the iPhone 17 Pro
// (2026-10-22): the overlay's quick-menu fix was never served on iOS
static void overlay_locate(NSString *romfs) {
    if (getenv("DM_ROMFS_OVERLAY")) { plog("boot: RomFS overlay \"%s\" (DM_ROMFS_OVERLAY)", getenv("DM_ROMFS_OVERLAY")); return; }
    for (NSString *d in @[[support_dir() stringByAppendingPathComponent:@"romfs_overlay"], [docs_dir() stringByAppendingPathComponent:@"romfs_overlay"],
                          [bundle_dir() stringByAppendingPathComponent:@"romfs_overlay"]]) {
        BOOL dir = NO; if (![NSFileManager.defaultManager fileExistsAtPath:d isDirectory:&dir] || !dir) continue;
        char why[512]; uint64_t t0 = clock_gettime_nsec_np(CLOCK_MONOTONIC);
        int ok = dm_romfs_overlay_check(d.fileSystemRepresentation, romfs.fileSystemRepresentation, why, sizeof why);
        plog("%sboot: RomFS overlay %s: %s (%.0f ms)", ok ? "" : "[red] ", d.fileSystemRepresentation, why, (clock_gettime_nsec_np(CLOCK_MONOTONIC) - t0) / 1e6);
        if (ok) { setenv("DM_ROMFS_OVERLAY", d.fileSystemRepresentation, 1); exclude_from_backup(d); return; }
    }
    plog("boot: no RomFS overlay (ios/run.sh push-data copies the workspace's build/<game>/romfs_overlay)");
}

// ---------- Host ----------
static char *g_argv[16]; static int g_argc;
static int g_host_up;
int dm_diag_host_up(void) { return g_host_up; }
static void boot_prepare(NSString *romfs) {
    NSFileManager *fm = NSFileManager.defaultManager; NSString *doc = docs_dir();
    NSString *res = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"dm"];
    for (NSString *d in @[@"save", @"temp"]) [fm createDirectoryAtPath:[doc stringByAppendingPathComponent:d] withIntermediateDirectories:YES attributes:nil error:nil];
    setenv("DM_ROMFS", romfs.fileSystemRepresentation, 0);
    NSString *font = sysfont_locate(); if (font) setenv("DM_SYSFONT", font.fileSystemRepresentation, 0);
    exclude_from_backup(romfs); exclude_from_backup(font);
    overlay_locate(romfs);   // before the host starts: host/game/mods.c puts the mods' layers in front of it
    setenv("DM_SAVE", [doc stringByAppendingPathComponent:@"save"].fileSystemRepresentation, 0);
    setenv("DM_TEMP", [doc stringByAppendingPathComponent:@"temp"].fileSystemRepresentation, 0);
    // The game's cache storage (Tears of the Kingdom; hle_fs.c): in Library/Caches, which the system may empty while the app is not running, as a
    // console's cache storage may be deleted. Unset, the Mac default under $HOME/dm-work does not exist in the container: the game asked again and
    // again (933,995 red lines in 10 s on the first iPhone run)
    { NSString *cs = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES)[0] stringByAppendingPathComponent:@"cache_storage"];
      [fm createDirectoryAtPath:cs withIntermediateDirectories:YES attributes:nil error:nil]; setenv("DM_CACHE", cs.fileSystemRepresentation, 0); }
    { NSString *mods = [doc stringByAppendingPathComponent:@"mods"]; BOOL dir = NO;
      if ([fm fileExistsAtPath:mods isDirectory:&dir] && dir) setenv("DM_MODS", mods.fileSystemRepresentation, 0); }
    setenv("DM_NVN_SHMAP", [res stringByAppendingPathComponent:@"shader_map.tsv"].fileSystemRepresentation, 0);
    // The game's language from the app's own language setting (plan item 170: Settings > Apps > <game> > Language, offered for the languages the app
    // declares in CFBundleLocalizations, ios/tools/project_lang.py): the device's first language, when the game has it; else the profile's own
    if (!getenv("DM_LANG")) {
        NSArray *locs = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleLocalizations"]; NSString *tag = NSLocale.preferredLanguages.firstObject;
        const char *code = [locs isKindOfClass:NSArray.class] && locs.count ? dm_lang_pick(tag.UTF8String, dm_game.languages) : NULL;
        if (code) setenv("DM_LANG", code, 1);
        plog("boot: language %s (the app's language %s; the game's: %s)", code ? code : dm_game.language, tag.UTF8String ?: "none", dm_game.languages);
    }
    setenv("DM_MSL_DIR", [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"msl"].fileSystemRepresentation, 0);
    // Relative paths in DM_* switches (screenshots etc.) land in Documents
    if (chdir(doc.fileSystemRepresentation)) plog("[red] boot: chdir Documents failed: %s", strerror(errno));
    g_argv[g_argc++] = "dmhost"; g_argv[g_argc++] = strdup(res.fileSystemRepresentation);
    char *mods = strdup(dm_game.modules);
    for (char *sv, *m = strtok_r(mods, " ", &sv); m && g_argc < 15; m = strtok_r(NULL, " ", &sv)) g_argv[g_argc++] = m;
    plog("boot: RomFS %s, fonts %s, save %s", romfs.fileSystemRepresentation, font ? font.fileSystemRepresentation : "(none)", getenv("DM_SAVE"));
}
static int thermal_now(void) { return (int)NSProcessInfo.processInfo.thermalState; }
static void sleep_for(double s) { usleep((useconds_t)(s * 1e6)); }
static void *boot_thread(void *arg) {
    (void)arg;
    { int lv = cool_level(getenv("DM_COOL_START")), at;   // an A/B run on the phone starts cool (coolstart.h, plan item 62f)
      if (lv >= 0) { int was = thermal_now(); double w = cool_wait(lv, thermal_now, sleep_for, 5, 600, &at);
          plog("%sboot: DM_COOL_START=%s: thermal state %d → %d after %.0f s", at > lv ? "[red] " : "", getenv("DM_COOL_START"), was, at, w); } }
    { int dm_pwrtest(void); if (dm_pwrtest()) return NULL; }   // DM_PWRTEST: a plain CPU / GPU load instead of the game (pwrtest.m, plan item 230)
    dm_cloud_boot();   // iCloud: newer cloud saves are swapped in before the save is mounted (waits up to 5 s)
    int rc = dm_main(g_argc, g_argv);
    plog("boot: dm_main returned %d", rc);
    return NULL;
}
// Graphics settings (Settings app → this app, Settings.bundle): dm.fps 60 / 30, dm.render_height 0 (the game's own) / -1 (native) / 900 / 720 / 540, dm.metalfx,
// dm.hot_hold (Hold 30 fps when hot, on unless turned off).
// At start they become DM_FPS / DM_RENDER_HEIGHT / DM_UPSCALE, unless a launch already set those (devicectl --environment, tests). Frame rate and
// MetalFX also follow a change made while the game runs (on returning to it); the render resolution is read by the game once, at startup.
void mtl_fps_set(int fps); void mtl_upscale_set(int on);   // gfx/nvn/nvn_mtl.m
static NSInteger g_gfx_fps, g_gfx_fx;   // the settings as last applied: a later change is applied, an unchanged one leaves a launch's variables alone
// The frame rate applied is the setting held at 30 in Low Power Mode or from thermal state Serious up (nvn_fps_policy, plan item 135): iOS throttles
// the device then anyway, and a game paced for 60 misses frames unevenly. Re-applied when the setting, the power mode or the thermal state changes
// dm.hot_hold: "Hold 30 fps when hot" (plan item 135a), on unless the player turned it off (unset reads as on: the behaviour before the switch)
static int hot_hold(void) { id v = [NSUserDefaults.standardUserDefaults objectForKey:@"dm.hot_hold"]; return v ? [v boolValue] : 1; }
static int fps_afforded(NSInteger user, const char **why) {
    NSProcessInfo *pi = NSProcessInfo.processInfo; int lp = pi.isLowPowerModeEnabled, th = (int)pi.thermalState, hold = hot_hold(), f = nvn_fps_policy((int)user, lp, th, hold);
    *why = f < user ? (lp ? "Low Power Mode" : th >= 3 ? "thermal state critical" : "thermal state serious")
                    : th >= 2 && !hold && user > 30 ? "the setting; hot, but Hold 30 fps when hot is off" : "the setting";
    return f;
}
static void gfx_settings_boot(void) {
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults; const char *why;
    g_gfx_fps = fps_afforded([d integerForKey:@"dm.fps"] == 30 ? 30 : 60, &why); g_gfx_fx = [d boolForKey:@"dm.metalfx"];
    NSInteger h = [d integerForKey:@"dm.render_height"];
    if (g_gfx_fps == 30) setenv("DM_FPS", "30", 0);
    if (h == -1) {   // "Native" (plan item 281): the 16:9 picture the screen's pixels hold, reported as the display (DM_DISPLAY_SIZE) and rendered
                     // at (DM_RENDER_HEIGHT). manosaba renders it (games/manosaba/graphics.c; the width as Screen.SetResolution's hook scales 2560x1440);
                     // Tears of the Kingdom goes docked and renders the largest 16:9 its heap holds, 1920x1080 (games/totk/frame_step.c)
        UIWindowScene *ws = nil; for (UIScene *sc in UIApplication.sharedApplication.connectedScenes) if ([sc isKindOfClass:UIWindowScene.class]) { ws = (UIWindowScene *)sc; break; }
        CGSize nb = ws ? ws.screen.nativeBounds.size : CGSizeZero; unsigned W, H;
        if (dm_native_size((unsigned)nb.width, (unsigned)nb.height, &W, &H)) { char b[32]; snprintf(b, sizeof b, "%ux%u", W, H); setenv("DM_DISPLAY_SIZE", b, 0); snprintf(b, sizeof b, "%u", H); setenv("DM_RENDER_HEIGHT", b, 0); }
    } else if (h > 0) { char b[16]; snprintf(b, sizeof b, "%ld", (long)h); setenv("DM_RENDER_HEIGHT", b, 0); }
    if (g_gfx_fx) setenv("DM_UPSCALE", "metalfx", 0);
    plog("boot: graphics settings: %ld fps (%s), render height %s%s%s, MetalFX %s, hold 30 fps when hot %s", (long)g_gfx_fps, why, getenv("DM_RENDER_HEIGHT") ?: "the game's",
         getenv("DM_DISPLAY_SIZE") ? ", display " : "", getenv("DM_DISPLAY_SIZE") ?: "", g_gfx_fx ? "on" : "off", hot_hold() ? "on" : "off");
}
static void gfx_settings_refresh(void) {
    if (!g_host_up) return;
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    const char *why; NSInteger fps = fps_afforded([d integerForKey:@"dm.fps"] == 30 ? 30 : 60, &why), fx = [d boolForKey:@"dm.metalfx"];
    if (fps != g_gfx_fps) { g_gfx_fps = fps; mtl_fps_set((int)fps); plog("frame rate %ld fps (%s)", (long)fps, why); }
    if (fx != g_gfx_fx) { g_gfx_fx = fx; mtl_upscale_set((int)fx); }
}
static void host_start(NSString *romfs) {
    if (g_host_up) return;
    g_host_up = 1; plog("boot: scene in the foreground, starting the host");
    gfx_settings_boot();
    [NSNotificationCenter.defaultCenter addObserverForName:NSUserDefaultsDidChangeNotification object:nil queue:NSOperationQueue.mainQueue
                                                usingBlock:^(NSNotification *n) { (void)n; gfx_settings_refresh(); }];
    for (NSNotificationName nm in @[NSProcessInfoPowerStateDidChangeNotification, NSProcessInfoThermalStateDidChangeNotification])   // (they come on any thread)
        [NSNotificationCenter.defaultCenter addObserverForName:nm object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) { (void)n; gfx_settings_refresh(); }];
    boot_prepare(romfs);
    dm_host_preload_start(g_argc, g_argv);   // module loading and relocation on a background thread, alongside the rest of the UI setup
    front_ios_lifecycle_install();
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 64 << 20);
    pthread_t t; pthread_create(&t, &a, boot_thread, NULL);
}

// ---------- View controllers ----------
@interface DmGameVC : UIViewController
@end
@implementation DmGameVC
// The system edge gestures are deferred (the first swipe from an edge only shows the home indicator / grabber, the second acts), and the home
// indicator is NOT set to auto-hide: auto-hide takes precedence over the deferral and brings the indicator back at full brightness on every tap
// anywhere (iPad mini, iOS 18.6.2; the same reports in Unity's forum thread "Option to hide iPhone X home indicator + Edge Protection", 2017-18).
// With the deferral alone, taps never bring it back and leaving takes a second swipe; the indicator stays faint on that iPad, and is not shown at
// all on an iPhone 17 Pro (iOS 26.4)
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }
- (BOOL)prefersHomeIndicatorAutoHidden { return NO; }
- (BOOL)prefersStatusBarHidden { return YES; }
@end
// Shown instead of the game when the game data is missing: what is missing and where it goes; Retry checks again
@interface DmMissingVC : UIViewController
@property (copy) void (^retry)(void);
@property (strong) UILabel *label;
@property BOOL preparing;   // the game data being located: no Retry
@end
@implementation DmMissingVC
- (BOOL)prefersStatusBarHidden { return YES; }
- (void)viewDidLoad {
    [super viewDidLoad]; self.view.backgroundColor = UIColor.blackColor;
    self.label = [UILabel new]; self.label.numberOfLines = 0; self.label.textColor = UIColor.whiteColor; self.label.textAlignment = NSTextAlignmentCenter;
    self.label.font = [UIFont systemFontOfSize:15]; [self.view addSubview:self.label];
    UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem]; [b setTitle:@"Retry" forState:UIControlStateNormal]; b.tag = 1;
    [b addTarget:self action:@selector(tap) forControlEvents:UIControlEventTouchUpInside]; [self.view addSubview:b];
    UIButton *x = [UIButton buttonWithType:UIButtonTypeSystem]; [x setTitle:@"Export Diagnostics" forState:UIControlStateNormal]; x.tag = 2;
    [x addTarget:self action:@selector(exportDiag) forControlEvents:UIControlEventTouchUpInside]; [self.view addSubview:x];
    b.hidden = self.preparing;
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews]; CGRect r = CGRectInset(self.view.bounds, 40, 30);
    self.label.frame = CGRectMake(r.origin.x, r.origin.y, r.size.width, r.size.height - 50);
    [self.view viewWithTag:1].frame = CGRectMake(CGRectGetMidX(r) - 160, CGRectGetMaxY(r) - 44, 120, 44);
    [self.view viewWithTag:2].frame = CGRectMake(CGRectGetMidX(r) + 0, CGRectGetMaxY(r) - 44, 180, 44);
}
- (void)tap { if (self.retry) self.retry(); }
- (void)exportDiag { dm_diag_export(self, nil); }
@end

// The GPU this build needs: Metal 4 (A14 / M1 and newer on iOS 26; iOS 26 also runs on A13, where creating the Metal 4 queue fails and the app
// ended with no word), or Metal 3 for the DM_IOS_M3 build. Checked before the host starts, so an unsupported device gets a screen saying so
// (upstream d2r-mobile b94670c, issue #41). DM_TEST_NO_METAL4=1 shows the screen on any device (env.txt)
static int gpu_supported(char *why, size_t n) {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); int ok = 0;
    if (getenv("DM_TEST_NO_METAL4")) ok = 0;
#if TARGET_OS_SIMULATOR   // the Simulator's GPU is an Apple family 2 stand-in, no Metal 3 (plan item 114): run anyway, for the controls, safe areas and lifecycle
    else { ok = dev != nil; if (ok) plog("boot: the iOS Simulator's GPU (%s, argument buffers tier %d): run for the controls, safe areas and lifecycle, the pictures are not a device's",
                                        dev.name.UTF8String, (int)dev.argumentBuffersSupport + 1); else snprintf(why, n, "No Metal device in the Simulator."); }
#elif DM_NVN_M3
    else ok = dev && [dev supportsFamily:MTLGPUFamilyMetal3];
    if (!ok) snprintf(why, n, "This device's GPU (%s) does not support Metal 3, which this build needs.", dev ? dev.name.UTF8String : "none");
#else
    else if (@available(iOS 26.0, *)) ok = dev && [dev supportsFamily:MTLGPUFamilyMetal4];
    if (!ok) snprintf(why, n, "This device's GPU (%s) does not support Metal 4, which this build needs (A14 or M1 and newer, iOS 26). "
                              "The iOS 18 build (DM_IOS_M3=1) runs on Metal 3 GPUs.", dev ? dev.name.UTF8String : "none");
#endif
    return ok;
}

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property (strong, nonatomic) UIWindow *window;
@property (strong) NSString *romfs;
@property BOOL exportFirst;   // launched by the Export Diagnostics quick action: export before the host starts
@property BOOL locating;      // romfs_locate running off the main thread
@property BOOL exporting;     // the launch's export still on screen: the host waits for it
@end
@implementation SceneDelegate
// The game data is located off the main thread: Tears of the Kingdom's 16 RomFS parts take minutes to check and unpack and its 304,243 files a
// while to walk the first time, and on the main thread the system's launch watchdog ends the app first (plan item 62i). A screen says what is
// happening meanwhile; the host starts once the data is there, at once if the scene is already in the foreground
- (void)showGameOrMissing {
    char why[1024];
    if (!gpu_supported(why, sizeof why)) {
        plog("boot: GPU not supported: %s", why);
        DmMissingVC *m = [DmMissingVC new]; self.window.rootViewController = m;
        m.label.text = [NSString stringWithFormat:@"%@ %@\n\n%@", @(dm_game.title), @(dm_game.version), @(why)];
        return;
    }
    if (self.locating) return;
    self.locating = YES; UIApplication.sharedApplication.idleTimerDisabled = YES;   // no auto-lock while unpacking (the host keeps it off later)
    DmMissingVC *p = [DmMissingVC new]; p.preparing = YES; self.window.rootViewController = p; [p loadViewIfNeeded];
    g_prep_head = [NSString stringWithFormat:@"%@ %@\n\nPreparing the game data…", @(dm_game.title), @(dm_game.version)];
    p.label.text = g_prep_head; g_prep_label = p.label;
    __weak SceneDelegate *me = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        char w[1024]; NSString *rf = romfs_locate(w, sizeof w); NSString *ws = @(w);
        dispatch_async(dispatch_get_main_queue(), ^{ SceneDelegate *s = me; if (!s) return; s.locating = NO; [s showLocated:rf why:ws]; });
    });
}
- (void)showLocated:(NSString *)rf why:(NSString *)whyText {
    UIWindowScene *ws = self.window.windowScene; const char *why = whyText.UTF8String;
    if (rf) {
        self.romfs = rf;
        // UIWindowSceneGeometry.coordinateSpace exists from iOS 26 only (the DM_IOS_M3 build runs on iOS 18: unrecognized selector at launch);
        // UIWindowScene.coordinateSpace is the same space before that (deprecated from 26)
        CGRect bounds;
        if (@available(iOS 26.0, *)) bounds = ws.effectiveGeometry.coordinateSpace.bounds;
        else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            bounds = ws.coordinateSpace.bounds;
#pragma clang diagnostic pop
        }
        DmGameVC *vc = [DmGameVC new]; vc.view = front_ios_view(bounds, ws.screen.nativeScale);
        self.window.rootViewController = vc;
        dm_log_overlay_attach(vc.view);
        // In the foreground already (the usual case: locating outlasts the launch): start now; otherwise sceneWillEnterForeground starts it. Not
        // before the Export Diagnostics quick action has exported (its completion starts the host)
        UISceneActivationState st = ws.activationState;
        if (!g_host_up && !self.exportFirst && !self.exporting && (st == UISceneActivationStateForegroundActive || st == UISceneActivationStateForegroundInactive)) {
            host_start(rf); front_ios_focus_event(2); if (st == UISceneActivationStateForegroundActive) front_ios_focus_event(3); }
        return;
    }
    plog("boot: game data missing: %s", why);
    DmMissingVC *m = [DmMissingVC new];
    __weak SceneDelegate *ws_ = self; m.retry = ^{ [ws_ showGameOrMissing]; };
    self.window.rootViewController = m;
    m.label.text = [NSString stringWithFormat:@"%@ %@\n\nThe game data is missing or incomplete:\n%@\n\nCopy the RomFS of your own copy of the game into the folder "
                    "\"RomFS\" of this app (Files app → On My iPhone), and the console's shared fonts into \"sysfont\". Then tap Retry.",
                    @(dm_game.title), @(dm_game.version), whyText];
}
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)s options:(UISceneConnectionOptions *)o {
    (void)s;
    self.exportFirst = [o.shortcutItem.type isEqualToString:@"export-diagnostics"];
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.backgroundColor = UIColor.blackColor;
    [self showGameOrMissing];
    [self.window makeKeyAndVisible];
}
- (void)sceneWillResignActive:(UIScene *)s { (void)s; if (g_host_up) front_ios_focus_event(0); }
- (void)sceneDidEnterBackground:(UIScene *)s { (void)s; run_mark(0); if (g_host_up) front_ios_focus_event(1); }
- (void)sceneWillEnterForeground:(UIScene *)s {
    (void)s; log_promote(); run_mark(1);
    if (!self.romfs || self.exportFirst) return;
    if (!g_host_up) host_start(self.romfs);
    front_ios_focus_event(2);
}
- (void)sceneDidBecomeActive:(UIScene *)s {
    (void)s; dm_log_overlay_refresh(); gfx_settings_refresh(); if (g_host_up) front_ios_focus_event(3);
    if (self.exportFirst) {
        self.exportFirst = NO; self.exporting = YES; __weak SceneDelegate *me = self;
        dm_diag_export(self.window.rootViewController, ^{
            me.exporting = NO;
            if (!me.romfs || g_host_up) return;
            host_start(me.romfs); front_ios_focus_event(2); front_ios_focus_event(3);
        });
    }
}
// The Export Diagnostics quick action while the app runs (Info.plist UIApplicationShortcutItems, ios/project.yml)
- (void)windowScene:(UIWindowScene *)ws performActionForShortcutItem:(UIApplicationShortcutItem *)item completionHandler:(void (^)(BOOL))done {
    (void)ws; BOOL ours = [item.type isEqualToString:@"export-diagnostics"];
    if (ours) dm_diag_export(self.window.rootViewController, nil);
    done(ours);
}
// Scene gone / app terminating: orderly host shutdown. (Swiping away a suspended app is a SIGKILL: neither of these runs.)
- (void)sceneDidDisconnect:(UIScene *)s { (void)s; run_mark(0); if (g_host_up) dm_host_shutdown("iOS scene disconnected"); }
@end

@interface App : UIResponder <UIApplicationDelegate>
@end
@implementation App
- (void)applicationWillTerminate:(UIApplication *)a { (void)a; run_mark(0); if (g_host_up) dm_host_shutdown("iOS app terminating"); }
// Memory warning: one log line (the system gives no other notice before ending the app for memory)
- (void)applicationDidReceiveMemoryWarning:(UIApplication *)a {
    static int n; if (++n > 50) return;
    task_vm_info_data_t vi; mach_msg_type_number_t cnt = TASK_VM_INFO_COUNT; unsigned long long fp = 0;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &cnt) == KERN_SUCCESS) fp = vi.phys_footprint >> 20;
    plog("[red] memory warning #%d (application state %ld): footprint %llu MB, GPU %llu MB, %llu MB left before the limit", n, (long)a.applicationState, fp,
         g_host_up ? mtl_gpu_allocated_mb() : 0ull, (unsigned long long)(os_proc_available_memory() >> 20));
}
- (BOOL)application:(UIApplication *)a didFinishLaunchingWithOptions:(NSDictionary *)o { (void)a; (void)o; return YES; }
@end

static void env_said(const char *name, const char *value) { plog("boot: env.txt: %s=%s", name, value); }
int main(int argc, char *argv[]) {
    strlcpy(g_docs, docs_dir().fileSystemRepresentation, sizeof g_docs);
    char last[512]; int intr = rundiag_launch(g_docs, last, sizeof last);   // before this session's log is opened
    log_open();
    // Switches without a debugger (host/envfile.h): the bundle's dm/env.txt (ios/run.sh DM_ENV_FILE), then Documents/env.txt (Files app) over it
    { NSString *b = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"dm/env.txt"], *d = [docs_dir() stringByAppendingPathComponent:@"env.txt"];
      int nb = dm_env_file_apply(b.fileSystemRepresentation, env_said), nd = dm_env_file_apply(d.fileSystemRepresentation, env_said);
      if (nb >= 0 || nd >= 0) plog("boot: env.txt: %d switch(es) from the bundle, %d from Documents", nb > 0 ? nb : 0, nd > 0 ? nd : 0); }
    logstamp_stderr();   // UTC milliseconds on every line from here on (after env.txt, which can say DM_LOG_TIME=0)
    if (getenv("DM_CLEAR_SHADER_CACHE") && atoi(getenv("DM_CLEAR_SHADER_CACHE"))) {   // a fresh install's empty Metal shader cache (host/shadercache.h, plan item 257)
        NSString *c = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:NSBundle.mainBundle.bundleIdentifier];
        int r = shader_cache_clear(c.fileSystemRepresentation); plog("%sboot: DM_CLEAR_SHADER_CACHE: %d Metal cache directories removed%s", r < 0 ? "[red] " : "", r < 0 ? 0 : r, r < 0 ? " (one could not be)" : "");
    }
    // The guest address space before UIKit and Metal map theirs (plan item 62a: the largest free gap shrinks once they have; after env.txt, which can
    // say DM_GUEST_AS_GB)
    { void svc_reserve_early(void); svc_reserve_early(); }
    if (intr) {
        snprintf(g_launch_note, sizeof g_launch_note, intr > 0 ? "the last session (%s) ended in the foreground without an orderly exit (killed for memory, a crash, or a "
                 "hang the system ended): its log is kept as " RUNDIAG_KEPT : "the last session ended in the foreground without an orderly exit, but its log was not kept: %s", last);
        plog("[red] boot: %s", g_launch_note);
    }
    // The default soft limit of 256 descriptors is about what a game keeps open; raise it (not above the hard limit)
    { struct rlimit rl; if (!getrlimit(RLIMIT_NOFILE, &rl)) { rlim_t was = rl.rlim_cur, want = 4096; if (rl.rlim_max != RLIM_INFINITY && want > rl.rlim_max) want = rl.rlim_max;
        if (was < want) { rl.rlim_cur = want; int rc = setrlimit(RLIMIT_NOFILE, &rl); plog("boot: descriptor soft limit %llu → %llu%s", (unsigned long long)was, (unsigned long long)want, rc ? " failed" : ""); } } }
    { char hw[64] = "?"; size_t hn = sizeof hw; sysctlbyname("hw.machine", hw, &hn, NULL, 0);
      NSDictionary *inf = NSBundle.mainBundle.infoDictionary; NSDateFormatter *df = [NSDateFormatter new]; df.dateFormat = @"yyyy-MM-dd HH:mm:ss ZZZ";
      id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
      plog("boot: %s %s, app %s (%s), %s, %s, GPU %s (BC textures %s), %s", dm_game.title, dm_game.version, [inf[@"CFBundleShortVersionString"] description].UTF8String ?: "?",
           [inf[@"CFBundleVersion"] description].UTF8String ?: "?", hw, NSProcessInfo.processInfo.operatingSystemVersionString.UTF8String, dev.name.UTF8String,
           dev.supportsBCTextureCompression ? "yes" : "no", [df stringFromDate:NSDate.date].UTF8String); }
    // The memory limit this launch got (footprint + what is left, as host/diag/memstat.m reads it) and whether the signer kept the increased memory
    // limit (ns2ios-shell 8160029's boot line, MIT, written anew): signing tools differ (ios/README.md), and the limit alone does not say why it is low
    { struct task_vm_info vi; mach_msg_type_number_t vn = TASK_VM_INFO_COUNT;
      uint64_t fp = task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &vn) == KERN_SUCCESS ? vi.phys_footprint : 0, av = os_proc_available_memory();
      NSData *exe = [NSData dataWithContentsOfFile:NSBundle.mainBundle.executablePath options:NSDataReadingMappedIfSafe error:nil];
      int iml = exe ? dm_codesig_bool(exe.bytes, exe.length, "com.apple.developer.kernel.increased-memory-limit") : -1;
      plog("boot: memory limit %llu MB (footprint %llu MB + %llu MB available), RAM %llu MB; the increased memory limit in the app's signature: %s", (unsigned long long)((fp + av) >> 20),
           (unsigned long long)(fp >> 20), (unsigned long long)(av >> 20), (unsigned long long)(NSProcessInfo.processInfo.physicalMemory >> 20),
           iml > 0 ? "yes" : iml == 0 ? "no (the signer left it out)" : "no entitlements found"); }
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(App.class)); }
}
