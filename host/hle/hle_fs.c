// nn::fs: on the real device it goes through the fsp-srv service; the host connects directly to the Mac file system. Mount name → host directory:
//   rom   → $DM_ROMFS (read-only; DM_ROMFS_OVERLAY layers take priority, see host_path)
//   save  → $DM_SAVE (must be set; transactional: the game works in <save>.work, Commit swaps it in atomically)
//   temp  → $DM_TEMP (default $DM_ROOT/run/platform/temp)
//   cache storage <index> → $DM_CACHE/<index> (default $DM_ROOT/run/platform/cache; not transactional, see MountCacheStorage)
// Paths look like "rom:/data/x". FileHandle / DirectoryHandle are 8 bytes passed by value, here they store the host fd / DIR*.
// Struct layout follows the public Horizon ABI: DirectoryEntry = {char name[0x301], pad[3], s8 type, pad[3], s64 size} (0x310 bytes).
#include "hle.h"
#include "game.h"
#include "front.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdatomic.h>
#include "plat.h"
#include <sys/xattr.h>
#include <sys/statvfs.h>

#define RES_PATH_NOT_FOUND 0x202u      // fs module 2, description 1
#define RES_PATH_EXISTS    0x402u      // fs module 2, description 2
#define RES_TARGET_NOT_FOUND 0x7d402u  // fs module 2, description 1002 (SDK API reference: ResultTargetNotFound)
enum { MODE_READ = 1, MODE_WRITE = 2, MODE_APPEND = 4 };
enum { ENT_DIR = 0, ENT_FILE = 1 };
enum { DIRMODE_DIR = 1, DIRMODE_FILE = 2 };

typedef struct { char name[0x301]; uint8_t pad0[3]; int8_t type; uint8_t pad1[3]; int64_t size; } DirectoryEntry;
_Static_assert(sizeof(DirectoryEntry) == 0x310, "DirectoryEntry layout");

#define MAX_MOUNT 16
// Save transactions (HLE audit items 1–3): on the real device a save can be rolled back as a whole before commit, and after a crash / kill a remount sees only the version from the last Commit.
// Host: at mount, the committed directory commit (= $DM_SAVE) is clonefile'd as a whole tree into the working directory <commit>.work (APFS copy-on-write, nearly zero cost), and the mount points at the working directory,
// all game reads and writes happen in the working directory; Commit = clonefile(work → <commit>.new), then renamex_np(RENAME_SWAP) swaps it atomically with commit, then the swapped-out old version is deleted.
// Killed at any moment, commit is either the old version or the new version, never half-written / a mix of old and new; the next mount discards leftover .work / .new.
// fixed: the contents never change while the process runs (RomFS), so directory name indexes may be kept (path_component)
typedef struct { char name[32]; char dir[1024]; char overlay[4096]; char commit[1024]; int ro, fixed; } mount_t;
static mount_t g_mnt[MAX_MOUNT];   // commit non-empty = transactional save mount
static int g_nmnt;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_path_lock = PTHREAD_MUTEX_INITIALIZER;
static void path_unlock(pthread_mutex_t **m) { pthread_mutex_unlock(*m); }
#define PATH_LOCK() pthread_mutex_t *path_guard __attribute__((cleanup(path_unlock))) = &g_path_lock; pthread_mutex_lock(path_guard)
static uint8_t g_fmode[65536];              // fd → open mode
static uint8_t g_fsave[65536];              // fd → whether it is on a transactional save mount (the save write injection point only counts these)
static _Atomic int g_save_writes;           // number of writes on the save mount (shared by the injection point and the commit log)
static _Atomic uint64_t g_txn_first_ns;     // time of this transaction's first save write (used by the commit log to report timing; cleared to 0 at commit)
// Write failures are refused per transaction: on the real device save space is reserved when the save is created, overwrites cannot fail, and the game does not check the return values of WriteFile / SetFileSize either, it Commits as usual after a failure
// (after injecting a failure on the 1st write, the committed joe.d2s is 0 bytes = character lost). When a host write fails (disk full etc., really possible on iOS):
// if any save write / set-size failed between the last Commit and this Commit, this Commit is refused -- the committed version is unchanged, the working directory is discarded and re-cloned from the committed version,
// and the front end is notified (front_save_failed); the next transaction (the game's next full save rewrite) is attempted as usual and committed if it succeeds. g_save_poison = errno of the first failure in this transaction (0 = none).
static _Atomic int g_save_poison;
static void save_poison(int e) { int z = 0; atomic_compare_exchange_strong(&g_save_poison, &z, e ? e : EIO); }
// Quitting while the game is saving (MacNativeGames' frontend waits up to 15 s, MIT): a save transaction with writes and no commit yet is lost by an
// exit (consistently: the last commit stays). hle_lifecycle.c quit_wait waits while this says one is pending
int dm_fs_save_pending(void) { return atomic_load(&g_txn_first_ns) != 0; }
void dm_fs_save_poison(int e) { save_poison(e); }   // game.h: a side file written by the pre-commit hook that failed to write also counts as a write failure of this transaction -- if the commit went ahead as usual, the items would no longer be in the character save and would not be in the side file either
// The default weak implementation only logs (Mac); the iOS front end provides a strong implementation of the same name that shows a native alert. Called on the game thread, the front end switches to the main thread itself
__attribute__((weak)) void front_save_failed(int err) { LOG("front_save_failed(errno=%d): no front end handler (weak default)", err); }
// DM_FSLOG=<file>: each ReadFile logs one TSV line "seconds since boot\tguest path\toffset\tlength\tguest buffer address" (which bytes boot read, for offline reverse lookup against the manifest)
static FILE *g_fslog; static char *g_fdpath[4096]; static pthread_mutex_t g_fslog_lock = PTHREAD_MUTEX_INITIALIZER;
static void fslog_init(void) { static int done; if (done) return; done = 1; const char *e = getenv("DM_FSLOG"); if (e && !(g_fslog = fopen(e, "w"))) die("DM_FSLOG: cannot open %s", e); }

static const char *envdir(const char *var, const char *rel, char *buf, size_t n) {
    const char *v = getenv(var);
    if (v) return v;
    snprintf(buf, n, "%s%s/%s", getenv("DM_ROOT") ?: getenv("HOME"), getenv("DM_ROOT") ? "" : "/dm-work", rel); return buf;
}

// Test injection DM_TEST_FS_FAILAT=<point>[:<count>][,...]: the steps of a save transaction that end with a failed write (mountclone = clone the
// work directory at mount, reclone = re-clone after a refused commit, guestname = write the guest name after a rename, renameback = undo the
// rename) fail with ENOSPC for the first <count> times (default 1), to check "no exit, committed version unchanged, the next commit works"
// (tests/t_fs.c section 6). (This and the full-disk handling below are from upstream d2r-mobile b94670c, MIT.)
static int dm_fs_test_fail(const char *pt) {
    static char spec[256]; static int cnt[8]; const char *e = getenv("DM_TEST_FS_FAILAT"); if (!e || !e[0]) return 0;
    if (strcmp(spec, e)) { strlcpy(spec, e, sizeof spec); memset(cnt, 0, sizeof cnt); }   // a new set of injections counts from zero
    size_t L = strlen(pt); int k = 0;
    for (const char *q = spec; *q && k < 8; k++) { const char *end = strchr(q, ','); size_t n = end ? (size_t)(end - q) : strlen(q);
        if (n >= L && !strncmp(q, pt, L) && (n == L || q[L] == ':')) { int lim = n > L ? atoi(q + L + 1) : 1; if (cnt[k] < lim) { cnt[k]++; errno = ENOSPC; LOG("DM_TEST_FS_FAILAT: %s fails with ENOSPC (time %d)", pt, cnt[k]); return 1; } return 0; }
        q = end ? end + 1 : q + n; }
    return 0;
}
static int in_txn_dir(const char *hp) { for (int i = 0; i < g_nmnt; i++) { size_t n = strlen(g_mnt[i].dir); if (g_mnt[i].commit[0] && !strncmp(hp, g_mnt[i].dir, n) && (hp[n] == '/' || !hp[n])) return 1; } return 0; }
static int rm_tree(const char *p);
static Result mount_ex(const char *name, const char *dir, int ro, int txn) {
    PATH_LOCK(); pthread_mutex_lock(&g_lock);
    for (int i = 0; i < g_nmnt; i++)
        if (!strcmp(g_mnt[i].name, name)) { pthread_mutex_unlock(&g_lock); return RES_OK; }
    if (g_nmnt >= MAX_MOUNT) die("mount table full");
    strlcpy(g_mnt[g_nmnt].name, name, sizeof g_mnt[0].name);
    g_mnt[g_nmnt].ro = ro; g_mnt[g_nmnt].fixed = 0; g_mnt[g_nmnt].overlay[0] = 0; g_mnt[g_nmnt].commit[0] = 0;
    if (!strcmp(name, "rom") && getenv("DM_ROMFS_OVERLAY"))
        strlcpy(g_mnt[g_nmnt].overlay, getenv("DM_ROMFS_OVERLAY"), sizeof g_mnt[0].overlay);
    // The transaction's work directory cannot be set up (leftovers that cannot be removed, a failed clone: real on a full disk): do not exit; mount
    // the committed directory read-only instead. The save reads as usual, writes fail, commits are no-ops, the committed version stays intact
    int txn_err = 0;
    if (txn) {
        char work[1100], neu[1100]; snprintf(work, sizeof work, "%s.work", dir); snprintf(neu, sizeof neu, "%s.new", dir);
        mkdir(dir, 0755);
        const char *why = NULL;
        if (!access(work, F_OK) && rm_tree(work)) why = "cannot remove the leftover working directory";
        else if (!access(neu, F_OK) && rm_tree(neu)) why = "cannot remove the leftover .new";
        else if (dm_fs_test_fail("mountclone") || dm_clone_tree(dir, work)) why = "cloning the working directory failed";
        if (why) { txn_err = errno ? errno : EIO; g_mnt[g_nmnt].ro = 1; strlcpy(g_mnt[g_nmnt].dir, dir, sizeof g_mnt[0].dir);
            LOG("[red] save transaction: %s (%s, errno=%d): mounting the committed directory %s read-only; this session's saves are not written, the committed save is unchanged", why, work, txn_err, dir); }
        else {
            strlcpy(g_mnt[g_nmnt].commit, dir, sizeof g_mnt[0].commit);
            atomic_store(&g_save_poison, 0);                                // the working directory was just cloned from the committed version, intact
            strlcpy(g_mnt[g_nmnt].dir, work, sizeof g_mnt[0].dir); }
    } else strlcpy(g_mnt[g_nmnt].dir, dir, sizeof g_mnt[0].dir);
    g_nmnt++;
    pthread_mutex_unlock(&g_lock);
    if (txn_err) front_save_failed(txn_err);
    if (!ro && !txn) mkdir(dir, 0755);
    LOG("fs mount %s: → %s%s%s", name, txn ? g_mnt[g_nmnt - 1].dir : dir, ro || txn_err ? " (read-only)" : txn ? " (transactional save: working directory, Commit swaps it in atomically)" : "", g_mnt[g_nmnt - 1].overlay[0] ? " (overlay first)" : "");
    return RES_OK;
}
static Result mount(const char *name, const char *dir, int ro) { return mount_ex(name, dir, ro, 0); }

