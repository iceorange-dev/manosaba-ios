// Live command channel (plan item 47, after MacNativeGames' TOTK_INPUT_FILE in games/totk/runtime/input.c, ly05010419/MacNativeGames, MIT):
// DM_CMD=<file> starts a host thread that follows the file from its start as lines are appended (created when missing, polled every 50 ms) and
// runs each complete line while the game runs, so a test or a person can look at each frame before choosing the next step instead of fixing
// times in advance (DM_KEYS). A line without its newline yet (a writer between two writes) waits for the newline. Commands:
//   key <name> [ms]      press a key for ms (default 200, 1-5000); names as in DM_KEYS: A B X Y LS RS L R ZL ZR PLUS MINUS LEFT UP RIGHT DOWN,
//                        LSL LSR LSU LSD (the left stick pushed fully)
//   down <name>          hold a key until "up <name>"
//   up <name>
//   wait <frames>        run the next line once that many more frames have been presented (1-100000)
//   sleep <ms>           run the next line after ms (1-600000)
//   shot <path>          the next presented frame as a PNG (as DM_NVN_SHOT)
//   passdump <frames>    the render pass records of the next frames (as DM_NVN_PASSDUMP, 1-600)
//   texdump <path>       the colour targets of the next frame as PNGs (as DM_NVN_TEXDUMP; %d in the path is the target's index)
//                        ("texdump -": no files, the half-float targets' values at ±65504 / ±inf / NaN counted and logged only)
//   padtest <name>       a touch pad self-test (touchpad.c: check, look, hide) run now, as DM_TEST_TOUCHPAD runs it at a time
//   padshot <path>       the on-screen controls as they are drawn now, as a PNG (the iOS front end; plan item 315e)
//   threads              every guest thread's state now (as on a crash)
//   memsnap <path>       a memory snapshot now (as DM_MEMSNAP, memsnap.c)
//   hlecount             the host call counts now (DM_HLECOUNT's file)
//   heaps                every sead heap in the guest's heap and alias regions (sead.c)
//   heap <addr>          the sead heaps holding addr (0x...) and the block it is in
//   quit                 the orderly exit of SIGTERM (hle_lifecycle.c)
//   anything else the game takes (dm_game_command, host/game.h): Tears of the Kingdom's survey pos / survey move (games/totk/survey.c, DM_SURVEY=1)
//                        and scene <w>x<h> (the scene render size while it runs, games/totk/frame_step.c)
// Blank lines and lines starting with # are skipped; an unknown or malformed line is logged in red and skipped. Each line run is logged
// ("[cmd] ..."). A file replaced by another (a copy pushed to a device is a new inode: T5eng's TOTK_INPUT_REOPEN) or cut shorter than what was read
// is followed again from its start. Checked by host/tests/t_cmdchan.c.
#include "dm.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
uint32_t hid_key_mask(const char *name);                // hle_hid.c
void hid_cmd_keys(uint64_t down, uint64_t up);
void dm_gfx_shot_at(long k, const char *path);          // gfx/nvn/nvn_mtl.m
void mtl_passdump_next(long n);
void mtl_texdump_next(const char *path);
void svc_thread_dump(void);                             // hle_thread.c
long dm_memsnap_write(const char *path, long frame);    // memsnap.c
void hlecount_flush(void);                              // hle_count.c
void dm_host_shutdown(const char *why);                 // hle_lifecycle.c
void sead_heaps_print(void);                            // sead.c
void sead_heap_of(uint64_t addr);
int tp_selftest_now(const char *name);                  // touchpad.c
void (*dm_padshot_hook)(const char *path);              // the front end's on-screen controls as a PNG (front_ios.m sets it; none elsewhere)

enum { CMD_NONE, CMD_KEY, CMD_DOWN, CMD_UP, CMD_WAIT, CMD_SLEEP, CMD_SHOT, CMD_PASSDUMP, CMD_TEXDUMP, CMD_THREADS, CMD_MEMSNAP, CMD_HLECOUNT, CMD_QUIT, CMD_HEAPS, CMD_HEAP, CMD_PADTEST, CMD_PADSHOT, CMD_GAME };
int dm_game_command(const char *line, int run);   // host/game.h: the game's own commands (Tears of the Kingdom: survey, plan item 167)
#define CMD_LINE 1024
typedef struct { int kind; uint32_t mask; long n; char path[CMD_LINE]; } dm_cmd;

