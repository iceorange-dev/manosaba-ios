// Physical memory in the alias region (hle_svc.c alias_phys, svc MapPhysicalMemory / UnmapPhysicalMemory): 4 KB guest pages on 16 KB host pages.
//   (1) hand-written cases: a host page opens with its first mapped subpage and stays open while any subpage is mapped; an unmapped subpage reads
//       zero while its neighbours keep their contents; a host page with no mapped subpage left is replaced, so mapping it again reads zero; mapping a
//       page twice or unmapping a page that is not mapped changes nothing; 4 MB mapped in one system call and unmapped in one (plan item 301f;
//       -DDM_TEST_ALIAS_PER_PAGE, a call for each host page as before, fails it);
//   (2) the counters behind dm_alias_stat (mapped 4 KB pages, host pages in use) against a reference model over 20000 random map / unmap calls,
//       with ranges that start and end inside host pages, plus the accessibility of every host page the model says is in use;
//   (3) everything unmapped: both counters back to zero.
// Stack region (svc MapMemory / UnmapMemory, the SDK moving a thread's stack from the heap):
//   (4) the contents move in and back; after UnmapMemory a host page no other mapping touches is inaccessible and no longer resident (it used to stay
//       a resident copy: about 6 MB per scene load in manosaba), a host page shared with a neighbouring mapping stays open with the neighbour's data;
//   (5) two threads mapping and unmapping neighbours that share a host page, 3000 times each: every thread always sees its own data.
// Physical memory limit (svc MapPhysicalMemory, as mesosphere KPageTableBase::MapPhysicalMemory): with an application pool of 285 MB
//   (6) mapping up to the pool succeeds; one page more is LimitReached (0x10801) and maps nothing; pages already mapped do not count again;
//       after an unmap the freed amount can be mapped again.
// svc SetHeapSize and GetInfo (11): Horizon's size rules and results (an unknown GetInfo type InvalidEnumValue; -DDM_TEST_GETINFO_OLD fails it), memory given back reads zeros on regrowth, a shrink over a MapMemory source
//   refused; PriorityMask, RandomEntropy and the memory totals as Horizon reports them; MapPhysicalMemory's argument results (totk-mobile's
//   vectors; -DDM_TEST_PHYS_OLD, as before, ends the program on the unaligned address). As before (-DDM_TEST_HEAP_OLD): SetHeapSize(1) is taken, and growing from that
//   unaligned size leaves the page unwritable: the test dies there (SIGBUS)
// svc SetMemoryPermission (Horizon's checks, Atmosphère): (7) unaligned address / size, size 0 and other permissions than none / read / read-write
//   are refused with Horizon's results; read-write and read are accepted.
// rodata (rodata_seal.h, the loader after relocation): (8) only the 16 KB pages wholly inside [RO_OFF, DATA_OFF) become read-only, a page shared
//   with data stays writable, a rodata smaller than a page seals nothing.
// Break context (svc OutputDebugString / Break): (9) the last 8 debug output lines are printed in order before the abort, the older ones not, and
//   the address the SDK's AbortImpl was called from.
// QueryMemory beside MapMemory / UnmapMemory (nxnative 8530042): (10) with 512 other mappings in the registry, four threads query their own mapped
//   stacks while two others unmap and map again those 512: every query reports the querying thread's own range as a stack. Reads without the registry lock
//   saw entries move under them (an unmap moves the last entry into the freed slot): another mapping, or the stack reported free.
// Address regions (plan item 289): (11) the SDK's AllocateAddressRegion as is, the highest region end kept, a refusal counted for its red line;
//   a refusal unseen as before (-DDM_TEST_AR_UNLOGGED): 1 fails.
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../hle/hle_svc.c"
#include "../rodata_seal.h"