// APFS is case-insensitive by default, Horizon paths are case-sensitive: every component is looked up exactly by guest name.
// Ordinary host names are unchanged; only when a name collides on the host is a separate physical name chosen, with the guest name stored in an xattr.
// clonefile preserves xattr, so save Commit/remount needs no separate mapping table; enumeration must not leak physical aliases.
#define FS_GUEST_NAME "org.dm.guest-name"
static int guest_name(const char *parent, const char *physical, char *out, size_t n) {
    // Only the physical names chosen on a collision (.dm-case-N) carry the xattr: set_guest_name writes it only when the physical name differs from
    // the guest name and removes it otherwise. Any other physical name is the guest name: no getxattr per entry (one system call per entry while
    // listing or looking up; seconds for a large RomFS). (From seedds/nxnative 76ea922, MIT.)
    if (strncmp(physical, ".dm-case-", 9)) { strlcpy(out, physical, n); return 1; }
    char p[1280]; snprintf(p, sizeof p, "%s/%s", parent, physical);
    ssize_t k = dm_xattr_get(p, FS_GUEST_NAME, out, n - 1);
    if (k >= 0) { out[k] = 0; return 1; }
    if (errno != DM_ENOATTR) return 0;
    strlcpy(out, physical, n); return 1;
}
// Name index of the directories of mounts whose contents never change (mount_t.fixed: RomFS): guest name → physical name, sorted by guest name,
// built at a directory's first lookup and kept for the process. The entry-by-entry scan below reads the whole parent directory for every path
// component. (From seedds/nxnative 1b70789, MIT, where Engage's first frame went from 21 s to 6 s; nxnative indexes every read-only mount, but
// here MountSaveDataReadOnly is read-only too and its directory changes with each Commit, so only RomFS is indexed.) Callers hold PATH_LOCK.
typedef struct { const char *guest, *phys; } name_ent;
typedef struct dir_index { struct dir_index *next; uint64_t hash; char *path; size_t n; name_ent *e; char *pool; } dir_index;
#define DIR_INDEX_SLOTS 8192
static dir_index *g_dir_index[DIR_INDEX_SLOTS];
static uint64_t fnv1a(const char *s) { uint64_t h = 1469598103934665603ull; while (*s) { h ^= (uint8_t)*s++; h *= 1099511628211ull; } return h; }
static int name_ent_cmp(const void *a, const void *b) { return strcmp(((const name_ent *)a)->guest, ((const name_ent *)b)->guest); }
// NULL when it cannot be built (the directory cannot be opened, reading an entry's name fails): the caller then scans as before
static const dir_index *dir_index_get(const char *dir) {
    uint64_t h = fnv1a(dir); dir_index **slot = &g_dir_index[h % DIR_INDEX_SLOTS];
    for (dir_index *x = *slot; x; x = x->next) if (x->hash == h && !strcmp(x->path, dir)) return x;
    DIR *d = opendir(dir); if (!d) return NULL;
    // The names go into one pool first (a physical name is stored separately only when it differs from the guest name), as offsets in off[];
    // they become pointers once the pool stops growing
    size_t n = 0, cap = 64, plen = 0, pcap = 4096; size_t (*off)[2] = malloc(cap * sizeof *off); char *pool = malloc(pcap);
    if (!off || !pool) die("fs: directory index allocation failed");
    struct dirent *e; char guest[0x301]; int ok = 1;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!guest_name(dir, e->d_name, guest, sizeof guest)) { ok = 0; break; }
        size_t gl = strlen(guest) + 1, same = !strcmp(guest, e->d_name), pl = same ? 0 : strlen(e->d_name) + 1;
        if (n == cap && !(off = realloc(off, (cap *= 2) * sizeof *off))) die("fs: directory index allocation failed");
        while (plen + gl + pl > pcap) if (!(pool = realloc(pool, pcap *= 2))) die("fs: directory index allocation failed");
        off[n][0] = plen; memcpy(pool + plen, guest, gl); plen += gl;
        off[n][1] = same ? off[n][0] : plen; if (!same) { memcpy(pool + plen, e->d_name, pl); plen += pl; }
        n++;
    }
    closedir(d);
    if (!ok) { free(off); free(pool); return NULL; }
    dir_index *x = calloc(1, sizeof *x); if (!x || !(x->path = strdup(dir)) || !(x->e = malloc((n ? n : 1) * sizeof *x->e))) die("fs: directory index allocation failed");
    for (size_t i = 0; i < n; i++) { x->e[i].guest = pool + off[i][0]; x->e[i].phys = pool + off[i][1]; }
    free(off); qsort(x->e, n, sizeof *x->e, name_ent_cmp);
    x->hash = h; x->n = n; x->pool = pool; x->next = *slot; *slot = x;
    return x;
}
static int path_component(char *parent, size_t n, const char *name, int create, int fixed) {
    if (fixed && !create) {
        const dir_index *x = dir_index_get(parent);
        if (x) {
            name_ent key = {name, NULL}; const name_ent *hit = bsearch(&key, x->e, x->n, sizeof *x->e, name_ent_cmp);
            if (!hit) { errno = ENOENT; return 0; }
            size_t len = strlen(parent), k = strlen(hit->phys);
            if (len + k + 2 > n) { errno = ENAMETOOLONG; return 0; }
            parent[len] = '/'; memcpy(parent + len + 1, hit->phys, k + 1); return 1;
        }
    }
    DIR *d = opendir(parent); if (!d) return 0;
    struct dirent *e; char physical[0x301] = "", guest[0x301];
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!guest_name(parent, e->d_name, guest, sizeof guest)) { closedir(d); return 0; }
        if (!strcmp(guest, name)) { strlcpy(physical, e->d_name, sizeof physical); break; }
    }
    closedir(d);
    if (!physical[0]) {
        if (!create) { errno = ENOENT; return 0; }
        strlcpy(physical, name, sizeof physical);
        char p[1280]; struct stat st;
        snprintf(p, sizeof p, "%s/%s", parent, physical);
        if (!lstat(p, &st)) { // the exact guest name does not exist, but APFS matched the physical name to another entry
            unsigned i = 0;
            do { snprintf(physical, sizeof physical, ".dm-case-%u", ++i);
                 snprintf(p, sizeof p, "%s/%s", parent, physical); } while (!lstat(p, &st));
        }
        if (errno != ENOENT) return 0;
    }
    size_t len = strlen(parent), k = strlen(physical);
    if (len + k + 2 > n) { errno = ENAMETOOLONG; return 0; }
    parent[len] = '/'; memcpy(parent + len + 1, physical, k + 1); return 1;
}
static int root_path(const char *root, const char *rest, char *out, size_t n) {
    strlcpy(out, root, n);
    while (*rest) {
        while (*rest == '/') rest++;
        if (!*rest) break;
        const char *end = strchr(rest, '/'); if (!end) end = rest + strlen(rest);
        size_t k = end - rest; char part[0x301];
        if (k >= sizeof part) { errno = ENAMETOOLONG; return 0; }
        memcpy(part, rest, k); part[k] = 0;
        if (!strcmp(part, "..")) { errno = ENOENT; return 0; }
        if (strcmp(part, ".") && !path_component(out, n, part, 0, 1)) return 0;   // only the RomFS mount and its overlay roots come here (rom_overlay_path, dir_snapshot)
        rest = end;
    }
    return 1;
}
// Files in the overlay that the host itself reads (marker files, sidecar tables, data archives) are all looked up here. DM_ROMFS_OVERLAY may have
// several layers, colon-separated, earlier ones taking priority; old devices have two layers,
// "in-bundle romfs_overlay_astc:Legacy Device Pack directory"; treating the whole string as one directory when building file names finds nothing at all
// on old devices.
int dm_overlay_file(const char *rel, char *out, size_t n) {
    const char *ov = getenv("DM_ROMFS_OVERLAY");
    for (const char *p = ov ? ov : ""; *p;) {
        size_t l = strcspn(p, ":");
        if (l && snprintf(out, n, "%.*s/%s", (int)l, p, rel) < (int)n && !access(out, R_OK)) return 1;
        p += l; if (*p == ':') p++;
    }
    if (n) out[0] = 0;
    return 0;
}
// RomFS files one host file serves whatever the layers hold (mods.c, plan item 164: a mod's copy of a file the game's own overlay patches, when the
// overlay's file or a merge of the two is the one to use). Set at boot, before the game runs; the path as the RomFS spells it, without a leading /
#ifndef DM_TEST_ROM_NO_PIN
#define DM_TEST_ROM_NO_PIN 0
#endif
static struct { char rel[512], host[1024]; } g_pin[16];
static int g_npin;
void dm_rom_pin(const char *rel, const char *host) {
    while (*rel == '/') rel++;
    for (int i = 0; i < g_npin; i++) if (!strcmp(g_pin[i].rel, rel)) { strlcpy(g_pin[i].host, host, sizeof g_pin[i].host); return; }
    if (g_npin == 16) { LOG("[red] rom pin: more than 16 files, %s left to the layers", rel); return; }
    strlcpy(g_pin[g_npin].rel, rel, sizeof g_pin[0].rel); strlcpy(g_pin[g_npin].host, host, sizeof g_pin[0].host); g_npin++;
}
static int rom_overlay_path(const mount_t *m, const char *rest, char *out, size_t n) {
    if (g_npin && !DM_TEST_ROM_NO_PIN) {   // (the macro: negative control of t_fs only, the pins ignored)
        const char *r = rest; while (*r == '/') r++;
        for (int i = 0; i < g_npin; i++) if (!strcmp(g_pin[i].rel, r)) { strlcpy(out, g_pin[i].host, n); return 1; }
    }
    char *roots = strdup(m->overlay);
    if (!roots) die("rom overlay: path allocation failed");
    char *save = NULL;
    for (char *root = strtok_r(roots, ":", &save); root; root = strtok_r(NULL, ":", &save)) {
        char candidate[1024];
        if (root_path(root, rest, candidate, sizeof candidate) && !access(candidate, F_OK)) {
            strlcpy(out, candidate, n); free(roots); return 1;
        }
    }
    int found = root_path(m->dir, rest, out, n) && !access(out, F_OK);
    free(roots);
    if (found) return 1;
    errno = ENOENT; return 0;
}
// Two spellings Horizon takes (plan item 64k; armyja's nxnative-aa-port, MIT, written anew): a separator before the mount name (Unity's .NET path
// code, Path.GetFullPath with the working directory "/", gives "/rom:/…") and '\' between components (Ace Attorney opens "savedata:\systemdata")
static const char *path_lead(const char *p) {
#ifndef DM_TEST_PATH_STRICT   // negative control of t_fs only: both spellings refused, as before
    while (*p == '/') p++;
#endif
    return p;
}
static int path_fwd(const char *rest, char *buf, size_t n, const char **out) {   // '\' as '/': 0 when the path is too long
    *out = rest;
#ifndef DM_TEST_PATH_STRICT
    if (!strchr(rest, '\\')) return 1;
    if (strlcpy(buf, rest, n) >= n) { errno = ENAMETOOLONG; return 0; }
    for (char *q = buf; *q; q++) if (*q == '\\') *q = '/';
    *out = buf;
#endif
    return 1;
}
// create only allows the last component to not exist yet; intermediate directories still need an exact match.
static int host_path_ex(const char *p, char *out, size_t n, int *ro, int create) {
    p = path_lead(p);
    const char *c = strchr(p, ':');
    if (!c) { errno = ENOENT; return 0; }
    size_t ln = c - p;
    for (int i = 0; i < g_nmnt; i++) {
        if (strlen(g_mnt[i].name) != ln || strncmp(g_mnt[i].name, p, ln)) continue;
        if (ro) *ro = g_mnt[i].ro;
        const char *rest; char fwd[0x400];
        if (!path_fwd(c + 1, fwd, sizeof fwd, &rest)) return 0;
        if (g_mnt[i].overlay[0] && !create) return rom_overlay_path(&g_mnt[i], rest, out, n);
        strlcpy(out, g_mnt[i].dir, n);
        while (*rest) {
            while (*rest == '/') rest++;
            if (!*rest) break;
            const char *end = strchr(rest, '/'); if (!end) end = rest + strlen(rest);
            size_t k = end - rest; char part[0x301];
            if (k >= sizeof part) { errno = ENAMETOOLONG; return 0; }
            memcpy(part, rest, k); part[k] = 0;
            if (!strcmp(part, "..")) { errno = ENOENT; return 0; }
            const char *next = end; while (*next == '/') next++;
            if (strcmp(part, ".") && !path_component(out, n, part, create && !*next, g_mnt[i].fixed)) return 0;
            rest = end;
        }
        return 1;
    }
    errno = ENOENT; return 0;
}
// For host code that opens a guest file itself (the movie extractor's SetDataSource(path)): read-only resolution through the mount table, overlays included
int dm_fs_host_file(const char *path, char *out, size_t n) { PATH_LOCK(); return host_path_ex(path, out, n, NULL, 0); }
static int set_guest_name(const char *hp, const char *path) {
    const char *end = path + strlen(path); while (end > path && end[-1] == '/') end--;
    const char *start = end; while (start > path && start[-1] != '/' && start[-1] != ':') start--;
    char guest[0x301]; size_t k = end - start;
    if (k >= sizeof guest) { errno = ENAMETOOLONG; return -1; }
    memcpy(guest, start, k); guest[k] = 0;
    const char *physical = strrchr(hp, '/') + 1;
    if (strcmp(physical, guest)) return dm_xattr_set(hp, FS_GUEST_NAME, guest, strlen(guest));
    return dm_xattr_remove(hp, FS_GUEST_NAME);
}
static Result err_res(const char *op, const char *path) {
    if (g_flag.fs) LOG("fs %s %s → errno %d", op, path, errno);
    if (errno == ENOENT || errno == ENOTDIR) return RES_PATH_NOT_FOUND;
    if (errno == EEXIST) return RES_PATH_EXISTS;
    LOG("fs %s %s failed, errno=%d", op, path, errno); return RES_FAIL;
}
// Name resolution shares one lock with create/rename/enumerate, aliases and xattr are published to the guest together.
#define HP_EX(path, create) PATH_LOCK(); char hp[1024]; int ro = 0; if (!host_path_ex(path, hp, sizeof hp, &ro, create)) return err_res("path", path)
#define HP(path) HP_EX(path, 0)
#define RW() if (ro) { LOG("fs write to a read-only mount: %s", hp); return RES_FAIL; }
// Create a directory / file on a read-only mount: when the target exists, answer "already exists" as a writable mount would. That is the only failure
// the game expects here (console save storage never fails a write); after a generic failure it went on with a null handle
#define RW_CREATE() if (ro) { if (!access(hp, F_OK)) return RES_PATH_EXISTS; LOG("fs write to a read-only mount: %s", hp); return RES_FAIL; }