// One line (without its newline) → c; 0, or -1 with why set
static int cmd_parse(const char *line, dm_cmd *c, const char **why) {
    memset(c, 0, sizeof *c); *why = NULL;
    while (*line == ' ' || *line == '\t') line++;
    size_t len = strlen(line); while (len && (line[len - 1] == ' ' || line[len - 1] == '\t' || line[len - 1] == '\r')) len--;
    if (!len || *line == '#') return 0;
    char w[16] = ""; size_t k = 0; while (k < len && line[k] != ' ' && line[k] != '\t') k++;
    if (k >= sizeof w) { *why = "unknown command"; return -1; }
    memcpy(w, line, k); w[k] = 0;
    const char *a = line + k; while (a < line + len && (*a == ' ' || *a == '\t')) a++;
    char arg[CMD_LINE]; size_t an = (size_t)(line + len - a); if (an >= sizeof arg) { *why = "line too long"; return -1; }
    memcpy(arg, a, an); arg[an] = 0;
    static const struct { const char *name; int kind; } names[] = {{"key", CMD_KEY}, {"down", CMD_DOWN}, {"up", CMD_UP}, {"wait", CMD_WAIT},
        {"sleep", CMD_SLEEP}, {"shot", CMD_SHOT}, {"passdump", CMD_PASSDUMP}, {"texdump", CMD_TEXDUMP}, {"threads", CMD_THREADS}, {"memsnap", CMD_MEMSNAP},
        {"hlecount", CMD_HLECOUNT}, {"quit", CMD_QUIT}, {"heaps", CMD_HEAPS}, {"heap", CMD_HEAP}, {"padtest", CMD_PADTEST}, {"padshot", CMD_PADSHOT}};
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) if (!strcmp(w, names[i].name)) c->kind = names[i].kind;
    if (!c->kind) {   // the game's own (it checks the line now and logs what running it does)
        if (len < sizeof c->path) { memcpy(c->path, line, len); c->path[len] = 0; if (dm_game_command(c->path, 0)) { c->kind = CMD_GAME; return 0; } }
        *why = "unknown command"; return -1; }
    switch (c->kind) {
    case CMD_KEY: case CMD_DOWN: case CMD_UP: {
        char key[16], extra[2]; long ms = 200; int got = sscanf(arg, "%15s %ld %1s", key, &ms, extra);
        if (got < 1 || got > (c->kind == CMD_KEY ? 2 : 1)) { *why = c->kind == CMD_KEY ? "expected key <name> [ms]" : "expected a key name"; return -1; }
        if (!(c->mask = hid_key_mask(key))) { *why = "unknown key name"; return -1; }
        if (c->kind == CMD_KEY && (ms < 1 || ms > 5000)) { *why = "key time out of 1-5000 ms"; return -1; }
        c->n = c->kind == CMD_KEY ? ms : 0; return 0; }
    case CMD_WAIT: case CMD_SLEEP: case CMD_PASSDUMP: {
        long lo = 1, hi = c->kind == CMD_WAIT ? 100000 : c->kind == CMD_SLEEP ? 600000 : 600; char *end;
        errno = 0; c->n = strtol(arg, &end, 10);
        if (end == arg || *end || errno || c->n < lo || c->n > hi) { *why = c->kind == CMD_WAIT ? "wait takes 1-100000 frames" : c->kind == CMD_SLEEP ? "sleep takes 1-600000 ms" : "passdump takes 1-600 frames"; return -1; }
        return 0; }
    case CMD_HEAP: {
        char *end; errno = 0; c->n = (long)strtoull(arg, &end, 0);
        if (end == arg || *end || errno || !c->n) { *why = "heap takes an address"; return -1; }
        return 0; }
    case CMD_SHOT: case CMD_MEMSNAP: case CMD_TEXDUMP: case CMD_PADSHOT: case CMD_PADTEST:
        if (!*arg) { *why = c->kind == CMD_PADTEST ? "expected a test name" : "expected a path"; return -1; }
        strlcpy(c->path, arg, sizeof c->path); return 0;
    default:
        if (*arg) { *why = "takes no argument"; return -1; }
        return 0;
    }
}