struct dm_flags g_flag; uint64_t g_t0_ns; module_t g_mod[MAX_MOD]; int g_nmod;
uint64_t now_ns(void) { return 0; }
uint64_t guest_ns(void) { return 0; }
int svc_thread(int num, uint64_t *regs) { (void)num; (void)regs; return 0; }
module_t *module_of(const void *a) { (void)a; return NULL; }
static uint64_t g_fake_ra;   // the SDK's g_AbortImplReturnAddress for section 9
static Result g_fake_ar_result; static uint64_t g_fake_ar_at;   // the SDK's AllocateAddressRegion for section 11: its answer and the address it gives
static Result fake_alloc_region(uint64_t *out, size_t size) { (void)size; if (g_fake_ar_result) return g_fake_ar_result; *out = g_fake_ar_at; return 0; }
void *module_sym(module_t *m, const char *n) {
    (void)m;
    if (!strcmp(n, "_ZN2nn2os21AllocateAddressRegionEPmm")) return (void *)fake_alloc_region;
    return g_fake_ra && !strcmp(n, "_ZN2nn4diag6detail24g_AbortImplReturnAddressE") ? &g_fake_ra : NULL;
}
void dm_backtrace(uint64_t fp) { (void)fp; }
void hlecount_svc(int n) { (void)n; }
uint64_t dm_entropy64(void) { return 0; }
void thr_counts(int *a, int *b, int *c) { *a = *b = *c = 0; }

static int checks, fails;
#define CHECK(c, ...) do { checks++; int ok_ = !!(c); fails += !ok_; if (!ok_ || verbose) { printf("%s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int verbose = 1;
static sigjmp_buf g_jb; static volatile int g_probing;
static void on_fault(int s) { (void)s; if (g_probing) siglongjmp(g_jb, 1); signal(s, SIG_DFL); }
static int readable(volatile uint8_t *p) {   // 1 = the byte can be read (the host page is open)
    g_probing = 1; int ok = 0;
    if (!sigsetjmp(g_jb, 1)) { (void)*p; ok = 1; }
    g_probing = 0; return ok;
}
static int writable(volatile uint8_t *p) {   // 1 = a byte can be written there
    g_probing = 1; int ok = 0;
    if (!sigsetjmp(g_jb, 1)) { *p = *p; ok = 1; }
    g_probing = 0; return ok;
}
static void map(size_t page, size_t n) { alias_phys(g_alias + page * 0x1000, n * 0x1000, 1, SIZE_MAX); }
static void unmap(size_t page, size_t n) { alias_phys(g_alias + page * 0x1000, n * 0x1000, 0, SIZE_MAX); }