// ---------- Mount ----------
HLE_FORCE("_ZN2nn2fs22QueryMountRomCacheSizeEPm", "fsp-srv; rom reads the host directory directly, minimal cache", Result, h_fs_QueryRomCache, size_t *out) { *out = 0x1000; return RES_OK; }
HLE_FORCE("_ZN2nn2fs8MountRomEPKcPvm", "fsp-srv; rom → host RomFS directory", Result, h_fs_MountRom, const char *name, void *cache, size_t sz) {
    (void)cache; (void)sz;
    const char *rom = getenv("DM_ROMFS"); if (!rom) die("DM_ROMFS is not set and the game profile has no workspace.romfs");
    Result r = mount(name, rom, 1);
    if (r == RES_OK) {
        PATH_LOCK();
        for (int i = 0; i < g_nmnt; i++) if (!strcmp(g_mnt[i].name, name)) {
            g_mnt[i].fixed = 1;
            if (getenv("DM_ROMFS_OVERLAY")) strlcpy(g_mnt[i].overlay, getenv("DM_ROMFS_OVERLAY"), sizeof g_mnt[i].overlay);
            break;
        }
    }
    return r;
}
HLE_FORCE("_ZN2nn2fs21MountTemporaryStorageEPKc", "fsp-srv; temporary storage → host directory", Result, h_fs_MountTemp, const char *name) {
    char b[1024]; return mount(name, envdir("DM_TEMP", "run/platform/temp", b, sizeof b), 0);
}
// The save exists once its committed directory ($DM_SAVE) does (plan item 64k): the console answers IsSaveDataExisting false until the save data is
// made (EnsureSaveData, or the system at the first launch); it used to answer yes always. EnsureSaveData makes the directory (as the mount does)
static int save_exists(void) {
#ifdef DM_TEST_SAVE_ALWAYS   // negative control of t_fs only: always yes, as before
    return 1;
#endif
    const char *d = getenv("DM_SAVE"); struct stat st; return d && !stat(d, &st) && S_ISDIR(st.st_mode);
}
HLE_FORCE("_ZN2nn2fs14EnsureSaveDataERKNS_7account3UidE", "fsp-srv; the save directory made if it is not there", Result, h_fs_EnsureSave, const void *uid) {
    (void)uid; const char *d = getenv("DM_SAVE"); if (d && mkdir(d, 0755) && errno != EEXIST) LOG("[red] EnsureSaveData: %s not made, errno %d", d, errno);
    return RES_OK;
}
// The current working directory of the transactional save (the copy the game reads and writes; the host's own side files must be written here to enter the committed version with Commit). Returns NULL when no save is mounted
const char *dm_save_workdir(void) { for (int i = 0; i < g_nmnt; i++) if (g_mnt[i].commit[0]) return g_mnt[i].dir; return NULL; }
// The committed directory of the transactional save (the copy on disk, the version from the last Commit). Returns NULL when no save is mounted
const char *dm_save_commitdir(void) { for (int i = 0; i < g_nmnt; i++) if (g_mnt[i].commit[0]) return g_mnt[i].commit; return NULL; }
HLE_FORCE("_ZN2nn2fs13MountSaveDataEPKcRKNS_7account3UidE", "fsp-srv; save data → host directory (single user)", Result, h_fs_MountSave, const char *name, const void *uid) {
    (void)uid;
    // No default directory: silently using some other save directory makes a run start from the wrong state. Test runs use a fresh copy of a reference save
    const char *d = getenv("DM_SAVE");
    if (!d) die("DM_SAVE is not set: point it at a writable save directory");
    char dd[1024]; strlcpy(dd, d, sizeof dd); size_t L = strlen(dd); while (L > 1 && dd[L - 1] == '/') dd[--L] = 0;   // strip trailing slashes (.work / .new are appended at the same level)
    dm_game_mount_save(dd);   // game hook (game.h)
    return mount_ex(name, dd, 0, 1);
}
void (*g_dm_save_commit_hook)(const char *commit_dir);   // called after a save commit succeeds (committed directory); the iOS iCloud sync sets it, NULL on the host
static _Atomic uint64_t g_save_commits;   // number of successful commits (used by the front end when going to background to wait for "the game's current save has been committed")
uint64_t dm_fs_commit_count(void) { return atomic_load(&g_save_commits); }
HLE_FORCE("_ZN2nn2fs6CommitEPKc", "fsp-srv; transactional save: the working tree is cloned and swapped with the committed one atomically (renamex_np RENAME_SWAP)", Result, h_fs_Commit, const char *name) {
    PATH_LOCK(); pthread_mutex_lock(&g_lock);
    int i = 0; while (i < g_nmnt && strcmp(g_mnt[i].name, name)) i++;
    if (i == g_nmnt || !g_mnt[i].commit[0]) { pthread_mutex_unlock(&g_lock); return RES_OK; }   // non-transactional mount: nothing to commit
    uint64_t tc0 = clock_gettime_nsec_np(CLOCK_MONOTONIC), tw0 = atomic_exchange(&g_txn_first_ns, 0);
    dm_game_save_precommit(g_mnt[i].dir);                     // game hook (game.h): side files written into the working directory are swapped in with this commit
    uint64_t tc1 = clock_gettime_nsec_np(CLOCK_MONOTONIC);
    int perr = atomic_exchange(&g_save_poison, 0);
    if (perr) {                                                         // a write failed in this transaction: refuse the commit, discard the working directory
        char torn[512] = ""; size_t tl = 0; DIR *d0 = opendir(g_mnt[i].dir); struct dirent *e0;   // record files truncated to 0 bytes (the damage that would enter the save if committed as usual)
        while (d0 && (e0 = readdir(d0))) {
            if (e0->d_name[0] == '.') continue;
            char sub[1300]; snprintf(sub, sizeof sub, "%s/%s", g_mnt[i].dir, e0->d_name); DIR *d1 = opendir(sub); struct dirent *e1;
            while (d1 && (e1 = readdir(d1))) { char f[1600]; struct stat st; snprintf(f, sizeof f, "%s/%s", sub, e1->d_name);
                if (e1->d_name[0] != '.' && !stat(f, &st) && S_ISREG(st.st_mode) && !st.st_size && tl + strlen(e0->d_name) + strlen(e1->d_name) + 3 < sizeof torn)
                    tl += snprintf(torn + tl, sizeof torn - tl, " %s/%s", e0->d_name, e1->d_name); }
            if (d1) closedir(d1);
        }
        if (d0) closedir(d0);
        int nopen = 0; for (int k = 0; k < 65536; k++) nopen += g_fsave[k];   // save files still open: after re-cloning, these handles point into the deleted old working directory
        // Clone the committed version into a sibling directory first; once that succeeds, swap it with the tainted working directory atomically and
        // delete the one swapped out. If any step fails, do not exit: the working directory stays and keeps its taint (the next commit is refused
        // again and re-cloning is retried); the committed version is unchanged. (Before, the working directory was deleted first and a failed clone
        // called die: on a full disk the process exited and the working directory was gone.)
        char re[1100]; snprintf(re, sizeof re, "%s.re", g_mnt[i].dir); int rerr = 0;
        if (!access(re, F_OK) && rm_tree(re)) rerr = errno ? errno : EIO;
        else if (dm_fs_test_fail("reclone") || dm_clone_tree(g_mnt[i].commit, re)) { rerr = errno ? errno : EIO; rm_tree(re); }
        else if (dm_rename_swap(re, g_mnt[i].dir)) { rerr = errno ? errno : EIO; rm_tree(re); }
        else rm_tree(re);                                               // the tainted working directory, swapped out
        if (rerr) { save_poison(perr); LOG("[red] save transaction: re-cloning the working directory failed, errno=%d: it stays as it is and tainted (the next commit is refused and re-cloning retried); the committed save is unchanged", rerr); }
        pthread_mutex_unlock(&g_lock);
        LOG("[red] save commit refused: a save write failed in this transaction, errno=%d (0-byte files in the working directory: %s); the committed save is unchanged, the working directory %s%s", perr, tl ? torn + 1 : "none", rerr ? "could not be re-cloned (see the line above)" : "was re-cloned from it", nopen ? "; [red] save files are still open" : "");
        front_save_failed(perr);
        return RES_FAIL;
    }
    char neu[1100]; snprintf(neu, sizeof neu, "%s.new", g_mnt[i].commit);
    Result r = RES_OK;
    if (!access(neu, F_OK)) rm_tree(neu);
#ifndef DM_TEST_FS_COMMIT_INPLACE
    if (dm_clone_tree(g_mnt[i].dir, neu)) { LOG("[red] save commit: clonefile %s failed, errno=%d (committed save unchanged)", neu, errno); r = RES_FAIL; }
    else if (dm_rename_swap(neu, g_mnt[i].commit)) { LOG("[red] save commit: atomic swap failed, errno=%d (committed save unchanged)", errno); rm_tree(neu); r = RES_FAIL; }
    else rm_tree(neu);                                                  // the old version that was swapped out
#else   // negative control of t_fs_kill only: the committed directory removed, then the working one cloned in its place (not atomic)
    if (rm_tree(g_mnt[i].commit) || dm_clone_tree(g_mnt[i].dir, g_mnt[i].commit)) r = RES_FAIL;
#endif
    // Timing readouts (integer microseconds, no floating point): how long the pre-commit hook and the clone + atomic swap each took; how long in total from this transaction's first save write to the end of the commit (the game writing files + the commit; 0 = no save write in this transaction)
    static int n; if (r == RES_OK) { uint64_t tc2 = clock_gettime_nsec_np(CLOCK_MONOTONIC);
        LOG("save commit #%d: %s swapped into %s (%d save writes so far) (hook %llu us, clone + swap %llu us, %llu us since the first write of the transaction)", ++n, g_mnt[i].dir, g_mnt[i].commit, atomic_load(&g_save_writes),
            (unsigned long long)((tc1 - tc0) / 1000), (unsigned long long)((tc2 - tc1) / 1000), (unsigned long long)(tw0 ? (tc2 - tw0) / 1000 : 0)); atomic_fetch_add(&g_save_commits, 1);
        if (g_dm_save_commit_hook) g_dm_save_commit_hook(g_mnt[i].commit); }   // iCloud sync (cloud.m): called with the lock held, that side only takes a clonefile snapshot
    pthread_mutex_unlock(&g_lock);
    return r;
}
HLE_FORCE("_ZN2nn2fs7UnmountEPKc", "fsp-srv; mounts stay, unmount is a no-op", void, h_fs_Unmount, const char *name) { (void)name; }

