// The Android app (plan item 301d): libdmhost.so in a NativeActivity without code of its own (android/tools/apk.py writes the APK). The system calls
// ANativeActivity_onCreate on the main thread; this file is the app's shell, as ios/Sources/main.m is the iPhone's:
//   Folders: the app's external files folder (Android/data/<package>/files, adb reaches it) holds the player's game data, RomFS/ and sysfont/
//   (android/run.sh push-data), env.txt (DM_ switches, host/envfile.h) and the log, boot.txt (the last session's kept as boot.previous.txt); the
//   internal one (files/) holds the save, temp, the product the APK carries (assets/product, copied out when the APK changed: the loader reads
//   files) and the settings panel's settings.txt (android/settings.c: env.txt wins over it); the cache folder the game's cache storage.
//   The settings panel (plan item 301g) is Java (android/java/.../Panel.java, the APK's classes.dex), loaded through the activity's class loader;
//   its two native methods are registered here.
//   The host starts on a thread of its own once the game data is there; when it is not, a dialog says what is missing and where it goes. An APK
//   built with the game's data (make apk-data: plan item 301h) has it copied out to the external folder first, on a thread of its own with its
//   progress on screen (data_apk.c, Progress.java).
//   front_android.c has the window, the lifecycle and input.
// The main thread runs the system's callbacks; other threads hand it work through dm_main_post (a pipe on its looper), for the Java calls that must
// happen there (a dialog, the system bars).
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/window.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include "dm.h"
#include "envfile.h"
#include "front.h"
#include "game.h"
#include "lang.h"
#include "logstamp.h"
#include "romfs_validate.h"
#include "settings.h"
#include "data_apk.h"

int dm_main(int argc, char **argv);   // host/runtime/main.c, built with -Dmain=dm_main
void front_android_install(ANativeActivity *a);   // front_android.c: the activity's callbacks
static ANativeActivity *g_act;
static char g_ext[512], g_int[512];
ANativeActivity *dm_activity(void) { return g_act; }

// ---------- the main thread's queue ----------
typedef struct { void (*fn)(void *); void *arg; } post_t;
static int g_post[2] = {-1, -1};   // non-blocking: on_post reads until the pipe is empty (a blocking read then held the main thread: an ANR)
static int on_post(int fd, int events, void *d) {
    (void)events; (void)d; post_t p;
    while (read(fd, &p, sizeof p) == (ssize_t)sizeof p) p.fn(p.arg);
    return 1;
}
void dm_main_post(void (*fn)(void *), void *arg) {
    post_t p = {fn, arg};
    if (g_post[1] < 0 || write(g_post[1], &p, sizeof p) != (ssize_t)sizeof p) LOG("[red] app: cannot hand work to the main thread");
}