static uint64_t svc(int num, uint64_t a0, uint64_t a1, uint64_t a2) { uint64_t r[32] = {a0, a1, a2}; svc_dispatch(num, r, 0); return r[0]; }
static int resident(void *p) { char v = 0; return mincore(p, 1, &v) == 0 && (v & MINCORE_INCORE); }
static _Atomic int g_mt_bad;
static void *mapper(void *arg) {   // thread 0: 0x40000..0x45000 (host pages 0x40000, 0x44000); thread 1: 0x45000..0x48000 (host page 0x44000)
    int me = (int)(intptr_t)arg; uint8_t *d = g_stack + (me ? 0x45000 : 0x40000); size_t n = me ? 0x3000 : 0x5000;
    uint8_t *s = aligned_alloc(16384, 0x8000);
    for (int it = 0; it < 3000; it++) {
        memset(s, me * 0x40 + (it & 0x3f), n);
        if (svc(0x04, (uint64_t)d, (uint64_t)s, n)) { g_mt_bad++; continue; }
        for (int r = 0; r < 4; r++) { if (d[0] != s[0] || d[n - 1] != s[0]) { g_mt_bad++; break; } d[n / 2] = s[0]; }
        if (svc(0x05, (uint64_t)d, (uint64_t)s, n)) g_mt_bad++;
    }
    free(s); return NULL;
}
static _Atomic int g_q_bad, g_q_done; static _Atomic long g_q_n;
static void *querier(void *arg) {   // section 10: its own stack at 0x4000000 + 0x10000 * i, 0x4000 bytes, queried 50 times a round
    int me = (int)(intptr_t)arg; uint8_t *d = g_stack + 0x4000000 + 0x10000 * (size_t)me; uint8_t *s = aligned_alloc(16384, 0x4000);
    for (int it = 0; it < 15000; it++) {
        if (svc(0x04, (uint64_t)d, (uint64_t)s, 0x4000)) { g_q_bad++; continue; }
        for (int q = 0; q < 50; q++) {
            uint64_t mi[5] = {0}; svc(0x06, (uint64_t)mi, 0, (uint64_t)(d + 0x2000)); g_q_n++;
            if (mi[0] != (uint64_t)d || mi[1] != 0x4000 || (uint32_t)mi[2] != 0x0B) g_q_bad++;
        }
        if (svc(0x05, (uint64_t)d, (uint64_t)s, 0x4000)) g_q_bad++;
    }
    free(s); g_q_done++; return NULL;
}
static uint8_t g_q_src[0x4000];
static void *churner(void *arg) {   // section 10: unmaps and maps again its half of the 512 mappings until the four queriers are done: an unmap moves the
    int me = (int)(intptr_t)arg;    // registry's last entry (often a querier's stack) into a slot near the front, which a query's scan may have passed
    for (unsigned j = 0; g_q_done < 4; j++) {
        uint8_t *d = g_stack + 0x1000000 + 0x4000 * (size_t)(me * 256 + (j % 256));
        if (svc(0x05, (uint64_t)d, (uint64_t)g_q_src, 0x4000) || svc(0x04, (uint64_t)d, (uint64_t)g_q_src, 0x4000)) g_q_bad++;
    }
    return NULL;
}
enum { NP = 4096 };   // pages of the alias region the random test uses (16 MB)
static uint8_t ref[NP];
static size_t ref_mapped(void) { size_t n = 0; for (int i = 0; i < NP; i++) n += ref[i]; return n; }
static size_t ref_hpages(size_t sub) { size_t n = 0; for (size_t h = 0; h < NP; h += sub) { int u = 0; for (size_t k = h; k < h + sub; k++) u |= ref[k]; n += u; } return n; }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);   // what was checked before a crash stays in the log (the old SetHeapSize of section 11 faults)
    signal(SIGBUS, on_fault); signal(SIGSEGV, on_fault);
    mem_init();
    size_t sub = dm_page_size() / 0x1000;
    printf("host page %zu KB, %zu guest pages each\n", dm_page_size() >> 10, sub);

    printf("1 hand-written cases\n");
    map(0, 1);
    CHECK(g_alias_mapped == 1 && g_alias_hpages == 1, "map page 0: 1 page mapped, 1 host page in use (%zu, %zu)", g_alias_mapped, g_alias_hpages);
    memset(g_alias, 0xa1, 0x1000);
    map(1, 1);
    CHECK(g_alias_mapped == 2 && g_alias_hpages == 1, "map page 1 (same host page): 2 mapped, still 1 host page (%zu, %zu)", g_alias_mapped, g_alias_hpages);
    memset(g_alias + 0x1000, 0xb2, 0x1000);
    map(0, 1);
    CHECK(g_alias_mapped == 2 && g_alias_hpages == 1, "map page 0 again: no change (%zu, %zu)", g_alias_mapped, g_alias_hpages);
    unmap(0, 1);
    CHECK(g_alias_mapped == 1 && g_alias_hpages == 1, "unmap page 0: 1 mapped, the host page stays in use for page 1 (%zu, %zu)", g_alias_mapped, g_alias_hpages);
    CHECK(g_alias[0] == 0 && g_alias[0xfff] == 0 && g_alias[0x1000] == 0xb2 && g_alias[0x1fff] == 0xb2, "page 0 reads zero, page 1 kept its contents");
    unmap(0, 1);
    CHECK(g_alias_mapped == 1 && g_alias_hpages == 1, "unmap page 0 again (not mapped): no change (%zu, %zu)", g_alias_mapped, g_alias_hpages);
    unmap(1, 1);
    CHECK(g_alias_mapped == 0 && g_alias_hpages == 0, "unmap page 1: nothing mapped, no host page in use (%zu, %zu)", g_alias_mapped, g_alias_hpages);
    CHECK(!readable(g_alias), "the host page is closed again");
    map(1, 1);
    CHECK(readable(g_alias + 0x1000) && g_alias[0x1000] == 0 && g_alias[0x1fff] == 0, "mapping page 1 again reads zero (fresh memory)");
    unmap(1, 1);
    if (sub > 1) {   // a range that starts and ends inside host pages: pages sub-1 .. 2*sub (3 host pages)
        map(sub - 1, sub + 2);
        CHECK(g_alias_mapped == sub + 2 && g_alias_hpages == 3, "map %zu pages from page %zu: 3 host pages in use (%zu, %zu)", sub + 2, sub - 1, g_alias_mapped, g_alias_hpages);
        unmap(sub, sub);
        CHECK(g_alias_mapped == 2 && g_alias_hpages == 2, "unmap the middle host page's pages: 2 mapped in 2 host pages (%zu, %zu)", g_alias_mapped, g_alias_hpages);
        CHECK(!readable(g_alias + sub * 0x1000) && readable(g_alias + (sub - 1) * 0x1000) && readable(g_alias + 2 * sub * 0x1000), "the middle host page is closed, its neighbours open");
        unmap(0, 3 * sub);
    }
    size_t mb, hmb, c0 = g_alias_calls; map(0, 1024); size_t c1 = g_alias_calls; dm_alias_stat(&mb, &hmb);
    CHECK(mb == 4 && hmb == 4, "dm_alias_stat after mapping 4 MB: %zu MB mapped in %zu MB of host pages", mb, hmb);
    unmap(0, 1024);
    CHECK(c1 - c0 == 1 && g_alias_calls - c1 == 1, "4 MB mapped in one system call and unmapped in one (%zu, %zu)", c1 - c0, g_alias_calls - c1);

    printf("2 random map / unmap against a reference model\n");
    srand(12345); int bad = 0, closed_bad = 0; verbose = 0;
    for (int it = 0; it < 20000; it++) {
        size_t p = (size_t)rand() % NP, n = 1 + (size_t)rand() % (rand() % 8 ? 8 : 96); if (p + n > NP) n = NP - p;
        int m = rand() % 2;
        alias_phys(g_alias + p * 0x1000, n * 0x1000, m, SIZE_MAX);
        for (size_t i = p; i < p + n; i++) ref[i] = (uint8_t)m;
        if (g_alias_mapped != ref_mapped() || g_alias_hpages != ref_hpages(sub)) { if (!bad++) printf("first mismatch at call %d: mapped %zu (model %zu), host pages %zu (model %zu)\n", it, g_alias_mapped, ref_mapped(), g_alias_hpages, ref_hpages(sub)); }
        if (it % 1000 == 999) for (size_t h = 0; h < NP; h += sub) { int u = 0; for (size_t k = h; k < h + sub; k++) u |= ref[k]; if (readable(g_alias + h * 0x1000) != u) closed_bad++; }
    }
    verbose = 1;
    CHECK(!bad, "counters equal the model after every call (%d mismatches)", bad);
    CHECK(!closed_bad, "every host page is open exactly when the model has a mapped subpage in it (%d wrong)", closed_bad);

    printf("3 everything unmapped\n");
    unmap(0, NP);
    CHECK(g_alias_mapped == 0 && g_alias_hpages == 0, "both counters back to zero (%zu, %zu)", g_alias_mapped, g_alias_hpages);

    printf("4 MapMemory / UnmapMemory\n");
    uint8_t *src = aligned_alloc(16384, 0x20000), *srcb = aligned_alloc(16384, 0x4000); for (int i = 0; i < 0x20000; i++) src[i] = (uint8_t)(i * 7 + 1);
    uint8_t *dst = g_stack + 0x10000;
    CHECK(svc(0x04, (uint64_t)dst, (uint64_t)src, 0x10000) == 0 && !memcmp(dst, src, 0x10000), "map 64 KB: the stack region holds the source's contents");
    memset(dst, 0x5a, 0x10000);
    CHECK(svc(0x05, (uint64_t)dst, (uint64_t)src, 0x10000) == 0 && src[0] == 0x5a && src[0xffff] == 0x5a, "unmap: the contents moved back to the source");
    CHECK(!readable(dst) && !readable(dst + 0xc000) && !resident(dst) && !resident(dst + 0xc000), "unmap: the stack region's pages are inaccessible and not resident");
    CHECK(svc(0x05, (uint64_t)dst, (uint64_t)src, 0x10000) == 0xD401, "unmapping it again: InvalidAddress, nothing touched");
    for (int i = 0; i < 0x10000; i++) src[i] = (uint8_t)(i * 3 + 2);
    CHECK(svc(0x04, (uint64_t)dst, (uint64_t)src, 0x10000) == 0 && !memcmp(dst, src, 0x10000), "map again at the same place: the new contents, nothing stale");
    svc(0x05, (uint64_t)dst, (uint64_t)src, 0x10000);
    uint8_t *a = g_stack + 0x20000, *b = g_stack + 0x25000;   // a = 0x20000..0x25000 (host pages 0x20000 and 0x24000), b = 0x25000..0x28000 (host page 0x24000)
    memset(srcb, 0xbb, 0x3000);
    CHECK(svc(0x04, (uint64_t)a, (uint64_t)src, 0x5000) == 0 && svc(0x04, (uint64_t)b, (uint64_t)srcb, 0x3000) == 0, "map two neighbours that share a host page");
    CHECK(svc(0x05, (uint64_t)a, (uint64_t)src, 0x5000) == 0, "unmap the first");
    CHECK(!readable(a) && readable(b) && b[0] == 0xbb && b[0x2fff] == 0xbb, "its own host page is closed; the shared one stays open with the neighbour's data");
    CHECK(svc(0x05, (uint64_t)b, (uint64_t)srcb, 0x3000) == 0 && !readable(b) && !readable(g_stack + 0x24000), "unmap the neighbour: the shared host page is closed too");

    printf("5 two threads on neighbours sharing a host page\n");
    pthread_t th[2]; for (int i = 0; i < 2; i++) pthread_create(&th[i], NULL, mapper, (void *)(intptr_t)i);
    for (int i = 0; i < 2; i++) pthread_join(th[i], NULL);
    CHECK(!g_mt_bad, "3000 map / write / check / unmap rounds on each thread: %d wrong", g_mt_bad);

    printf("6 physical memory limit\n");
    setenv("DM_APP_POOL_ADD_MB", "-3000", 1);   // pool 3285 - 3000 = 285 MB; no images or heap in this test: the alias region may hold 285 MB
    uint64_t A = (uint64_t)g_alias;
    CHECK(svc(0x2C, A, 280ull << 20, 0) == 0 && g_alias_mapped == (280u << 8), "map 280 MB: success (%zu pages)", g_alias_mapped);
    CHECK(svc(0x2C, A + (280ull << 20), 8ull << 20, 0) == 0x10801 && g_alias_mapped == (280u << 8), "8 MB more: LimitReached, nothing mapped (%zu pages)", g_alias_mapped);
    CHECK(svc(0x2C, A + (276ull << 20), 10ull << 20, 0) == 0x10801 && g_alias_mapped == (280u << 8), "10 MB of which 4 MB are already mapped (6 MB new, 286 MB in all): LimitReached");
    CHECK(svc(0x2C, A + (277ull << 20), 8ull << 20, 0) == 0 && g_alias_mapped == (285u << 8), "8 MB of which 3 MB are already mapped: exactly the limit, success");
    CHECK(svc(0x2C, A, 16ull << 20, 0) == 0, "mapping pages that are all mapped already: success");
    CHECK(svc(0x2C, A + (285ull << 20), 0x1000, 0) == 0x10801, "one page past the limit: LimitReached");
    CHECK(svc(0x2D, A, 100ull << 20, 0) == 0 && svc(0x2C, A + (285ull << 20), 100ull << 20, 0) == 0 && g_alias_mapped == (285u << 8), "unmap 100 MB, then 100 MB elsewhere fits again");
    svc(0x2D, A, 400ull << 20, 0);
    CHECK(g_alias_mapped == 0, "everything unmapped");

    printf("7 SetMemoryPermission\n");
    { uint64_t h = (uint64_t)g_alias;
      CHECK(svc(0x02, h + 0x800, 0x1000, 3) == 0xCC01, "unaligned address: InvalidAddress");
      CHECK(svc(0x02, h, 0x800, 3) == 0xCA01, "unaligned size: InvalidSize");
      CHECK(svc(0x02, h, 0, 3) == 0xCA01, "size 0: InvalidSize");
      CHECK(svc(0x02, h, 0x1000, 2) == 0xD801, "write without read: InvalidNewMemoryPermission");
      CHECK(svc(0x02, h, 0x1000, 5) == 0xD801, "read + execute: InvalidNewMemoryPermission");
      CHECK(svc(0x02, h, 0x1000, 3) == 0, "read-write: accepted");
      CHECK(svc(0x02, h, 0x1000, 1) == 0, "read: accepted (reported, not applied)"); }

    printf("8 rodata sealed after relocation\n");
    { uint8_t *b = mmap(NULL, 0x20000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); uint64_t lo, hi;
      CHECK(dm_rodata_seal(b, 0x5000, 0x5000, 0x19000, 0x1C000, &lo, &hi) == 0 && lo == 0x8000 && hi == 0x18000, "text to 0x5000, data from 0x19000: pages 0x8000..0x18000 sealed (%#llx..%#llx)", (unsigned long long)lo, (unsigned long long)hi);
      CHECK(!writable(b + 0x8000) && !writable(b + 0x17ff8), "a write into the sealed rodata faults (first and last page)");
      CHECK(readable(b + 0x8000), "... and it can still be read");
      CHECK(writable(b + 0x18000) && writable(b + 0x19000), "the page shared with data stays writable");
      CHECK(writable(b + 0x7ff8), "the page shared with text keeps its own protection (writable in this test)");
      uint8_t *c = mmap(NULL, 0x20000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
      CHECK(dm_rodata_seal(c, 0x5000, 0x5000, 0x7000, 0x1C000, &lo, &hi) == 0 && lo == hi && writable(c + 0x5000), "rodata within one page: nothing sealed");
      CHECK(dm_rodata_seal(c, 0x5000, 0x5000, 0x19000, 0x10000, &lo, &hi) == 0 && hi == 0x10000 && writable(c + 0x10000), "the key page bounds the sealed range");
      munmap(b, 0x20000); munmap(c, 0x20000); }

    printf("9 Break context\n");
    { char s[32];
      for (int i = 0; i < 10; i++) { snprintf(s, sizeof s, "line %d\n", i); svc(0x27, (uint64_t)s, strlen(s), 0); }
      g_nmod = 1; g_fake_ra = 0x1234567;
      const char *td = getenv("TMPDIR"); char pth[1024]; snprintf(pth, sizeof pth, "%s/t_guest_mem_brk.XXXXXX", td ? td : "/private/tmp");
      int fd = mkstemp(pth), save = dup(2); fflush(stderr); dup2(fd, 2); abort_context(); fflush(stderr); dup2(save, 2); close(save);
      char out[4096] = {0}; lseek(fd, 0, SEEK_SET); read(fd, out, sizeof out - 1); close(fd); unlink(pth);
      const char *a = strstr(out, "[guest] line 2"), *b = strstr(out, "[guest] line 9");
      CHECK(a && b && a < b && !strstr(out, "[guest] line 1\n"), "the last 8 debug lines (2..9) in order, the older ones not");
      CHECK(strstr(out, "AbortImpl was called from ?+0x1234567") != NULL, "the AbortImpl caller from g_AbortImplReturnAddress");
      g_nmod = 0; g_fake_ra = 0; }

    printf("10 QueryMemory while other threads map and unmap\n");
    { int ok = 1;
      for (int i = 0; i < 512 && ok; i++) ok = svc(0x04, (uint64_t)(g_stack + 0x1000000 + 0x4000 * (size_t)i), (uint64_t)g_q_src, 0x4000) == 0;
      pthread_t q[4], c[2];
      for (int i = 0; i < 4; i++) pthread_create(&q[i], NULL, querier, (void *)(intptr_t)i);
      for (int i = 0; i < 2; i++) pthread_create(&c[i], NULL, churner, (void *)(intptr_t)i);
      for (int i = 0; i < 4; i++) pthread_join(q[i], NULL);
      for (int i = 0; i < 2; i++) pthread_join(c[i], NULL);
      CHECK(ok && !g_q_bad, "%ld queries of a thread's own stack among 512 mappings, two threads unmapping and mapping them again: %d wrong", (long)g_q_n, (int)g_q_bad);
      for (int i = 0; i < 512; i++) svc(0x05, (uint64_t)(g_stack + 0x1000000 + 0x4000 * (size_t)i), (uint64_t)g_q_src, 0x4000); }

    printf("11 SetHeapSize and GetInfo as Horizon (plan item 143; switchbrew, totk-mobile's tests/test_kernel.c vectors)\n");
    {   uint64_t r[32];
#define GI(id, h, sub) (memset(r, 0, sizeof r), r[1] = (id), r[2] = (h), r[3] = (sub), svc_dispatch(0x29, r, 0), r[0])
#define HEAP(sz) (memset(r, 0, sizeof r), r[1] = (sz), svc_dispatch(0x01, r, 0), r[0])
        CHECK(HEAP(1) == 0xCA01, "SetHeapSize(1): InvalidSize (not a multiple of 2 MiB)");
        CHECK(HEAP(0x140000000ull) == 0xCA01, "SetHeapSize(5 GiB): InvalidSize (above 4 GiB)");
        if (g_heap_cap + 0x200000 <= 0x100000000ull) CHECK(HEAP((g_heap_cap + 0x200000) & ~(size_t)0x1fffff) == 0xD001, "SetHeapSize above the heap region (%zu MiB): OutOfMemory 0xD001", g_heap_cap >> 20);
        else printf("  - the heap region (%zu MiB) is above 4 GiB here: every aligned size above it is InvalidSize first (iOS has a smaller one)\n", g_heap_cap >> 20);
        CHECK(HEAP(0x400000) == 0 && r[1] == (uint64_t)g_heap, "SetHeapSize(4 MiB): the heap's base");
        *(uint64_t *)g_heap = 42; *(uint64_t *)(g_heap + 0x200000) = 99;
        CHECK(HEAP(0x200000) == 0 && HEAP(0x400000) == 0 && *(uint64_t *)g_heap == 42 && *(uint64_t *)(g_heap + 0x200000) == 0, "shrunk to 2 MiB and grown again: the kept part keeps 42, the part given back reads 0");
        uint8_t *dst = g_stack + 0x2000000; *(uint64_t *)(g_heap + 0x200000) = 7;
        CHECK(svc(0x04, (uint64_t)dst, (uint64_t)(g_heap + 0x200000), 0x1000) == 0, "a thread stack mapped from the heap's second 2 MiB (MapMemory)");
        CHECK(HEAP(0x200000) == 0xD401, "shrinking over that source: InvalidCurrentMemory 0xD401");
        CHECK(svc(0x05, (uint64_t)dst, (uint64_t)(g_heap + 0x200000), 0x1000) == 0 && HEAP(0x200000) == 0 && HEAP(0) == 0, "unmapped, the shrink goes through, and to 0");
        CHECK(GI(1, 0xffff8001u, 0) == 0 && r[1] == 0x0ffffffff0000000ull, "PriorityMask: priorities 28-59 (the NPDM's ThreadInfo), got %#llx", (unsigned long long)r[1]);
        int same = 1; uint64_t e[4];
        for (int i = 0; i < 4; i++) { same &= GI(11, 0, i) == 0; e[i] = r[1]; same &= GI(11, 0, i) == 0 && r[1] == e[i]; }
        CHECK(same, "RandomEntropy 0-3: the same value on every call");
        CHECK(GI(11, 0, 4) == 0xE801 && GI(11, 0xffff8001u, 0) == 0xE401, "RandomEntropy subtype 4: InvalidCombination; with a handle: InvalidHandle");
        GI(6, 0xffff8001u, 0); uint64_t tot = r[1]; GI(21, 0xffff8001u, 0); uint64_t totn = r[1]; GI(16, 0xffff8001u, 0); uint64_t sres = r[1];
        GI(7, 0xffff8001u, 0); uint64_t used = r[1]; GI(22, 0xffff8001u, 0); uint64_t usedn = r[1];
        CHECK(sres == dm_game.npdm_sysres && sres && tot == totn + sres && used == usedn + sres, "SystemResourceSize the NPDM's (%#llx); Total = TotalNonSystem + it, Used = UsedNonSystem + it", (unsigned long long)sres);
        CHECK(GI(0x7fff, 0xffff8001u, 0) == 0xF001 && r[1] == 0 && GI(9999, 0, 0) == 0xF001, "an unknown type: InvalidEnumValue 0xF001 (Horizon; plan item 160), it succeeded with 0");
#define PHYS(n, a, sz) (memset(r, 0, sizeof r), r[0] = (a), r[1] = (sz), svc_dispatch((n), r, 0), r[0])
        uint64_t al = (uint64_t)g_alias;
        CHECK(PHYS(0x2C, al + 1, 0x1000) == 0xCC01, "MapPhysicalMemory at an unaligned address: InvalidAddress 0xCC01 (it ended the program)");
        CHECK(PHYS(0x2C, al, 0) == 0xCA01 && PHYS(0x2C, al, 0x1001) == 0xCA01, "size 0 or unaligned: InvalidSize 0xCA01");
        CHECK(PHYS(0x2C, al + g_alias_cap, 0x1000) == 0xDC01, "just past the alias region: InvalidMemoryRegion 0xDC01 (it said 0xD401)");
        CHECK(PHYS(0x2C, UINT64_MAX - 0xfff, 0x1000) == 0xDC01 && PHYS(0x2C, al, UINT64_MAX - 0xfff) == 0xDC01, "a range that wraps around: InvalidMemoryRegion");
#undef PHYS
#undef GI
#undef HEAP
    }

    // (11) Address regions (plan item 289): the SDK's AllocateAddressRegion called as is, its region's end in the alias region kept as the highest,
    // and a refusal passed back and counted (the log's red line)
    {   uint64_t at = 0, top; int refused; g_fake_ar_at = (uint64_t)(uintptr_t)g_alias + 0x3e00000;
        Result r = hle_os_alloc_region(&at, 0x10000000);
        svc_region_stat(&top, &refused);
        CHECK(r == 0 && at == g_fake_ar_at && top == 0x3e00000 + 0x10000000, "an address region: the SDK's address, its end (%llu MB) the highest", (unsigned long long)(top >> 20));
        g_fake_ar_at -= 0x1000000; hle_os_alloc_region(&at, 0x100000); svc_region_stat(&top, &refused);
        CHECK(top == 0x3e00000 + 0x10000000, "... a region ending lower leaves the highest end");
        g_fake_ar_result = 0x1803; r = hle_os_alloc_region(&at, 0x40000000); svc_region_stat(&top, &refused); g_fake_ar_result = 0;
        CHECK(r == 0x1803 && refused == 1, "a refused address region: its result passed back, and counted (logged red)");
    }

    printf("t_guest_mem: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