// ---------- Files ----------
HLE_FORCE("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci", "fsp-srv → open(2)", Result, h_fs_OpenFile, uint64_t *out, const char *path, int mode) {
    HP(path);
    if (mode & (MODE_WRITE | MODE_APPEND)) RW();
    int fd = open(hp, (mode & (MODE_WRITE | MODE_APPEND)) ? ((mode & MODE_READ) ? O_RDWR : O_WRONLY) : O_RDONLY);
    if (fd < 0) return err_res("open", hp);
    struct stat st; fstat(fd, &st);
    if (S_ISDIR(st.st_mode)) { close(fd); return RES_PATH_NOT_FOUND; }
    if (fd >= (int)sizeof g_fmode) die("fd beyond the table");
    g_fmode[fd] = (uint8_t)mode; *out = (uint64_t)fd;
    { const char *c = strchr(path, ':'); size_t ln = c ? (size_t)(c - path) : 0; g_fsave[fd] = 0;
      for (int i = 0; c && i < g_nmnt; i++) if (strlen(g_mnt[i].name) == ln && !strncmp(g_mnt[i].name, path, ln)) g_fsave[fd] = g_mnt[i].commit[0] != 0; }
    fslog_init();
    if (g_fslog && fd < 4096) { pthread_mutex_lock(&g_fslog_lock); free(g_fdpath[fd]); g_fdpath[fd] = strdup(path); pthread_mutex_unlock(&g_fslog_lock); }
    if (g_flag.fs) LOG("fs open [%.2fs] %s (%s) mode=%d → fd %d", (now_ns() - g_t0_ns) / 1e9, path, hp, mode, fd);
    return RES_OK;
}
HLE_FORCE("_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm", "fsp-srv → pread(2)", Result, h_fs_ReadFile, size_t *out, uint64_t h, int64_t off, void *buf, size_t sz) {
    if (!h) { *out = 0; return RES_FAIL; }   // a file handle is an fd; 0 is stdin and never a file the game opened: the null handle it keeps using after a failed open
    size_t got = 0; uint64_t t0 = g_flag.bootprof ? now_ns() : 0; int reprep = 0;
    dm_guest_write_prepare(buf, sz);   // The destination may be a texture pool page write-protected by the graphics layer: a kernel write to a read-only page returns EFAULT instead of page-faulting, so unprotect and mark dirty first
    while (got < sz) {
        ssize_t r = pread((int)h, (uint8_t *)buf + got, sz - got, off + got);
        if (r < 0) { if (errno == EINTR) continue;
#ifndef DM_TEST_FS_NO_REPREP   // negative control of t_fs only: one prepare, as before
            // The graphics layer write-protects tracked pages again after each upload (gfx/nvn/nvn_dirty.c): a page made writable above can be read-only
            // again by the time pread writes it, and a system call gets EFAULT where a plain store would fault into the tracker. Prepare the rest again
            // and read on (nxnative 53d7aad, MIT; plan item 196: Monster Train 2 at 60 fps on a Mac lost a read in 2 of 3 runs, none in 4 after)
            if (errno == EFAULT && reprep++ < 8) {
                static _Atomic int once; if (!once++) LOG("[fs] ReadFile into %p+%#zx: the destination was write-protected again during the read, prepared again at +%#zx (first case)", buf, sz, got);
                dm_guest_write_prepare((uint8_t *)buf + got, sz - got); continue; }
#endif
            return err_res("pread", "fd"); }
        if (r == 0) break;
        got += r;
    }
    *out = got;
    if (g_flag.bootprof) dm_prof_add(PROF_FS_READ, now_ns() - t0, got);
    if (g_fslog) { pthread_mutex_lock(&g_fslog_lock);
        static int hash_on = -1; if (hash_on < 0) hash_on = getenv("DM_FSLOG_HASH") != NULL;   // DM_FSLOG_HASH=1: append a column with the FNV-1a 64 of the content read (to compare whether the bytes read from two resource packs match)
        uint64_t fh = 0xcbf29ce484222325ull; if (hash_on) for (size_t i = 0; i < got; i++) fh = (fh ^ ((uint8_t *)buf)[i]) * 0x100000001b3ull;
        if (hash_on) fprintf(g_fslog, "%.4f\t%s\t%lld\t%zu\t%p\t%016llx\n", (now_ns() - g_t0_ns) / 1e9, (int)h < 4096 && g_fdpath[(int)h] ? g_fdpath[(int)h] : "?", (long long)off, got, buf, (unsigned long long)fh);
        else fprintf(g_fslog, "%.4f\t%s\t%lld\t%zu\t%p\n", (now_ns() - g_t0_ns) / 1e9, (int)h < 4096 && g_fdpath[(int)h] ? g_fdpath[(int)h] : "?", (long long)off, got, buf);
        pthread_mutex_unlock(&g_fslog_lock); }
    if (g_flag.fs) {   // Read metering: print a line every 64 MiB accumulated or every 1000 reads
        static uint64_t n, bytes, next_b = 64 << 20; static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock(&m); n++; bytes += got;
        if (bytes >= next_b || n % 1000 == 0) { LOG("fs reads: %llu, %.1f MiB (this one fd %d offset %#llx size %#zx)", n, bytes / 1048576.0, (int)h, off, got); if (bytes >= next_b) next_b += 64 << 20; }
        pthread_mutex_unlock(&m);
    }
    return RES_OK;
}
HLE_FORCE("_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE", "fsp-srv → pwrite(2)", Result, h_fs_WriteFile, uint64_t h, int64_t off, const void *buf, size_t sz, const void *opt) {
    if (!h) return RES_FAIL;
    (void)opt; size_t put = 0;
    if (g_fsave[(int)h & 0xffff]) {                                     // Save write injection point (for deterministic regression; only writes on the save mount are counted)
        static int cfg = -1, kill_n, fail_n, fail_e;
        if (cfg < 0) { const char *k = getenv("DM_TEST_KILL_AT"), *f = getenv("DM_TEST_FS_FAIL");
            kill_n = k && !strncmp(k, "write:", 6) ? atoi(k + 6) : 0;
            if (f && !strncmp(f, "write:", 6)) { fail_n = atoi(f + 6); const char *c = strchr(f + 6, ':'); fail_e = c ? atoi(c + 1) : ENOSPC; }
            cfg = 1; }
        int n = atomic_fetch_add(&g_save_writes, 1) + 1;
        { uint64_t z = 0; atomic_compare_exchange_strong(&g_txn_first_ns, &z, clock_gettime_nsec_np(CLOCK_MONOTONIC)); }
        if (kill_n && n == kill_n) { LOG("DM_TEST_KILL_AT: save write %d, _exit (killed mid-write)", n); _exit(9); }
        if (fail_n && n == fail_n) { LOG("DM_TEST_FS_FAIL: save write %d fails, errno=%d", n, fail_e); errno = fail_e; save_poison(fail_e); return err_res("pwrite (injected)", "fd"); }
    }
    while (put < sz) {
        ssize_t r = pwrite((int)h, (const uint8_t *)buf + put, sz - put, off + put);
        if (r < 0) { if (errno == EINTR) continue; if (g_fsave[(int)h & 0xffff]) save_poison(errno); return err_res("pwrite", "fd"); }
        if (r == 0) { errno = ENOSPC; if (g_fsave[(int)h & 0xffff]) save_poison(ENOSPC); return err_res("pwrite (0 bytes written)", "fd"); }   // This used to loop forever here
        put += r;
    }
    return RES_OK;
}
HLE_FORCE("_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE", "fsp-srv → fstat(2)", Result, h_fs_GetFileSize, int64_t *out, uint64_t h) {
    if (!h) return RES_FAIL;
    struct stat st; if (fstat((int)h, &st)) return err_res("fstat", "fd");
    *out = st.st_size; return RES_OK;
}
HLE_FORCE("_ZN2nn2fs11SetFileSizeENS0_10FileHandleEl", "fsp-srv → ftruncate(2)", Result, h_fs_SetFileSize, uint64_t h, int64_t sz) {
    if (!h) return RES_FAIL;
    if (!ftruncate((int)h, sz)) return RES_OK;
    if (g_fsave[(int)h & 0xffff]) save_poison(errno);
    return err_res("ftruncate", "fd");
}
HLE_FORCE("_ZN2nn2fs9FlushFileENS0_10FileHandleE", "fsp-srv; host writes are in the page cache already, flush is a no-op", Result, h_fs_FlushFile, uint64_t h) { (void)h; return RES_OK; }
HLE_FORCE("_ZN2nn2fs9CloseFileENS0_10FileHandleE", "fsp-srv → close(2)", void, h_fs_CloseFile, uint64_t h) { if (!h) return; g_fmode[(int)h & 0xffff] = 0; g_fsave[(int)h & 0xffff] = 0;
    close((int)h); }