// ---------- Java, on the main thread (activity->env belongs to it) ----------
static int jx(JNIEnv *E, const char *what) {
    if (!(*E)->ExceptionCheck(E)) return 0;
    (*E)->ExceptionDescribe(E); (*E)->ExceptionClear(E); LOG("[red] app: Java exception in %s", what); return 1;
}
static jobject jcall_obj(JNIEnv *E, jobject o, const char *name, const char *sig) {
    jclass c = (*E)->GetObjectClass(E, o); jmethodID m = (*E)->GetMethodID(E, c, name, sig);
    jobject r = m ? (*E)->CallObjectMethod(E, o, m) : NULL; (*E)->DeleteLocalRef(E, c);
    return jx(E, name) ? NULL : r;
}
static int g_main_tid;
static int on_main_thread(void) { return gettid() == g_main_tid; }
// A dialog with a title, a message and OK (it only closes): the game data missing, a save refused
typedef struct { char title[128], msg[1024]; } dialog_t;
static void dialog_now(void *p) {
    dialog_t *d = p; JNIEnv *E = g_act->env;
    jclass B = (*E)->FindClass(E, "android/app/AlertDialog$Builder");
    jmethodID init = (*E)->GetMethodID(E, B, "<init>", "(Landroid/content/Context;)V");
    jobject b = (*E)->NewObject(E, B, init, g_act->clazz);
    if (!jx(E, "AlertDialog.Builder") && b) {
        jmethodID st = (*E)->GetMethodID(E, B, "setTitle", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;");
        jmethodID sm = (*E)->GetMethodID(E, B, "setMessage", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;");
        jmethodID sp = (*E)->GetMethodID(E, B, "setPositiveButton", "(Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;");
        jmethodID sh = (*E)->GetMethodID(E, B, "show", "()Landroid/app/AlertDialog;");
        jstring t = (*E)->NewStringUTF(E, d->title), m = (*E)->NewStringUTF(E, d->msg), ok = (*E)->NewStringUTF(E, "OK");
        (*E)->DeleteLocalRef(E, (*E)->CallObjectMethod(E, b, st, t)); (*E)->DeleteLocalRef(E, (*E)->CallObjectMethod(E, b, sm, m));
        (*E)->DeleteLocalRef(E, (*E)->CallObjectMethod(E, b, sp, ok, NULL));
        if (!jx(E, "the dialog's text")) { (*E)->DeleteLocalRef(E, (*E)->CallObjectMethod(E, b, sh)); jx(E, "AlertDialog.show"); }
        (*E)->DeleteLocalRef(E, t); (*E)->DeleteLocalRef(E, m); (*E)->DeleteLocalRef(E, ok); (*E)->DeleteLocalRef(E, b);
    }
    (*E)->DeleteLocalRef(E, B); free(d);
}
void dm_dialog(const char *title, const char *msg) {   // any thread
    dialog_t *d = calloc(1, sizeof *d); if (!d) return;
    snprintf(d->title, sizeof d->title, "%s", title); snprintf(d->msg, sizeof d->msg, "%s", msg);
    LOG("app: dialog \"%s\": %s", title, msg);
    if (on_main_thread()) dialog_now(d); else dm_main_post(dialog_now, d);
}
// ---------- the settings panel (Java: android/java/io/github/iceorange_dev/dm/Panel.java) ----------
static jclass g_panel;   // a global reference, or NULL when the APK has no such class
static char g_settings[600];   // its file
int dm_perf_line(char *out, size_t n);   // gfx/nvn/hle/timing.inc
void front_android_settings_changed(void);   // front_android.c
static jboolean perf_on(void) { char v[8]; return dm_setting("DM_PERF_LINE", v, sizeof v) && !strcmp(v, "1"); }
static void panel_perf(JNIEnv *E) {
    if (!g_panel) return;
    (*E)->CallStaticVoidMethod(E, g_panel, (*E)->GetStaticMethodID(E, g_panel, "perf", "(Landroid/app/Activity;Z)V"), g_act->clazz, perf_on()); jx(E, "Panel.perf");
}
static void JNICALL j_applied(JNIEnv *E, jclass c) {   // (the main thread: a change in the panel)
    (void)c; dm_settings_reload(); front_android_settings_changed(); panel_perf(E);
}
static jstring JNICALL j_perf_line(JNIEnv *E, jclass c) { (void)c; char b[128]; dm_perf_line(b, sizeof b); return (*E)->NewStringUTF(E, b); }
static jclass app_class(JNIEnv *E, const char *name) {   // one of the APK's own classes (classes.dex) through the activity's class loader: a global reference
    jobject cl = jcall_obj(E, g_act->clazz, "getClassLoader", "()Ljava/lang/ClassLoader;"); if (!cl) return NULL;
    jclass CL = (*E)->FindClass(E, "java/lang/ClassLoader"); jstring nm = (*E)->NewStringUTF(E, name);
    jobject c = (*E)->CallObjectMethod(E, cl, (*E)->GetMethodID(E, CL, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;"), nm);
    jclass g = jx(E, name) || !c ? NULL : (*E)->NewGlobalRef(E, c);
    if (!g) LOG("[red] app: the APK has no %s (classes.dex)", name);
    (*E)->DeleteLocalRef(E, nm); (*E)->DeleteLocalRef(E, CL); (*E)->DeleteLocalRef(E, cl); if (c) (*E)->DeleteLocalRef(E, c);
    return g;
}
static jclass g_progress;   // Progress.java: the first start's copy of the game's data
static void panel_load(JNIEnv *E) {
    g_progress = app_class(E, "io.github.iceorange_dev.dm.Progress");
    if (!(g_panel = app_class(E, "io.github.iceorange_dev.dm.Panel"))) return;
    JNINativeMethod nat[] = {{"applied", "()V", (void *)j_applied}, {"perfLine", "()Ljava/lang/String;", (void *)j_perf_line}};
    if ((*E)->RegisterNatives(E, g_panel, nat, 2) || jx(E, "the settings panel's native methods")) { (*E)->DeleteGlobalRef(E, g_panel); g_panel = NULL; }
}
void dm_panel_open(void *unused) {   // the main thread (front_android.c posts it: Back, three fingers)
    (void)unused; if (!g_panel) return; JNIEnv *E = g_act->env;
    jstring p = (*E)->NewStringUTF(E, g_settings);
    (*E)->CallStaticVoidMethod(E, g_panel, (*E)->GetStaticMethodID(E, g_panel, "show", "(Landroid/app/Activity;Ljava/lang/String;)V"), g_act->clazz, p);
    if (!jx(E, "Panel.show")) LOG("app: settings panel");
    (*E)->DeleteLocalRef(E, p);
}
static int display_size(unsigned *w, unsigned *h) {   // the screen's pixels (Panel.displaySize), for the setting Native
    if (!g_panel) return 0; JNIEnv *E = g_act->env;
    jintArray a = (*E)->CallStaticObjectMethod(E, g_panel, (*E)->GetStaticMethodID(E, g_panel, "displaySize", "(Landroid/app/Activity;)[I"), g_act->clazz);
    if (jx(E, "Panel.displaySize") || !a) return 0;
    jint v[2]; (*E)->GetIntArrayRegion(E, a, 0, 2, v); (*E)->DeleteLocalRef(E, a);
    *w = (unsigned)v[0]; *h = (unsigned)v[1]; LOG("app: the screen %ux%u", *w, *h); return v[0] > 0 && v[1] > 0;
}

// The system bars hidden (WindowInsetsController, API 30), shown again for a while by a swipe from the edge
void dm_bars_hide(void *unused) {
    (void)unused; JNIEnv *E = g_act->env;
    jobject w = jcall_obj(E, g_act->clazz, "getWindow", "()Landroid/view/Window;"); if (!w) return;
    jobject ic = jcall_obj(E, w, "getInsetsController", "()Landroid/view/WindowInsetsController;");
    jclass T = (*E)->FindClass(E, "android/view/WindowInsets$Type"), C = (*E)->FindClass(E, "android/view/WindowInsetsController");
    if (ic && T && C && !jx(E, "the insets classes")) {
        jint bars = (*E)->CallStaticIntMethod(E, T, (*E)->GetStaticMethodID(E, T, "systemBars", "()I"));
        jfieldID f = (*E)->GetStaticFieldID(E, C, "BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE", "I");
        jint swipe = f ? (*E)->GetStaticIntField(E, C, f) : 0;
        if (!jx(E, "the insets values")) {
            (*E)->CallVoidMethod(E, ic, (*E)->GetMethodID(E, C, "setSystemBarsBehavior", "(I)V"), swipe);
            (*E)->CallVoidMethod(E, ic, (*E)->GetMethodID(E, C, "hide", "(I)V"), bars); jx(E, "WindowInsetsController.hide");
        }
    }
    if (T) (*E)->DeleteLocalRef(E, T); if (C) (*E)->DeleteLocalRef(E, C); if (ic) (*E)->DeleteLocalRef(E, ic); (*E)->DeleteLocalRef(E, w);
}
// ---------- log ----------
static void log_open(void) {
    char cur[600], prev[600]; snprintf(cur, sizeof cur, "%s/boot.txt", g_ext); snprintf(prev, sizeof prev, "%s/boot.previous.txt", g_ext);
    rename(cur, prev);
    int fd = open(cur, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return;   // (the log goes to logcat's stderr, nowhere)
    dup2(fd, 2); dup2(fd, 1); close(fd);
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *e = getenv("DM_LOG_TIME"); if (!e || strcmp(e, "0")) logstamp_stderr();
}
static void say_setting(const char *who, const char *n, const char *v) { LOG("app: %s %s=%s", who, n, v); }

// ---------- the product, from the APK ----------
// Copied to files/product when the APK it came from is another (its path, size and time: a new install has a new path); each file is written
// beside its place and renamed over it, the stamp last, so a copy cut short is made again at the next start
static int product_out(AAssetManager *am, const char *apk) {
    char dir[600], stamp[600], want[700], have[700] = ""; struct stat st;
    snprintf(dir, sizeof dir, "%s/product", g_int); snprintf(stamp, sizeof stamp, "%s/.from_apk", dir); mkdir(dir, 0700);
    if (stat(apk, &st)) { LOG("[red] app: cannot stat the APK %s: %s", apk, strerror(errno)); return -1; }
    snprintf(want, sizeof want, "%s %lld %lld", apk, (long long)st.st_size, (long long)st.st_mtim.tv_sec);
    FILE *f = fopen(stamp, "r"); if (f) { if (!fgets(have, sizeof have, f)) have[0] = 0; fclose(f); }
    if (!strcmp(want, have)) return 0;
    uint64_t t0 = now_ns(), bytes = 0; int n = 0;
    unlink(stamp);
    AAssetDir *d = AAssetManager_openDir(am, "product"); const char *name;
    while (d && (name = AAssetDir_getNextFileName(d))) {
        char src[300], dst[700], tmp[710]; snprintf(src, sizeof src, "product/%s", name); snprintf(dst, sizeof dst, "%s/%s", dir, name); snprintf(tmp, sizeof tmp, "%s.part", dst);
        AAsset *a = AAssetManager_open(am, src, AASSET_MODE_STREAMING); int o = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        int ok = a && o >= 0; char buf[1 << 16]; int r = 0;
        while (ok && (r = AAsset_read(a, buf, sizeof buf)) > 0) { for (int w = 0; ok && w < r;) { ssize_t k = write(o, buf + w, (size_t)(r - w)); if (k > 0) w += (int)k; else ok = 0; } bytes += (uint64_t)r; }
        if (r < 0) ok = 0;
        if (o >= 0 && (fsync(o) || close(o))) ok = 0;
        if (a) AAsset_close(a);
        if (!ok || rename(tmp, dst)) { LOG("[red] app: copying %s out of the APK failed: %s", src, strerror(errno)); AAssetDir_close(d); return -1; }
        n++;
    }
    if (d) AAssetDir_close(d);
    if (!n) { LOG("[red] app: the APK carries no product (assets/product)"); return -1; }
    f = fopen(stamp, "w"); if (f) { fputs(want, f); fclose(f); }
    LOG("app: the product copied out of the APK: %d files, %.1f MB in %.0f ms", n, bytes / 1e6, (now_ns() - t0) / 1e6);
    return 0;
}

// ---------- the host ----------
static char *g_argv[16]; static int g_argc;
static void *host_main(void *a) {
    (void)a; int rc = dm_main(g_argc, g_argv);
    LOG("app: the host returned %d: the game has ended", rc); _exit(rc);
}
static void mkdirs(const char *p) {
    char b[600]; snprintf(b, sizeof b, "%s", p);
    for (char *s = b + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(b, 0700); *s = '/'; }
    mkdir(b, 0700);
}
// The rest of the start, once the game's data is in place (at once, or after data_main): its check, the product, the host
static AAssetManager *g_am; static char g_romfs[600], g_font[600], g_apk[600];
static void boot_start(void *unused) {
    (void)unused;
    { struct stat st; if (!stat(g_font, &st) && S_ISDIR(st.st_mode)) setenv("DM_SYSFONT", g_font, 0); }
    char why[512];
    if (!dm_romfs_validate(getenv("DM_ROMFS"), why, sizeof why)) {
        char msg[1024]; snprintf(msg, sizeof msg, "%s\n\nCopy the game's RomFS folder (your own dump) to\n%s\nand the console's fonts to\n%s\n(android/run.sh push-data does both), then start the game again.", why, g_romfs, g_font);
        LOG("[red] app: the game data is missing: %s", why); dm_dialog("Game data missing", msg);
        return;
    }
    if (!g_apk[0] || product_out(g_am, g_apk)) { dm_dialog("Cannot start", "The game's code could not be copied out of the app. See boot.txt."); return; }
    { char sh[600]; snprintf(sh, sizeof sh, "%s/product/shaders.dmvk", g_int); setenv("DM_VK_SHADERS", sh, 0); }   // the Vulkan backend's shaders (gfx/nvn/vk/vk_shader.c)
    LOG("app: RomFS %s, fonts %s, save %s", getenv("DM_ROMFS"), getenv("DM_SYSFONT") ? getenv("DM_SYSFONT") : "(none)", getenv("DM_SAVE"));
    {   // dmhost <product> <modules...>, as the iOS app's arguments
        static char prod[600]; snprintf(prod, sizeof prod, "%s/product", g_int);
        g_argv[g_argc++] = "dmhost"; g_argv[g_argc++] = prod;
        char *mods = strdup(dm_game.modules);
        for (char *sv, *m = strtok_r(mods, " ", &sv); m && g_argc < 15; m = strtok_r(NULL, " ", &sv)) g_argv[g_argc++] = m;
        pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 8u << 20); pthread_t th;
        if (pthread_create(&th, &at, host_main, NULL)) LOG("[red] app: cannot start the host thread"); else pthread_detach(th);
        pthread_attr_destroy(&at);
    }
}
// The first start's copy of the game's data (data_apk.c) on a thread of its own, its progress on screen (Progress.java on the main thread)
static struct { uint64_t total, base; _Atomic uint64_t shown; } g_data;
static void progress_show(void) {
    if (!g_progress) return; JNIEnv *E = g_act->env;
    jstring t = (*E)->NewStringUTF(E, "Preparing the game"), m = (*E)->NewStringUTF(E, "Copying the game's data out of the app (first start)...");
    (*E)->CallStaticVoidMethod(E, g_progress, (*E)->GetStaticMethodID(E, g_progress, "show", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;)V"), g_act->clazz, t, m);
    jx(E, "Progress.show"); (*E)->DeleteLocalRef(E, t); (*E)->DeleteLocalRef(E, m);
}
static void progress_done(void) { if (g_progress) { JNIEnv *E = g_act->env; (*E)->CallStaticVoidMethod(E, g_progress, (*E)->GetStaticMethodID(E, g_progress, "done", "()V")); jx(E, "Progress.done"); } }
static void progress_ui(void *p) {   // (the main thread)
    uint64_t done = (uint64_t)(uintptr_t)p; if (!g_progress) return; JNIEnv *E = g_act->env; char b[160];
    snprintf(b, sizeof b, "Copying the game's data out of the app (first start): %.0f of %.0f MB", done / 1e6, g_data.total / 1e6);
    jstring m = (*E)->NewStringUTF(E, b);
    (*E)->CallStaticVoidMethod(E, g_progress, (*E)->GetStaticMethodID(E, g_progress, "update", "(Ljava/lang/String;I)V"), m, (jint)(g_data.total ? done * 1000 / g_data.total : 0));
    jx(E, "Progress.update"); (*E)->DeleteLocalRef(E, m);
}
static void on_copied(uint64_t done, uint64_t total, void *ctx) {   // (data_main's thread) at most one update each 32 MB
    (void)total; (void)ctx; uint64_t all = g_data.base + done;
    if (all - atomic_load(&g_data.shown) < (32u << 20) && all < g_data.total) return;
    atomic_store(&g_data.shown, all); dm_main_post(progress_ui, (void *)(uintptr_t)all);
}
static void data_finished(void *rc) {   // (the main thread)
    progress_done();
    if (rc) { dm_dialog("Cannot start", "The game's data could not be copied out of the app (is the phone's storage full?). See boot.txt."); return; }
    boot_start(NULL);
}
static void *data_main(void *unused) {
    (void)unused; uint64_t t0 = now_ns(); int64_t nr = dm_data_need(g_am, "romfs", g_romfs); int rc = 0;
    if (nr > 0) { g_data.base = 0; rc = dm_data_out(g_am, "romfs", g_romfs, on_copied, NULL); }
    if (!rc && dm_data_need(g_am, "sysfont", g_font) > 0) { g_data.base = nr > 0 ? (uint64_t)nr : 0; rc = dm_data_out(g_am, "sysfont", g_font, on_copied, NULL); }
    LOG("app: the game's data copied out in %.1f s%s", (now_ns() - t0) / 1e9, rc ? " [red] (failed)" : "");
    dm_main_post(data_finished, (void *)(intptr_t)rc);
    return NULL;
}
static void boot(ANativeActivity *a) {
    char model[PROP_VALUE_MAX] = "", rel[PROP_VALUE_MAX] = "", sdk[PROP_VALUE_MAX] = "";
    __system_property_get("ro.product.model", model); __system_property_get("ro.build.version.release", rel); __system_property_get("ro.build.version.sdk", sdk);
    JNIEnv *E = a->env;
    jobject jp = jcall_obj(E, a->clazz, "getPackageCodePath", "()Ljava/lang/String;"), jn = jcall_obj(E, a->clazz, "getPackageName", "()Ljava/lang/String;");
    const char *apk = jp ? (*E)->GetStringUTFChars(E, jp, NULL) : NULL, *pkg = jn ? (*E)->GetStringUTFChars(E, jn, NULL) : NULL;
    LOG("app: %s %s, %s (Android %s, SDK %s); package %s, %s", dm_game.id, dm_game.version, model, rel, sdk, pkg ? pkg : "?", apk ? apk : "?");
    { char p[600]; snprintf(p, sizeof p, "%s/env.txt", g_ext); snprintf(g_settings, sizeof g_settings, "%s/settings.txt", g_int);
      dm_settings_boot(g_settings, p, display_size, say_setting); }   // env.txt over the panel's settings (android/settings.c)
    // DM_TEST_SHOW_WHEN_LOCKED=1 (unattended tests on the phone): the activity shows over the lock screen and turns the screen on (Activity.
    // setShowWhenLocked / setTurnScreenOn, API 27); the phone stays locked, nothing else of it opens. Never on by default. HyperOS also wants its
    // own app op for it (MIUIOP 10020, the phone owner's "show on lock screen" setting): while it is off the app is held behind the lock screen
    { const char *e = getenv("DM_TEST_SHOW_WHEN_LOCKED"); if (e && atoi(e)) {
        jclass C = (*E)->GetObjectClass(E, a->clazz);
        jmethodID sw = (*E)->GetMethodID(E, C, "setShowWhenLocked", "(Z)V"), ts = (*E)->GetMethodID(E, C, "setTurnScreenOn", "(Z)V");
        if (sw && ts) { (*E)->CallVoidMethod(E, a->clazz, sw, JNI_TRUE); (*E)->CallVoidMethod(E, a->clazz, ts, JNI_TRUE); }
        LOG("app: DM_TEST_SHOW_WHEN_LOCKED: shown over the lock screen%s", jx(E, "setShowWhenLocked") ? " [red] (refused)" : "");
        (*E)->DeleteLocalRef(E, C); } }
    // the folders
    char romfs[600], font[600], save[600], temp[600], cache[600];
    snprintf(romfs, sizeof romfs, "%s/RomFS", g_ext); snprintf(font, sizeof font, "%s/sysfont", g_ext);
    snprintf(save, sizeof save, "%s/save", g_int); snprintf(temp, sizeof temp, "%s/temp", g_int);
    snprintf(cache, sizeof cache, "%s", g_int); { char *s = strrchr(cache, '/'); if (s) snprintf(s, sizeof cache - (size_t)(s - cache), "/cache/cache_storage"); }
    mkdirs(save); mkdirs(temp); mkdirs(cache);
    setenv("DM_ROMFS", romfs, 0); setenv("DM_SAVE", save, 0); setenv("DM_TEMP", temp, 0); setenv("DM_CACHE", cache, 0);
    { struct stat st; if (!stat(font, &st) && S_ISDIR(st.st_mode)) setenv("DM_SYSFONT", font, 0); }
    { char m[600]; struct stat st; snprintf(m, sizeof m, "%s/mods", g_ext); if (!stat(m, &st) && S_ISDIR(st.st_mode)) setenv("DM_MODS", m, 0); }
    setenv("DM_AUDIO", "1", 0);   // the app plays sound (the adb shell's host plays to the silent sink unless asked)
    if (!getenv("DM_LANG")) {   // the device's language (Android gives the language and the region here; lang.h takes TW / HK / MO as Traditional)
        AConfiguration *c = AConfiguration_new(); AConfiguration_fromAssetManager(c, a->assetManager);
        char l[3] = "", r[3] = "", tag[8]; AConfiguration_getLanguage(c, l); AConfiguration_getCountry(c, r); AConfiguration_delete(c);
        snprintf(tag, sizeof tag, "%.2s%s%.2s", l, *r ? "-" : "", r);
        const char *code = dm_lang_pick(tag, dm_game.languages); if (code) setenv("DM_LANG", code, 1);
        LOG("app: language %s (the device's %s; the game's: %s)", code ? code : dm_game.language, tag, dm_game.languages);
    }
    if (chdir(g_ext)) LOG("[red] app: chdir %s: %s", g_ext, strerror(errno));   // relative paths in DM_ switches land beside the log
    snprintf(g_romfs, sizeof g_romfs, "%s", romfs); snprintf(g_font, sizeof g_font, "%s", font); snprintf(g_apk, sizeof g_apk, "%s", apk ? apk : ""); g_am = a->assetManager;
    {   // the game's data the APK carries (make apk-data), copied out first when the external folder does not hold it yet
        int64_t nr = dm_data_need(g_am, "romfs", g_romfs), nf = dm_data_need(g_am, "sysfont", g_font);
        if (nr > 0 || nf > 0) {
            g_data.total = (uint64_t)(nr > 0 ? nr : 0) + (uint64_t)(nf > 0 ? nf : 0);
            LOG("app: the APK carries the game's data: %.1f MB to copy out (first start)", g_data.total / 1e6);
            progress_show();
            pthread_t th; if (pthread_create(&th, NULL, data_main, NULL)) { LOG("[red] app: cannot start the data copy"); progress_done(); } else pthread_detach(th);
            goto out;
        }
    }
    boot_start(NULL);
out:
    if (apk) (*E)->ReleaseStringUTFChars(E, jp, apk); if (pkg) (*E)->ReleaseStringUTFChars(E, jn, pkg);
}

JNIEXPORT void ANativeActivity_onCreate(ANativeActivity *a, void *saved, size_t saved_size) {
    (void)saved; (void)saved_size;
    g_act = a; front_android_install(a);
    static int started; if (started) { LOG("app: the activity was made again; the host goes on"); return; }
    started = 1; g_main_tid = gettid();
    snprintf(g_ext, sizeof g_ext, "%s", a->externalDataPath ? a->externalDataPath : a->internalDataPath);
    snprintf(g_int, sizeof g_int, "%s", a->internalDataPath);
    mkdirs(g_ext); mkdirs(g_int);
    log_open();
    if (pipe2(g_post, O_CLOEXEC | O_NONBLOCK) == 0) ALooper_addFd(ALooper_forThread(), g_post[0], ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, on_post, NULL);
    ANativeActivity_setWindowFlags(a, AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_FULLSCREEN, 0);
    dm_bars_hide(NULL);
    panel_load(a->env);
    boot(a);
    panel_perf(a->env);   // the frame rate line, when it is on
}