static void cmd_run(const dm_cmd *c) {
    switch (c->kind) {
    case CMD_KEY: hid_cmd_keys(c->mask, 0); usleep((useconds_t)c->n * 1000); hid_cmd_keys(0, c->mask); break;
    case CMD_DOWN: hid_cmd_keys(c->mask, 0); break;
    case CMD_UP: hid_cmd_keys(0, c->mask); break;
    case CMD_WAIT: { uint64_t until = dm_nvn_frame_count() + (uint64_t)c->n; while (dm_nvn_frame_count() < until) usleep(5000); break; }
    case CMD_SLEEP: usleep((useconds_t)c->n * 1000); break;
    case CMD_SHOT: dm_gfx_shot_at(1, c->path); break;
    case CMD_PASSDUMP: mtl_passdump_next(c->n); break;
    case CMD_TEXDUMP: mtl_texdump_next(c->path); break;
    case CMD_THREADS: svc_thread_dump(); break;
    case CMD_MEMSNAP: { long f = (long)dm_nvn_frame_count(); long n = dm_memsnap_write(c->path, f);
        if (n < 0) LOG("[red] command channel: memsnap %s failed", c->path); else LOG("[cmd] memsnap: frame %ld, %ld bytes to %s", f, n, c->path); break; }
    case CMD_HLECOUNT: hlecount_flush(); break;
    case CMD_QUIT: dm_host_shutdown("command channel quit"); break;
    case CMD_HEAPS: sead_heaps_print(); break;
    case CMD_HEAP: sead_heap_of((uint64_t)c->n); break;
    case CMD_PADTEST: if (!tp_selftest_now(c->path)) LOG("[red] command channel: padtest %s: no such test (check, look, hide)", c->path); break;
    case CMD_PADSHOT: if (dm_padshot_hook) dm_padshot_hook(c->path); else LOG("[red] command channel: padshot %s: no on-screen controls in this front end", c->path); break;
    case CMD_GAME: dm_game_command(c->path, 1); break;
    }
}

// At the end of the file: 1 when path now names another file (*fd reopened on it) or one shorter than what was read (rewound)
static int cmd_reopen(int *fd, const char *path) {
#ifndef DM_TEST_CMD_NO_REOPEN   // negative control of t_cmdchan only: the first file followed for good
    struct stat now, open_; if (!path || stat(path, &now) || fstat(*fd, &open_)) return 0;
    if (now.st_ino != open_.st_ino || now.st_dev != open_.st_dev) { int nfd = open(path, O_RDONLY); if (nfd < 0) return 0; close(*fd); *fd = nfd; return 1; }
    if (now.st_size < lseek(*fd, 0, SEEK_CUR)) { lseek(*fd, 0, SEEK_SET); return 1; }
#else
    (void)fd; (void)path;
#endif
    return 0;
}
// Follows fd (path: to notice a replaced file, NULL not to) until a quit; returns the number of lines run (the unit test reads it); closes fd
static long cmd_loop(int fd, const char *path) {
    char buf[CMD_LINE + 1]; size_t n = 0; int skip = 0; long ran = 0;
    for (;;) {
        ssize_t r = read(fd, buf + n, CMD_LINE - n);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) { LOG("[red] command channel: read failed (%s), stopping", strerror(errno)); close(fd); return ran; }
        if (r == 0 && cmd_reopen(&fd, path)) { LOG("[cmd] the file was replaced or cut short: following it from its start"); n = 0; skip = 0; continue; }
        if (r == 0) {
#ifdef DM_TEST_CMD_PARTIAL   // negative control of t_cmdchan only: a line is run as far as it was written, newline or not
            if (n) { buf[n] = 0; dm_cmd c; const char *why; if (!cmd_parse(buf, &c, &why) && c.kind) { cmd_run(&c); ran++; if (c.kind == CMD_QUIT) { close(fd); return ran; } } else if (why) LOG("[red] command channel: %s: %s", why, buf); n = 0; }
#endif
            usleep(50000); continue;
        }
        n += (size_t)r;
        for (;;) {
            char *nl = memchr(buf, '\n', n);
            if (!nl) { if (n == CMD_LINE) { if (!skip) LOG("[red] command channel: a line longer than %d bytes, skipped", CMD_LINE - 1); skip = 1; n = 0; } break; }
            *nl = 0; size_t used = (size_t)(nl - buf) + 1;
            if (!skip) {
                dm_cmd c; const char *why;
                if (cmd_parse(buf, &c, &why)) LOG("[red] command channel: %s: %s", why, buf);
                else if (c.kind) { LOG("[cmd] %s", buf); cmd_run(&c); ran++; if (c.kind == CMD_QUIT) { close(fd); return ran; } }
            }
            skip = 0; memmove(buf, buf + used, n - used); n -= used;
        }
    }
}

static int g_cmd_fd; static const char *g_cmd_path;
static void *cmd_thread(void *arg) { (void)arg; cmd_loop(g_cmd_fd, g_cmd_path); return NULL; }
void dm_cmd_start(void) {
    const char *p = getenv("DM_CMD"); if (!p || !*p) return;
    int fd = open(p, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) { LOG("[red] DM_CMD: cannot open %s (%s)", p, strerror(errno)); return; }
    g_cmd_fd = fd; g_cmd_path = p;
    pthread_t t; if (pthread_create(&t, NULL, cmd_thread, NULL)) { LOG("[red] DM_CMD: cannot start its thread"); close(fd); return; }
    pthread_detach(t); LOG("command channel: following %s", p);
}
