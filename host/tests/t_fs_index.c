// RomFS name index and directory listing (hle_fs.c path_component / dir_index_get / ReadDirectory). opendir and getxattr are counted:
//   (1) 2000 lookups in a 2000-entry RomFS directory open each directory once and read no xattr (the scan used to open the parent directory
//       and read every entry's xattr for every lookup: 4000 opendir and millions of getxattr here);
//   (2) a missing name is PathNotFound; a nested path resolves;
//   (3) a case collision in RomFS (b.txt next to .dm-case-1 named "B.txt") resolves both names to their own files, the physical alias itself is
//       not a guest name, and only .dm-case-1's xattr is read;
//   (4) ReadDirectory lists guest names (no alias), the types and the sizes;
//   (5) MountSaveDataReadOnly is read-only but not indexed: a file the save gains afterwards (a Commit through the writable mount) is found;
//       another application's save does not exist (TotK looks for Breath of the Wild's).
#include <dirent.h>
#include <sys/xattr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int g_opendir_n, g_getxattr_n;
static DIR *count_opendir(const char *p) { g_opendir_n++; return opendir(p); }
#include "plat.h"
static ssize_t count_getxattr(const char *p, const char *n, void *v, size_t s) { g_getxattr_n++; return dm_xattr_get(p, n, v, s); }   // hle_fs.c reads through plat.h
#define opendir count_opendir
#define dm_xattr_get count_getxattr
#include "../hle/hle_fs.c"
#undef opendir
#undef dm_xattr_get

struct dm_flags g_flag; uint64_t g_t0_ns;
uint64_t now_ns(void) { return 0; }
void dm_prof_add(int id, uint64_t ns, uint64_t b) { (void)id; (void)ns; (void)b; }
void dm_guest_write_prepare(void *a, size_t n) { (void)a; (void)n; }
static int checks, fails;
#define CHECK(c, ...) do { checks++; int ok_ = !!(c); fails += !ok_; printf("%s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); } while (0)
static void put(const char *p, const char *s) { FILE *f = fopen(p, "w"); if (!f) { perror(p); exit(2); } fputs(s, f); fclose(f); }
static int content(const char *path, const char *want) {
    uint64_t h; if (h_fs_OpenFile(&h, path, MODE_READ)) return 0;
    char b[64] = {0}; size_t n = 0; Result r = h_fs_ReadFile(&n, h, 0, b, sizeof b - 1);
    h_fs_CloseFile(h); return !r && n == strlen(want) && !memcmp(b, want, n);
}
static int list(const char *path, DirectoryEntry *e, int max) {
    uint64_t h; if (h_fs_OpenDirectory(&h, path, 3)) return -1;
    int64_t n = 0; h_fs_ReadDirectory(&n, e, h, max); h_fs_CloseDirectory(h); return (int)n;
}
static const DirectoryEntry *find(const DirectoryEntry *e, int n, const char *name) { for (int i = 0; i < n; i++) if (!strcmp(e[i].name, name)) return &e[i]; return NULL; }
enum { N = 2000 };

