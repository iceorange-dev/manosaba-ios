// A save killed at any moment (plan item 51: an app swiped away in the app switcher while suspended gets SIGKILL, no callback): a child process
// mounts the transactional save and commits version after version (64 files, all holding the version number), telling the parent each commit it
// finished; the parent kills it with SIGKILL after a random 2-40 ms, 60 times, and on until the writer has got through more than 60 commits (at most
// 180 times: on a loaded machine it gets through fewer in each, batch 110 / 112 saw 59). After each kill a fresh process mounts the save: every file holds the
// same version, and that version is the last one reported or the one after (a commit can finish before its report). The real hle_fs.c with
// clonefile and renamex_np(RENAME_SWAP) on the disk under TMPDIR.
// Negative control: -DDM_TEST_FS_COMMIT_INPLACE (remove the committed directory, then clone the working one in its place) loses or tears the save.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "../hle/hle_fs.c"
struct dm_flags g_flag; uint64_t g_t0_ns; uint64_t now_ns(void) { return 0; } void dm_prof_mark(const char *s) { (void)s; }
void dm_prof_add(int id, uint64_t ns, uint64_t b) { (void)id; (void)ns; (void)b; } void dm_guest_write_prepare(void *a, size_t n) { (void)a; (void)n; }
enum { NFILES = 64, TRIALS = 60 };
static Result write_file(const char *path, const char *s) {
    uint64_t h; Result r = h_fs_OpenFile(&h, path, MODE_WRITE); if (r) { if (h_fs_CreateFile(path, 0) || h_fs_OpenFile(&h, path, MODE_WRITE)) return RES_FAIL; }
    h_fs_SetFileSize(h, (int64_t)strlen(s)); r = h_fs_WriteFile(h, 0, s, strlen(s), NULL); h_fs_CloseFile(h); return r;
}
static void writer(int fd) {   // commits versions 1, 2, ... until killed, reporting each finished commit
    if (h_fs_MountSave("save", NULL) != RES_OK) _exit(3);
    char c[64]; uint64_t h; size_t got = 0; int v = 0;
    if (!h_fs_OpenFile(&h, "save:/f00", MODE_READ)) { h_fs_ReadFile(&got, h, 0, c, sizeof c - 1); h_fs_CloseFile(h); c[got] = 0; v = atoi(c); }   // go on from what is there
    for (;;) {
        v++; char val[64], p[64]; snprintf(val, sizeof val, "%d version %d of the save", v, v);
        for (int k = 0; k < NFILES; k++) { snprintf(p, sizeof p, "save:/f%02d", k); if (write_file(p, val)) _exit(4); }
        if (h_fs_Commit("save") != RES_OK) _exit(5);
        if (write(fd, &v, sizeof v) != sizeof v) _exit(6);
    }
}
static int reader(void) {   // in a fresh process: the version every file holds (0 if none), -1 if they differ or one is torn
    if (h_fs_MountSave("save", NULL) != RES_OK) return -2;
    int v = -3;
    for (int k = 0; k < NFILES; k++) {
        char p[64], c[64]; uint64_t h; size_t got = 0; snprintf(p, sizeof p, "save:/f%02d", k);
        if (h_fs_OpenFile(&h, p, MODE_READ)) { if (v > 0) return -1; v = 0; continue; }
        h_fs_ReadFile(&got, h, 0, c, sizeof c - 1); h_fs_CloseFile(h); c[got] = 0;
        int x = atoi(c); char want[64]; snprintf(want, sizeof want, "%d version %d of the save", x, x);
        if (strcmp(c, want) || (v >= 0 && v != x) || (v == 0 && k)) return -1;
        v = x;
    }
    return v;
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    char root[1024], save[1100]; const char *tp = getenv("TMPDIR"); snprintf(root, sizeof root, "%s/t_fs_kill.XXXXXX", tp ? tp : "/private/tmp");
    if (!mkdtemp(root)) { perror("mkdtemp"); return 2; }
    snprintf(save, sizeof save, "%s/save", root); setenv("DM_SAVE", save, 1);
    srand(12345); int bad = 0, lost = 0, mid = 0, last_v = 0, vmax = 0, t;
    for (t = 0; t < TRIALS || (vmax <= TRIALS && t < 3 * TRIALS); t++) {
        int pf[2]; if (pipe(pf)) return 2;
        pid_t w = fork(); if (!w) { close(pf[0]); freopen("/dev/null", "w", stderr); writer(pf[1]); }
        close(pf[1]); usleep(2000 + rand() % 38000); kill(w, SIGKILL); int st; waitpid(w, &st, 0);
        int v, reported = last_v; while (read(pf[0], &v, sizeof v) == sizeof v) reported = v; close(pf[0]);
        int rp[2]; if (pipe(rp)) return 2;
        pid_t r = fork(); if (!r) { close(rp[0]); freopen("/dev/null", "w", stderr); int got = reader(); if (write(rp[1], &got, sizeof got) != sizeof got) _exit(7); _exit(0); }
        close(rp[1]); int got = -9; if (read(rp[0], &got, sizeof got) != sizeof got) got = -9; close(rp[0]); waitpid(r, &st, 0);
        int ok = got == reported || got == reported + 1;
        if (!ok) { bad++; lost += got >= 0 && got < reported; if (bad <= 5) printf("  trial %d: the save reads %d after commit %d was reported\n", t + 1, got, reported); }
        mid += got == reported + 1; if (got > vmax) vmax = got; last_v = got > 0 ? got : reported;
    }
    printf("%d trials, %d commits in all; %d killed after a commit it had not reported yet\n", t, vmax, mid);
    int fails = 0;
    #define CHECK(c, ...) do { if (c) printf("  ✓ "); else { fails++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
    CHECK(bad == 0, "after every kill each file holds one whole version, the last reported or the next (%d of %d not, %d of them lost commits)", bad, t, lost);
    CHECK(vmax > TRIALS, "the writer got through many commits (%d), so kills landed in writes and commits alike", vmax);
    char cmd[1200]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); if (system(cmd)) {}
    printf("== 2 checks, %d failed ==\n", fails); return fails != 0;
}