HLE_FORCE("_ZN2nn2fs15GetFileOpenModeENS0_10FileHandleE", "fsp-srv; the open mode is kept by the host", int, h_fs_GetFileOpenMode, uint64_t h) { return g_fmode[(int)h & 0xffff]; }

// ---------- Path operations ----------
HLE_FORCE("_ZN2nn2fs10CreateFileEPKcl", "fsp-srv → open(O_CREAT|O_EXCL)+ftruncate", Result, h_fs_CreateFile, const char *path, int64_t sz) {
    HP_EX(path, 1); RW_CREATE();
    int fd = open(hp, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return err_res("create", hp);
    int rc = ftruncate(fd, sz); int e = errno; close(fd);
    if (!rc && set_guest_name(hp, path)) { rc = -1; e = errno; }
    if (rc) unlink(hp);
    errno = e; return rc ? err_res("create", hp) : RES_OK;
}
HLE_FORCE("_ZN2nn2fs10DeleteFileEPKc", "fsp-srv → unlink(2)", Result, h_fs_DeleteFile, const char *path) { HP(path); RW(); return unlink(hp) ? err_res("unlink", hp) : RES_OK; }
HLE_FORCE("_ZN2nn2fs15CreateDirectoryEPKc", "fsp-srv → mkdir(2)", Result, h_fs_CreateDirectory, const char *path) { HP_EX(path, 1); RW_CREATE();
    if (mkdir(hp, 0755)) return err_res("mkdir", hp);
    if (set_guest_name(hp, path)) { int e = errno; rmdir(hp); errno = e; return err_res("mkdir name", hp); }
    return RES_OK; }
static int rm_tree(const char *p) {
    DIR *d = opendir(p); if (!d) return -1;
    struct dirent *e; char c[1024]; int rc = 0;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(c, sizeof c, "%s/%s", p, e->d_name);
        rc |= e->d_type == DT_DIR ? rm_tree(c) : unlink(c);
    }
    closedir(d);
    return rc | rmdir(p);
}
HLE_FORCE("_ZN2nn2fs26DeleteDirectoryRecursivelyEPKc", "fsp-srv → recursive delete (writable mounts only)", Result, h_fs_DeleteDirRec, const char *path) {
    HP(path); RW(); return rm_tree(hp) ? err_res("rmtree", hp) : RES_OK;
}
HLE_FORCE("_ZN2nn2fs10RenameFileEPKcS2_", "fsp-srv → rename(2)", Result, h_fs_RenameFile, const char *from, const char *to) {
    HP(from); RW(); char hp2[1024];
    if (!host_path_ex(to, hp2, sizeof hp2, NULL, 1)) return err_res("rename path", to);
    if (!access(hp2, F_OK)) return RES_PATH_EXISTS;
    if (rename(hp, hp2)) return err_res("rename", hp);
    if (dm_fs_test_fail("guestname") || set_guest_name(hp2, to)) { int e = errno ? errno : EIO;
        if (dm_fs_test_fail("renameback") || rename(hp2, hp)) {   // the undo failed too: the entry stays at the new path with the wrong guest name; do not exit. In a transactional save this transaction's commit is refused (committed version unchanged; re-cloning restores it)
            LOG("[red] fs rename: setting the name metadata failed and undoing the rename failed, errno=%d: %s stays at the new path%s", errno, hp2, in_txn_dir(hp2) ? "; this save transaction's commit will be refused" : ""); if (in_txn_dir(hp2)) save_poison(e); }
        errno = e; return err_res("rename name", hp2); }
    return RES_OK;
}
HLE_FORCE("_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc", "fsp-srv → stat(2)", Result, h_fs_GetEntryType, int32_t *out, const char *path) {
    HP(path); struct stat st;
    if (stat(hp, &st)) return err_res("stat", hp);
    *out = S_ISDIR(st.st_mode) ? ENT_DIR : ENT_FILE; return RES_OK;
}