int main(void) {
    char root[1024], rom[1100], p[1300];
    const char *tmp = getenv("TMPDIR"); snprintf(root, sizeof root, "%s/t_fs_index.XXXXXX", tmp ? tmp : "/private/tmp");
    if (!mkdtemp(root)) { perror("mkdtemp"); return 2; }
    snprintf(rom, sizeof rom, "%s/rom", root); mkdir(rom, 0755);
    snprintf(p, sizeof p, "%s/voice", rom); mkdir(p, 0755);
    for (int i = 0; i < N; i++) { snprintf(p, sizeof p, "%s/voice/v%04d.bin", rom, i); put(p, "v"); }
    snprintf(p, sizeof p, "%s/Mixed", rom); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/Mixed/b.txt", rom); put(p, "lower");
    snprintf(p, sizeof p, "%s/Mixed/.dm-case-1", rom); put(p, "UPPER!");
    if (setxattr(p, FS_GUEST_NAME, "B.txt", 5, 0, XATTR_NOFOLLOW)) { perror("setxattr"); return 2; }
    snprintf(p, sizeof p, "%s/a", rom); mkdir(p, 0755); snprintf(p, sizeof p, "%s/a/b", rom); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/a/b/c", rom); mkdir(p, 0755); snprintf(p, sizeof p, "%s/a/b/c/deep.txt", rom); put(p, "deep");
    setenv("DM_ROMFS", rom, 1); unsetenv("DM_ROMFS_OVERLAY");
    CHECK(h_fs_MountRom("rom", NULL, 0) == RES_OK, "mount the RomFS");

    printf("1 lookups in a large directory\n");
    g_opendir_n = g_getxattr_n = 0; int ok = 0;
    for (int i = 0; i < N; i++) { char g[64]; snprintf(g, sizeof g, "rom:/voice/v%04d.bin", i); ok += content(g, "v"); }
    CHECK(ok == N, "%d of %d files read", ok, N);
    CHECK(g_opendir_n == 2, "the RomFS root and voice/ opened once each (opendir %d)", g_opendir_n);
    CHECK(g_getxattr_n == 0, "no xattr read for plain names (getxattr %d)", g_getxattr_n);

    printf("2 missing and nested\n");
    uint64_t h; Result r = h_fs_OpenFile(&h, "rom:/voice/nope.bin", MODE_READ); if (!r) h_fs_CloseFile(h);
    CHECK(r == RES_PATH_NOT_FOUND, "a missing name → PathNotFound");
    CHECK(content("rom:/a/b/c/deep.txt", "deep"), "a nested path resolves");

    printf("3 case collision in RomFS\n");
    g_getxattr_n = 0;
    CHECK(content("rom:/Mixed/B.txt", "UPPER!"), "B.txt → .dm-case-1");
    CHECK(content("rom:/Mixed/b.txt", "lower"), "b.txt → b.txt");
    r = h_fs_OpenFile(&h, "rom:/Mixed/.dm-case-1", MODE_READ); if (!r) h_fs_CloseFile(h);
    CHECK(r == RES_PATH_NOT_FOUND, "the physical alias .dm-case-1 is not a guest name");
    CHECK(g_getxattr_n == 1, "only .dm-case-1's xattr was read (getxattr %d)", g_getxattr_n);

    printf("4 directory listing\n");
    DirectoryEntry e[8]; int n = list("rom:/Mixed", e, 8); const DirectoryEntry *x;
    CHECK(n == 2, "Mixed lists 2 entries (%d)", n);
    CHECK((x = find(e, n, "B.txt")) && x->type == ENT_FILE && x->size == 6, "B.txt: a file of 6 bytes");
    CHECK((x = find(e, n, "b.txt")) && x->type == ENT_FILE && x->size == 5, "b.txt: a file of 5 bytes");
    CHECK(!find(e, n, ".dm-case-1"), "no physical alias in the listing");
    n = list("rom:/", e, 8);
    CHECK(n == 3 && (x = find(e, n, "voice")) && x->type == ENT_DIR && x->size == 0 && find(e, n, "Mixed") && find(e, n, "a"), "the root lists 3 directories (%d)", n);

    printf("5 a read-only save mount is not indexed\n");
    char save[1100]; snprintf(save, sizeof save, "%s/save", root); mkdir(save, 0755);
    snprintf(p, sizeof p, "%s/old.dat", save); put(p, "old");
    setenv("DM_SAVE", save, 1);
    CHECK(h_fs_MountSaveRo("ros", dm_game.program_id, NULL) == RES_OK, "mount the save read-only");
    CHECK(h_fs_MountSaveRo("oth", dm_game.program_id ^ 1, NULL) == RES_TARGET_NOT_FOUND, "another application's save is not found");
    CHECK(h_fs_SaveExistsApp(dm_game.program_id, NULL) && !h_fs_SaveExistsApp(dm_game.program_id ^ 1, NULL), "only this game's save exists");
    CHECK(content("ros:/old.dat", "old"), "an existing save file reads");
    r = h_fs_OpenFile(&h, "ros:/new.dat", MODE_READ); if (!r) h_fs_CloseFile(h);
    CHECK(r == RES_PATH_NOT_FOUND, "new.dat is not there yet");
    snprintf(p, sizeof p, "%s/new.dat", save); put(p, "new");   // what a Commit through the writable mount does to this directory
    CHECK(content("ros:/new.dat", "new"), "after the save gains new.dat it is found (no stale index)");

    printf("t_fs_index: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
