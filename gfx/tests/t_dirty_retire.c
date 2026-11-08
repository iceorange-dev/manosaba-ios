// A pool destroyed while its pages are being protected (gfx/nvn/nvn_dirty.c nvn_dirty_protect; seedds/nxnative af616f5 and MacNativeGames
// 9505582, MIT): the destruction is staged right after protection's lookup. Under the slot mutex it waits for the protection to finish and then
// makes the pages writable again, so the returned memory takes a host read(); without the mutex the protection lands on memory no range owns any
// more and read() fails with EFAULT [-DDM_TEST_DIRTY_PROTECT_NOLOCK: 2 fail].
static void after_lookup(void);
#define DM_DIRTY_AFTER_LOOKUP() after_lookup()
#include "../nvn/nvn_dirty.c"
#include <stdio.h>
#include <errno.h>

module_t g_mod[MAX_MOD];
int g_nmod;
module_t *module_of(const void *p) { (void)p; return NULL; }
void crash_install(void);

static int g_fail;
#define EXPECT(name, want, got) do { \
    long long want_ = (long long)(want), got_ = (long long)(got); \
    printf("%-84s expected %-4lld → got %-4lld %s\n", name, want_, got_, want_ == got_ ? "✓" : "✗"); \
    if (want_ != got_) g_fail++; \
} while (0)

static uint8_t *g_mem; static size_t g_pg;
static _Atomic int g_stage; static pthread_t g_remover;
static void *remover(void *a) { (void)a; nvn_dirty_remove_range(g_mem); return NULL; }   // a guest thread finalizing the pool
static void after_lookup(void) {   // the range found, no page touched yet: the pool's destruction starts now and is given time to finish
    if (!atomic_exchange(&g_stage, 0)) return;
    pthread_create(&g_remover, NULL, remover, NULL); usleep(100000);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); crash_install();
    g_pg = (size_t)getpagesize();
    g_mem = mmap(NULL, 4 * g_pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (g_mem == MAP_FAILED) { perror("mmap"); return 2; }
    nvn_dirty_add_range(g_mem, 4 * g_pg);
    atomic_store(&g_stage, 1);
    nvn_dirty_protect(g_mem, 2 * g_pg);   // an upload protects the first two pages while the pool goes
    pthread_join(g_remover, NULL);
    EXPECT("the pool is gone: no range tracks its memory", 0, nvn_dirty_tracked(g_mem, 64));
    int fd[2]; if (pipe(fd)) { perror("pipe"); return 2; }
    static const char msg[] = "returned memory"; write(fd[1], msg, sizeof msg);
    ssize_t n = read(fd[0], g_mem + g_pg, sizeof msg); int e = n < 0 ? errno : 0;   // the host reads a file into the memory the game got back
    EXPECT("a host read() into the returned memory succeeds [-DDM_TEST_DIRTY_PROTECT_NOLOCK]", (long long)sizeof msg, n);
    EXPECT("... no EFAULT", 0, e == EFAULT);
    printf("t_dirty_retire: %s\n", g_fail ? "failed" : "all passed"); return g_fail != 0;
}