// ---------- Directories ----------
// An overlaid directory is listed once, at open: every root's names merged (the first root to name an entry wins), kept compact (the names in one
// buffer, an offset, type and size per entry; DirectoryEntry is 784 bytes and Tears of the Kingdom has directories of 75,940 entries) and handed out
// as DirectoryEntry when read. The listing used to stop at 2048 names without a word, with a quadratic duplicate check
typedef struct { uint32_t name; int8_t type; int64_t size; } dsnap_t;
typedef struct { DIR *d; char path[1024]; DIR *od; char opath[1024]; dsnap_t *snap; char *snames; int64_t nsnap, psnap; int mode; } dirh_t;
static uint32_t name_hash(const char *s) { uint32_t h = 2166136261u; while (*s) h = (h ^ (uint8_t)*s++) * 16777619u; return h; }
static void dir_snapshot(dirh_t *h, const mount_t *m, const char *rest) {
    char *roots = strdup(m->overlay);
    const char **all = calloc(65, sizeof *all);
    char *parent = malloc(1024), *guest = malloc(0x301), *rel = malloc(2048), *resolved = malloc(1024);
    size_t ncap = 1024, buf_cap = 1 << 16, buf_n = 0, hcap = 2048; int64_t nn = 0;
    dsnap_t *ent = malloc(ncap * sizeof *ent); char *buf = malloc(buf_cap); int64_t *set = malloc(hcap * sizeof *set);
    if (!roots || !all || !parent || !guest || !rel || !resolved || !ent || !buf || !set) die("rom overlay: directory snapshot allocation failed");
    for (size_t i = 0; i < hcap; i++) set[i] = -1;
    int nr = 0;
    all[nr++] = m->dir;
    char *save = NULL;
    for (char *root = strtok_r(roots, ":", &save); root && nr < 65; root = strtok_r(NULL, ":", &save)) all[nr++] = root;
    for (int ri = nr - 1; ri >= 0; ri--) {
        if (!root_path(all[ri], rest, parent, 1024)) continue;
        DIR *x = opendir(parent); if (!x) continue;
        struct dirent *e;
        while ((e = readdir(x))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (!guest_name(parent, e->d_name, guest, 0x301)) continue;
            size_t k = name_hash(guest) & (hcap - 1); int dup = 0;
            for (; set[k] >= 0; k = (k + 1) & (hcap - 1)) if (!strcmp(buf + ent[set[k]].name, guest)) { dup = 1; break; }
            if (dup) continue;
            size_t len = strlen(guest) + 1;
            if (buf_n + len > buf_cap) { while (buf_n + len > buf_cap) buf_cap *= 2; if (!(buf = realloc(buf, buf_cap))) die("rom overlay: directory snapshot allocation failed"); }
            if ((size_t)nn == ncap) { ncap *= 2; if (!(ent = realloc(ent, ncap * sizeof *ent))) die("rom overlay: directory snapshot allocation failed"); }
            memcpy(buf + buf_n, guest, len); ent[nn] = (dsnap_t){.name = (uint32_t)buf_n}; buf_n += len; set[k] = nn++;
            if ((size_t)nn * 2 > hcap) {   // keep the set at most half full
                free(set); hcap *= 2; if (!(set = malloc(hcap * sizeof *set))) die("rom overlay: directory snapshot allocation failed");
                for (size_t i = 0; i < hcap; i++) set[i] = -1;
                for (int64_t j = 0; j < nn; j++) { size_t q = name_hash(buf + ent[j].name) & (hcap - 1); while (set[q] >= 0) q = (q + 1) & (hcap - 1); set[q] = j; }
            }
        }
        closedir(x);
    }
    for (int64_t j = 0; j < nn; j++) {
        snprintf(rel, 2048, "%s/%s", rest, buf + ent[j].name);
        if (!rom_overlay_path(m, rel, resolved, 1024)) continue;
        struct stat st;
        if (!stat(resolved, &st)) { ent[j].type = S_ISDIR(st.st_mode) ? ENT_DIR : ENT_FILE; ent[j].size = S_ISDIR(st.st_mode) ? 0 : st.st_size; }
    }
    h->snap = ent; h->snames = buf; h->nsnap = nn;
    free(set); free(resolved); free(rel); free(guest); free(parent); free(all); free(roots);
}
HLE_FORCE("_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci", "fsp-srv → opendir(3)", Result, h_fs_OpenDirectory, uint64_t *out, const char *path, int mode) {
    HP(path); DIR *d = opendir(hp);
    if (!d) return err_res("opendir", hp);
    dirh_t *h = calloc(1, sizeof *h); h->d = d; strlcpy(h->path, hp, sizeof h->path); h->mode = mode;
    const char *c = strchr(path, ':');
    if (c) for (int i = 0; i < g_nmnt; i++) if (g_mnt[i].overlay[0] && strlen(g_mnt[i].name) == (size_t)(c - path) && !strncmp(g_mnt[i].name, path, (size_t)(c - path))) {
        dir_snapshot(h, &g_mnt[i], c + 1); break;
    }
    *out = (uint64_t)h; return RES_OK;
}
HLE_FORCE("_ZN2nn2fs13ReadDirectoryEPlPNS0_14DirectoryEntryENS0_15DirectoryHandleEl", "fsp-srv → readdir(3)", Result, h_fs_ReadDirectory, int64_t *count, DirectoryEntry *ents, uint64_t hh, int64_t max) {
    if (!hh) { *count = 0; return RES_FAIL; }
    PATH_LOCK(); dirh_t *h = (dirh_t *)hh; int64_t n = 0; struct dirent *e;
    if (h->snap) { while (n < max && h->psnap < h->nsnap) { const dsnap_t *src = &h->snap[h->psnap++]; if (!(h->mode & (src->type == ENT_DIR ? DIRMODE_DIR : DIRMODE_FILE))) continue;
        memset(&ents[n], 0, sizeof ents[n]); strlcpy(ents[n].name, h->snames + src->name, sizeof ents[n].name); ents[n].type = src->type; ents[n].size = src->size; n++; } *count = n; return RES_OK; }
    int dfd = dirfd(h->d);
    while (n < max && (e = readdir(h->d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        // A directory's type comes from d_type without a stat; files need their size, from fstatat relative to the directory fd (following links,
        // like stat did) instead of resolving the whole path again for every entry. (From seedds/nxnative 76ea922, MIT.)
        struct stat st; int isdir;
        if (e->d_type == DT_DIR) isdir = 1;
        else { if (fstatat(dfd, e->d_name, &st, 0)) continue; isdir = S_ISDIR(st.st_mode); }
        if (!(h->mode & (isdir ? DIRMODE_DIR : DIRMODE_FILE))) continue;
        DirectoryEntry *o = &ents[n++]; memset(o, 0, sizeof *o);
        if (!guest_name(h->path, e->d_name, o->name, sizeof o->name)) { char c[1280]; snprintf(c, sizeof c, "%s/%s", h->path, e->d_name); *count = n - 1; return err_res("directory name", c); }
        o->type = isdir ? ENT_DIR : ENT_FILE; o->size = isdir ? 0 : st.st_size;
    }
    *count = n; return RES_OK;
}
HLE_FORCE("_ZN2nn2fs14CloseDirectoryENS0_15DirectoryHandleE", "fsp-srv → closedir(3)", void, h_fs_CloseDirectory, uint64_t hh) {
    if (!hh) return;   // the game also closes after a failed open (handle 0)
    dirh_t *h = (dirh_t *)hh; closedir(h->d); if (h->od) closedir(h->od); free(h->snap); free(h->snames); free(h);
}

// ---------- APIs other games use (Unity: RAM disk, read-without-count, directory counts, add-on content probing ...) ----------
// Same mount table and host paths as above; optional takeovers (HLE_FORCE_OPT), since not every game imports them
HLE_FORCE_OPT("_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE", "fs library allocator: the host allocates nothing on the guest heap, ignored (the original asks fsp-srv about the access log first)", void, h_fs_SetAllocator, void *a, void *f) { (void)a; (void)f; }
HLE_FORCE_OPT("_ZN2nn2fs25EnableGlobalFileDataCacheEPvm", "global file data cache: host reads use the page cache, ignored", void, h_fs_GlobalCache, void *buf, size_t sz) { (void)buf; (void)sz; }
HLE_FORCE_OPT("_ZN2nn2fs20GetRamDiskBufferSizeEm", "RAM disk: contents in a host temporary directory, minimal guest buffer", size_t, h_fs_RamDiskBufSize, size_t sz) { (void)sz; return 0x1000; }
HLE_FORCE_OPT("_ZN2nn2fs12MountRamDiskEPKcPvm", "RAM disk → a subdirectory of the host temporary directory (emptied at every mount, like power loss on the console)", Result, h_fs_MountRamDisk, const char *name, void *buf, size_t sz) {
    (void)buf; (void)sz; char b[1024], d[1100];
    snprintf(d, sizeof d, "%s/ramdisk_%s", envdir("DM_TEMP", "run/platform/temp", b, sizeof b), name);
    if (!access(d, F_OK) && rm_tree(d)) LOG("[red] RAM disk: cannot remove the previous contents: %s", d);
    mkdir(envdir("DM_TEMP", "run/platform/temp", b, sizeof b), 0755);
    return mount(name, d, 0);
}
HLE_FORCE_OPT("_ZN2nn2fs9IsMountedEPKc", "mount table query (returns bool)", unsigned, h_fs_IsMounted, const char *name) {
    pthread_mutex_lock(&g_lock); int hit = 0;
    for (int i = 0; i < g_nmnt; i++) if (!strcmp(g_mnt[i].name, name)) hit = 1;
    pthread_mutex_unlock(&g_lock); return hit;
}
// The sdk's POSIX layer (libc open / stat / opendir, which Unity's IL2CPP file interface goes through) first checks the mount and length limits for a path (sdk+0x9f890):
// IsPathMounted → GetPathLengthMax → GetUtf16PathLengthMax → GetEntryNameLengthMax → GetUtf16EntryNameLengthMax. The native implementations look in the sdk's own mount table,
// which does not contain the host's mounts (MountRom etc. are taken over), so they always fail (Unity's StreamingAssets reads report DirectoryNotFound).
// IsPathMounted as in sdk+0xb6220: null pointer / "@Host" prefix → false; otherwise look up the mount name before ':'.
// Limits: the native implementations read the mounted file system's attributes; here the output follows the sdk code's branch for "the attributes give no limit" --
//   GetPathLengthMax / GetEntryNameLengthMax = 0x300 (sdk+0xbbedc / +0xbc03c), GetUtf16*LengthMax = {false, 0} (worked through sdk+0xbc150 / +0xbc350 instruction by instruction).
//   What the real RomFS / save data attributes report is unverified; the limits only serve path validation, and the game's paths are far shorter than 0x300.
static int path_mounted(const char *path) {
    if (!path || !strncmp(path, "@Host", 5)) return 0;
    path = path_lead(path);
    const char *c = strchr(path, ':'); if (!c) return 0;
    size_t ln = (size_t)(c - path); int hit = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < g_nmnt; i++) if (strlen(g_mnt[i].name) == ln && !strncmp(g_mnt[i].name, path, ln)) hit = 1;
    pthread_mutex_unlock(&g_lock); return hit;
}
#define LIMIT_MOUNTED(path) if (!path_mounted(path)) { LOG("[red] fs path limit query: %s is not mounted", path ? path : "(null)"); return RES_FAIL; }
HLE_FORCE_OPT("_ZN2nn2fs13IsPathMountedEPKc", "is the path's mount there (returns bool)", unsigned, h_fs_IsPathMounted, const char *path) { return path_mounted(path); }
HLE_FORCE_OPT("_ZN2nn2fs16GetPathLengthMaxEPmPKc", "path length limit (the SDK default when the file system gives none)", Result, h_fs_PathLenMax, size_t *out, const char *path) { LIMIT_MOUNTED(path); *out = 0x300; return RES_OK; }
HLE_FORCE_OPT("_ZN2nn2fs21GetEntryNameLengthMaxEPmPKc", "entry name length limit (as above)", Result, h_fs_NameLenMax, size_t *out, const char *path) { LIMIT_MOUNTED(path); *out = 0x300; return RES_OK; }
HLE_FORCE_OPT("_ZN2nn2fs21GetUtf16PathLengthMaxEPbPmPKc", "UTF-16 path length limit (as above: none)", Result, h_fs_U16PathLenMax, uint8_t *has, size_t *out, const char *path) { LIMIT_MOUNTED(path); *has = 0; *out = 0; return RES_OK; }
HLE_FORCE_OPT("_ZN2nn2fs26GetUtf16EntryNameLengthMaxEPbPmPKc", "UTF-16 entry name length limit (as above: none)", Result, h_fs_U16NameLenMax, uint8_t *has, size_t *out, const char *path) { LIMIT_MOUNTED(path); *has = 0; *out = 0; return RES_OK; }
HLE_FORCE_OPT("_ZN2nn2fs31QueryMountAddOnContentCacheSizeEPmi", "add-on content: minimal cache", Result, h_fs_QueryAocCache, size_t *out, int idx) { (void)idx; *out = 0x1000; return RES_OK; }
HLE_FORCE_OPT("_ZN2nn2fs17MountAddOnContentEPKciPvm", "add-on content: nothing to mount", Result, h_fs_MountAoc, const char *name, int idx, void *buf, size_t sz) {
    (void)buf; (void)sz; LOG("fs MountAddOnContent %s #%d: the host has no add-on content data", name, idx); return RES_PATH_NOT_FOUND;
}
// bool results: Switch callers read only w0's low byte
HLE_FORCE_OPT("_ZN2nn2fs18IsSaveDataExistingERKNS_7account3UidE", "does the save exist: its directory is there", unsigned, h_fs_SaveExists, const void *uid) { (void)uid; return save_exists(); }
// Another application's save (TotK looks for The Legend of Zelda: Breath of the Wild's, 0x01007ef00011e000): the host has only this game's save
HLE_FORCE_OPT("_ZN2nn2fs18IsSaveDataExistingENS_13ApplicationIdERKNS_7account3UidE", "does the save exist (given application): only this game's", unsigned, h_fs_SaveExistsApp, uint64_t app, const void *uid) {
    (void)uid; return app == dm_game.program_id && save_exists();
}
// Access log switch and read priority (TotK calls SetPriorityOnCurrentThread on its threads): on the console both ask fsp-srv; the host has no access log,
// and priority does not change its reads. From nxnative's hle_fs.c (seedds, MIT)
HLE_FORCE_OPT("_ZN2nn2fs26SetPriorityOnCurrentThreadENS0_8PriorityE", "file read priority: ignored", void, h_fs_SetPrio, int prio) { (void)prio; }
HLE_FORCE_OPT("_ZN2nn2fs6detail18IsEnabledAccessLogEj", "access log: off (bool)", unsigned, h_fs_AccessLogMask, unsigned mask) { (void)mask; return 0; }
HLE_FORCE_OPT("_ZN2nn2fs6detail18IsEnabledAccessLogEv", "access log: off (bool)", unsigned, h_fs_AccessLog, void) { return 0; }

// Indexed cache storage (TotK): one host directory per index under $DM_CACHE. Semantics from the SDK API reference (fs_CacheStorageWithIndex.h,
// fs_CacheStorage.h): MountCacheStorage(name, index) gives ResultTargetNotFound when that index does not exist; ReadCacheStorageList lists the existing
// indices. Not transactional: what the game writes there is data it can make again, and Commit on a non-transactional mount is a no-op.
// CacheStorageListHandle is a pointer the game only hands back (the sdk keeps a 0x28-byte object: cursor + vector<int>); the host keeps its own list.
// CacheStorageInfo is 0x20 bytes, zeroed, the index at +0 (sdk 0xeda5c ReadCacheStorageList).
static const char *cache_dir(int index, char *out, size_t n) {
    char b[1024]; snprintf(out, n, "%s/%d", envdir("DM_CACHE", "run/platform/cache", b, sizeof b), index); return out;
}
HLE_FORCE_OPT("_ZN2nn2fs17MountCacheStorageEPKci", "cache storage <index> → host directory; ResultTargetNotFound when it was not created", Result, h_fs_MountCache, const char *name, int index) {
    char d[1100]; struct stat st;
    if (stat(cache_dir(index, d, sizeof d), &st) || !S_ISDIR(st.st_mode)) return RES_TARGET_NOT_FOUND;
    return mount(name, d, 0);
}
HLE_FORCE_OPT("_ZN2nn2fs18CreateCacheStorageEill", "create cache storage <index>: a host directory (sizes not reserved)", Result, h_fs_CreateCache, int index, int64_t size, int64_t journal) {
    char b[1024], d[1100]; mkdir(envdir("DM_CACHE", "run/platform/cache", b, sizeof b), 0755);
    if (mkdir(cache_dir(index, d, sizeof d), 0755) && errno != EEXIST) {   // at most 3 lines: a game may ask again and again
        static _Atomic int nf; if (atomic_fetch_add(&nf, 1) < 3) LOG("[red] CreateCacheStorage %d: %s errno=%d", index, d, errno); return RES_FAIL; }
    LOG("fs cache storage %d created → %s (data %lld, journal %lld bytes)", index, d, (long long)size, (long long)journal);
    return RES_OK;
}
HLE_FORCE_OPT("_ZN2nn2fs18DeleteCacheStorageEi", "delete cache storage <index>: its host directory", Result, h_fs_DeleteCache, int index) {
    char d[1100]; cache_dir(index, d, sizeof d);
    if (access(d, F_OK)) return RES_TARGET_NOT_FOUND;
    int r; { PATH_LOCK(); r = rm_tree(d); }
    LOG("fs cache storage %d deleted", index);
    return r ? RES_FAIL : RES_OK;
}
typedef struct { int n, cur, idx[64]; } cache_list;
HLE_FORCE_OPT("_ZN2nn2fs20OpenCacheStorageListEPNS0_22CacheStorageListHandleE", "cache storage list: the numeric directories under $DM_CACHE", Result, h_fs_OpenCacheList, cache_list **out) {
    cache_list *l = calloc(1, sizeof *l); char b[1024];
    DIR *dir = opendir(envdir("DM_CACHE", "run/platform/cache", b, sizeof b)); struct dirent *e;
    while (dir && (e = readdir(dir)) && l->n < 64) {
        char *end; long v = strtol(e->d_name, &end, 10);
        if (e->d_name[0] >= '0' && e->d_name[0] <= '9' && !*end && v >= 0 && v <= 0x7fffffff) l->idx[l->n++] = (int)v;
    }
    if (dir) closedir(dir);
    for (int i = 1; i < l->n; i++) for (int j = i; j > 0 && l->idx[j - 1] > l->idx[j]; j--) { int t = l->idx[j]; l->idx[j] = l->idx[j - 1]; l->idx[j - 1] = t; }
    *out = l; return RES_OK;
}
HLE_FORCE_OPT("_ZN2nn2fs20ReadCacheStorageListEPiPNS0_16CacheStorageInfoENS0_22CacheStorageListHandleEi", "cache storage list: next entries",
              Result, h_fs_ReadCacheList, int *n, uint8_t *info, cache_list *l, int count) {
    int k = 0;
    while (k < count && l && l->cur < l->n) { memset(info + 0x20 * k, 0, 0x20); memcpy(info + 0x20 * k, &l->idx[l->cur++], 4); k++; }
    *n = k; return RES_OK;
}
HLE_FORCE_OPT("_ZN2nn2fs21CloseCacheStorageListENS0_22CacheStorageListHandleE", "cache storage list: closed", void, h_fs_CloseCacheList, cache_list *l) { free(l); }
HLE_FORCE_OPT("_ZN2nn2fs21MountSaveDataReadOnlyEPKcNS_13ApplicationIdERKNS_7account3UidE", "read-only mount of this application's save → the same host save directory (committed copy, read-only)", Result, h_fs_MountSaveRo, const char *name, uint64_t app, const void *uid) {
    (void)uid; const char *d = getenv("DM_SAVE"); if (!d) die("DM_SAVE is not set: point it at a writable save directory");
    LOG("fs MountSaveDataReadOnly %s, application %#llx%s", name, (unsigned long long)app, app == dm_game.program_id ? "" : ": not this game, no such save");
    if (app != dm_game.program_id) return RES_TARGET_NOT_FOUND;   // the save does not exist (the SDK reference makes its existence a precondition)
    return mount(name, d, 1);
}
HLE_FORCE_OPT("_ZN2nn2fs6CommitEPKPKci", "commit several mounts: one Commit each (each atomic)", Result, h_fs_CommitN, const char *const *names, int n) {
    Result r = RES_OK;
    for (int i = 0; i < n; i++) { Result ri = h_fs_Commit(names[i]); if (ri && !r) r = ri; }
    return r;
}
static Result read_exact(uint64_t h, int64_t off, void *buf, size_t sz) {
    size_t got = 0; Result r = h_fs_ReadFile(&got, h, off, buf, sz);
    if (r == RES_OK && got != sz) { LOG("[red] fs ReadFile (no size out) read %zu / %zu bytes: fd %d offset %#llx goes past the end of the file", got, sz, (int)h, (long long)off); return RES_FAIL; }
    return r;
}
HLE_FORCE_OPT("_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm", "fsp-srv → pread(2); the overload without a size out must read everything", Result, h_fs_ReadFileN, uint64_t h, int64_t off, void *buf, size_t sz) { return read_exact(h, off, buf, sz); }
HLE_FORCE_OPT("_ZN2nn2fs8ReadFileENS0_10FileHandleElPvmRKNS0_10ReadOptionE", "fsp-srv → pread(2); the overload without a size out must read everything", Result, h_fs_ReadFileNO, uint64_t h, int64_t off, void *buf, size_t sz, const void *opt) { (void)opt; return read_exact(h, off, buf, sz); }
HLE_FORCE_OPT("_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvmRKNS0_10ReadOptionE", "fsp-srv → pread(2); size out, with read options (the SDK's libc read uses it)", Result, h_fs_ReadFileO, size_t *out, uint64_t h, int64_t off, void *buf, size_t sz, const void *opt) { (void)opt; return h_fs_ReadFile(out, h, off, buf, sz); }
HLE_FORCE_OPT("_ZN2nn2fs15DeleteDirectoryEPKc", "fsp-srv → rmdir(2)", Result, h_fs_DeleteDir, const char *path) { HP(path); RW(); return rmdir(hp) ? err_res("rmdir", hp) : RES_OK; }
HLE_FORCE_OPT("_ZN2nn2fs25CleanDirectoryRecursivelyEPKc", "fsp-srv → empty the directory, keep the directory", Result, h_fs_CleanDir, const char *path) {
    HP(path); RW(); DIR *d = opendir(hp); if (!d) return err_res("opendir", hp);
    struct dirent *e; char c[1280]; int rc = 0;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(c, sizeof c, "%s/%s", hp, e->d_name);
        rc |= e->d_type == DT_DIR ? rm_tree(c) : unlink(c);
    }
    closedir(d);
    return rc ? err_res("clean", hp) : RES_OK;
}
HLE_FORCE_OPT("_ZN2nn2fs15RenameDirectoryEPKcS2_", "fsp-srv → rename(2) (directory)", Result, h_fs_RenameDir, const char *from, const char *to) {
    HP(from); RW(); char hp2[1024];
    if (!host_path_ex(to, hp2, sizeof hp2, NULL, 1)) return err_res("rename path", to);
    if (!access(hp2, F_OK)) return RES_PATH_EXISTS;
    if (rename(hp, hp2)) return err_res("rename", hp);
    if (dm_fs_test_fail("guestname") || set_guest_name(hp2, to)) { int e = errno ? errno : EIO;
        if (dm_fs_test_fail("renameback") || rename(hp2, hp)) {   // the undo failed too: the entry stays at the new path with the wrong guest name; do not exit. In a transactional save this transaction's commit is refused (committed version unchanged; re-cloning restores it)
            LOG("[red] fs rename: setting the name metadata failed and undoing the rename failed, errno=%d: %s stays at the new path%s", errno, hp2, in_txn_dir(hp2) ? "; this save transaction's commit will be refused" : ""); if (in_txn_dir(hp2)) save_poison(e); }
        errno = e; return err_res("rename name", hp2); }
    return RES_OK;
}
HLE_FORCE_OPT("_ZN2nn2fs16GetFreeSpaceSizeEPlPKc", "fsp-srv → statvfs(3): free space of the host volume", Result, h_fs_FreeSpace, int64_t *out, const char *path) {
    HP(path); struct statvfs st; if (statvfs(hp, &st)) return err_res("statvfs", hp);
    *out = (int64_t)st.f_bavail * (int64_t)st.f_frsize; return RES_OK;
}
HLE_FORCE_OPT("_ZN2nn2fs22GetDirectoryEntryCountEPlNS0_15DirectoryHandleE", "fsp-srv: directory entries counted by the open mode (as ReadDirectory counts them)", Result, h_fs_DirCount, int64_t *out, uint64_t hh) {
    PATH_LOCK(); dirh_t *h = (dirh_t *)hh; int64_t n = 0;
    if (h->snap) { for (int64_t i = 0; i < h->nsnap; i++) if (h->mode & (h->snap[i].type == ENT_DIR ? DIRMODE_DIR : DIRMODE_FILE)) n++; *out = n; return RES_OK; }
    DIR *d = opendir(h->path); if (!d) return err_res("opendir", h->path);
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char c[1280]; snprintf(c, sizeof c, "%s/%s", h->path, e->d_name);
        struct stat st; if (stat(c, &st)) continue;
        if (h->mode & (S_ISDIR(st.st_mode) ? DIRMODE_DIR : DIRMODE_FILE)) n++;
    }
    closedir(d); *out = n; return RES_OK;
}
