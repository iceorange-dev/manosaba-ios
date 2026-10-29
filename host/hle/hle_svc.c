// Horizon kernel system calls the SDK runtime makes: memory layout, heap, queries, debug output, timing, process info.
// Register convention as in the public Horizon ABI: inputs w1.., outputs w0 = Result, x1 = value (GetInfo / SetHeapSize).
#include "dm.h"
#include "hle.h"
#include "game.h"
#include <stdatomic.h>
#include <sys/mman.h>
#include <TargetConditionals.h>
#include <mach/mach.h>
#include "guest_as.h"
#include <errno.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>

// One reserved block of guest address space, partitioned the Horizon way (address values are the host's real mmap addresses). It takes address space, not memory.
// Unity never calls SetHeapSize: its memory comes from the alias region through the SDK's VammManager, which keeps the first 0x3fe00000 of that region for its
// own node heap (0x3e00000 where a game's rules cut it: plan item 289, both games). A real 39-bit process has a 64 GB alias region; here Mac reserves 12 GB (alias 4 GB) and iOS steps down from 6 GB (see mem_init).
// DM_GUEST_AS_GB=<n> (n >= 6) sets the total.
static size_t g_reserve;

#define RESERVE g_reserve
static uint8_t *g_as;              // ASLR region base = the whole block
static uint8_t *g_heap; static size_t g_heap_cur, g_heap_cap; static int g_heap_rejects;
// The address range lies entirely within the currently opened (readable and writable) part of the guest heap: such reads need no kernel trap to check, memcpy directly (used where guest memory is read every tick, such as diag.c tk_rd)
int dm_guest_heap_readable(uint64_t a, size_t n) { uint64_t b = (uint64_t)(uintptr_t)g_heap; size_t cur = g_heap_cur; return g_heap && a >= b && n <= cur && a - b <= cur - n; }
static uint8_t *g_alias; static size_t g_alias_cap;
static uint8_t *g_stack; static size_t g_stack_cap;

// Physical memory in the alias region (SDK VammManager): Horizon maps it in 4 KB pages, the host page is 16 KB on Apple silicon. A host page is
// accessible while any of its 4 KB subpages is mapped. An unmapped subpage is zeroed while its host page stays in use; a host page with no mapped
// subpage left is replaced by a fresh anonymous PROT_NONE page, so a later map always reads zero (fresh physical memory on the real device).
static uint8_t *g_alias_bits; static pthread_mutex_t g_alias_lock = PTHREAD_MUTEX_INITIALIZER;   // one bit per 4 KB page of the alias region
static size_t g_alias_mapped, g_alias_hpages;   // mapped 4 KB pages; host pages with at least one mapped subpage (a whole host page counts toward memory once touched)
static int alias_bit(size_t i) { return g_alias_bits[i >> 3] >> (i & 7) & 1; }
// Alias region readout (any thread, no lock: two word reads): MB the game has mapped, and MB of host pages that mapping keeps in use; the difference
// is what 4 KB mappings scattered over 16 KB host pages cost on top
void dm_alias_stat(size_t *mapped_mb, size_t *host_mb) { *mapped_mb = g_alias_mapped >> 8; *host_mb = g_alias_hpages * vm_page_size >> 20; }
// Returns 1 (and maps nothing) when mapping would take more than limit_pages 4 KB pages in all: the console's kernel reserves the pages not yet mapped
// from the process's physical memory limit and fails with LimitReached (mesosphere KPageTableBase::MapPhysicalMemory)
static int alias_phys(uint8_t *a, size_t size, int map, size_t limit_pages) {
    size_t hp = vm_page_size, sub = hp / 0x1000;
    pthread_mutex_lock(&g_alias_lock);
    if (!g_alias_bits && !(g_alias_bits = calloc(g_alias_cap / 0x1000 / 8 + 1, 1))) die("cannot allocate the alias region bitmap");
    size_t first = (size_t)(a - g_alias) / 0x1000, n = size / 0x1000;
    if (map) { size_t add = 0; for (size_t i = first; i < first + n; i++) add += !alias_bit(i);
               if (g_alias_mapped + add > limit_pages) { pthread_mutex_unlock(&g_alias_lock); return 1; } }
    // Usage readout: mapped 4 KB pages, peak, highest offset used; one log line each time the peak passes another 128 MB (sizes the alias region; the iOS reservation has a ceiling)
    static size_t peak, top, logged;
    for (size_t i = first; i < first + n; i++) { int was = alias_bit(i); if (map && !was) g_alias_mapped++; else if (!map && was) g_alias_mapped--; }
    if (map && first + n > top) top = first + n;
    for (size_t h = first / sub * sub; h < first + n; h += sub) {   // every host page the range touches
        int before = 0, used = 0; for (size_t k = h; k < h + sub; k++) before |= alias_bit(k);
        for (size_t k = h > first ? h : first; k < h + sub && k < first + n; k++) if (map) g_alias_bits[k >> 3] |= 1 << (k & 7); else g_alias_bits[k >> 3] &= ~(1 << (k & 7));
        for (size_t k = h; k < h + sub; k++) used |= alias_bit(k);
        if (used && !before) g_alias_hpages++; else if (!used && before) g_alias_hpages--;
        uint8_t *hpage = g_alias + h * 0x1000;
        if (map) { if (mprotect(hpage, hp, PROT_READ | PROT_WRITE)) die("MapPhysicalMemory mprotect %p errno=%d", hpage, errno); }
        else if (used) { for (size_t k = h; k < h + sub; k++) if (k >= first && k < first + n) memset(g_alias + k * 0x1000, 0, 0x1000); }
        else if (mmap(hpage, hp, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == MAP_FAILED) die("UnmapPhysicalMemory: replacing page %p failed, errno=%d", hpage, errno);
    }
    if (g_alias_mapped > peak) { peak = g_alias_mapped; if ((peak >> 15) > logged) { logged = peak >> 15;
        LOG("alias region peak use %zu MB (highest offset %zu MB, region %zu MB; host pages in use now %zu MB)", peak >> 8, top >> 8, g_alias_cap >> 20, g_alias_hpages * hp >> 20); } }
    pthread_mutex_unlock(&g_alias_lock);
    return 0;
}

static void mem_init(void) {
    if (g_as) return;
    size_t alias_cap = TARGET_OS_IPHONE ? 0xC0000000ull : 0x100000000ull;
    g_reserve = TARGET_OS_IPHONE ? 6ull << 30 : 12ull << 30;
    const char *e = getenv("DM_GUEST_AS_GB"); if (e && atoi(e) >= 6) g_reserve = (size_t)atoi(e) << 30;
#if TARGET_OS_IPHONE
    // Without the extended virtual addressing entitlement (a free account cannot sign it) the largest single reservation varies by device and OS:
    // 6 GB failed on an iPhone 17 Pro (iOS 26.4) before the increased memory limit was signed in (plan item 65); with it the whole 6 GB is reserved
    // (2026-10-23, a free account: "iOS reserved 6144 MB of 6144"; LASRecomp PR #238 saw the address space follow that entitlement, plan item 258).
    // From 6 GB down by 256 MB to what the game needs, in the lowest free gap from 4 GB that ends
    // below 2^39 (guest_as.h, plan item 62a; done at the top of main(), before UIKit and Metal map theirs: svc_reserve_early), the free gaps logged
    // when it is less than 6 GB; mmap's own choice if that fails. Everything except the 256 MB prefix, the 256 MB stack region and the profile's
    // heap (guest_heap_mb) goes to the alias region (manosaba on the Mac: a 90 s story run peaks at 1166 MB of alias used, highest offset 2460 MB),
    // which needs 1.5 GB at least (VammManager keeps ~1 GB where no cut makes it 62 MB, plan item 289)
    if (!e) {
        size_t heap = (size_t)(dm_game.guest_heap_mb ? dm_game.guest_heap_mb : 256) << 20, want = g_reserve, least = 0x20000000ull + heap + 0x60000000ull;
        if (least > want) die("reserve guest AS: a %zu MB heap needs %zu MB, more than the %zu MB asked for", heap >> 20, least >> 20, want >> 20);
        static as_gap gaps[1024]; int ng = as_gaps(gaps, 1024, AS_FROM, AS_LIMIT), ingap = 0; uint64_t at = 0, size = 0;
        if (as_pick(gaps, ng, want, least, 0x10000000ull, 2ull << 20, &at, &size) && (g_as = as_reserve_at(at, size))) { g_reserve = size; ingap = 1; }
        else for (size_t r = want; r >= least; r -= 0x10000000ull) {
            g_as = mmap(NULL, r, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (g_as != MAP_FAILED) { g_reserve = r; break; }
            g_as = NULL;
        }
        if (!g_as || g_reserve < want) as_log_gaps(stderr, gaps, ng);
        if (!g_as) die("reserve guest AS: not even the %zu MB a %zu MB heap needs could be reserved (free gaps above)", least >> 20, heap >> 20);
        alias_cap = g_reserve - 0x20000000ull - heap;
        LOG("guest address space: iOS reserved %zu MB of %zu (alias %zu MB, heap %zu MB)%s", g_reserve >> 20, want >> 20, alias_cap >> 20, heap >> 20, ingap ? " in a free gap" : " where mmap put it");
    }
#endif
    if (!g_as) g_as = mmap(NULL, RESERVE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (g_as == MAP_FAILED) die("reserve guest AS %#zx", RESERVE);
    if ((uint64_t)(uintptr_t)g_as + RESERVE > AS_LIMIT) {   // past the Switch's 39-bit space (macOS 27 reserves about 24 GB of its own somewhere in 0x70_0000_0000..2^39, MacNativeGames)
        static as_gap gaps[1024]; uint8_t *was = g_as; uint64_t at, size;
        int ng = as_gaps(gaps, 1024, AS_FROM, AS_LIMIT);
        g_as = as_pick(gaps, ng, RESERVE, RESERVE, 0x10000000ull, 2ull << 20, &at, &size) ? as_reserve_at(at, size) : NULL;
        if (!g_as) { as_log_gaps(stderr, gaps, ng); die("reserve guest AS: %p + %zu MB ends past 2^39 and no free gap below holds it", (void *)was, RESERVE >> 20); }
        munmap(was, RESERVE);
        LOG("guest address space: moved below 2^39 from %p", (void *)was);
    }
    // Partition inside the block: 256 MB prefix, alias region, stack region 256 MB, heap the rest
    g_alias = g_as + 0x10000000; g_alias_cap = alias_cap;
    g_stack = g_alias + g_alias_cap; g_stack_cap = 0x10000000;
    g_heap  = g_stack + g_stack_cap; g_heap_cap  = RESERVE - (size_t)(g_heap - g_as);
    LOG("guest AS %p +%#llx  heap %p alias %p stack %p (ends %llu MB below 2^39)", g_as, (unsigned long long)RESERVE, g_heap, g_alias, g_stack,
        (unsigned long long)((AS_LIMIT - ((uint64_t)(uintptr_t)g_as + RESERVE)) >> 20));
}
// The guest address space reserved now (iOS main(), before UIKit and Metal take address space: plan item 62a); otherwise at the first svc
void svc_reserve_early(void) { mem_init(); }

// Application memory pool reported to the game: the 3285 MB a 4 GB Switch gives applications, + DM_APP_POOL_ADD_MB (default 512; on iOS the profile's
// ios_pool_add_mb, Tears of the Kingdom 0, plan item 62a; may be negative).
// GetInfo TotalMemory reports it and SetHeapSize refuses a heap that does not fit beside the loaded images.
#define APP_POOL 0xCD500000ull
static uint64_t app_pool(void) {
    static uint64_t v;
    if (!v) { const char *e = getenv("DM_APP_POOL_ADD_MB"); long add = e ? atol(e) : TARGET_OS_IPHONE ? dm_game.ios_pool_add_mb : 512; v = APP_POOL + ((uint64_t)add << 20);
              if (e) LOG("DM_APP_POOL_ADD_MB: application pool %llu MB (Switch 3285 MB %+ld)", (unsigned long long)(v >> 20), add); }
    return v;
}
static size_t image_bytes(void) { size_t n = 0; for (int i = 0; i < g_nmod; i++) n += g_mod[i].size; return (n + 0x1fffff) & ~(size_t)0x1fffff; }

static module_t *mod_named(const char *n) { for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, n)) return &g_mod[i]; return NULL; }
// The cheat VM's memory regions (cheats.c, plan item 150b): {main's image, the heap, the alias region, the whole reserved block (ASLR)}; 0 = none yet
void dm_guest_bases(uint64_t out[4]) {
    module_t *m = mod_named("main");
    out[0] = m ? (uint64_t)(uintptr_t)m->base : 0; out[1] = (uint64_t)(uintptr_t)g_heap; out[2] = (uint64_t)(uintptr_t)g_alias; out[3] = (uint64_t)(uintptr_t)g_as;
}
// The SDK's own allocator, called as is: a null return is logged (first 8), since callers in games often do not check it
HLE_FORCE("_ZN2nn3mem17StandardAllocator8AllocateEmm", "the SDK allocator, logging null returns", void *, hle_stdalloc_allocate, void *self, size_t size, size_t align) {
    static void *(*orig)(void *, size_t, size_t);
    if (!orig) { orig = (void *(*)(void *, size_t, size_t))module_sym(mod_named("sdk"), "_ZN2nn3mem17StandardAllocator8AllocateEmm"); if (!orig) die("StandardAllocator::Allocate not found in sdk"); }
    void *p = orig(self, size, align);
    if (!p && size) { static _Atomic int n; if (atomic_fetch_add(&n, 1) < 8) LOG("[red] game heap allocation returned null: %zu bytes (alignment %zu), allocator %p", size, align, self); }
    return p;
}
// Address regions (plan item 289, after nx2android 8056043, MIT, written anew): nn::os::AllocateAddressRegion places a region in the alias region (the SDK's
// VammManager, after its node area), and only the pages mapped into it later reach MapPhysicalMemory, so a refused region left nothing in the log. Unity 6
// takes all its memory so; with no region left its allocator returns null and IL2CPP's collector aborts (Graveyard Keeper 2, about 24 minutes in). The SDK's
// own, called as is: the highest region end logged at each 512 MB it passes, a refusal red (the first 8). -DDM_TEST_AR_UNLOGGED: a refusal as before, unseen
static _Atomic uint64_t g_ar_top; static _Atomic int g_ar_refused;
void svc_region_stat(uint64_t *top, int *refused) { *top = atomic_load(&g_ar_top); *refused = atomic_load(&g_ar_refused); }   // highest region end (offset in the alias region), refusals
HLE_FORCE_OPT("_ZN2nn2os21AllocateAddressRegionEPmm", "the SDK's own, the highest region end and refusals logged", Result, hle_os_alloc_region, uint64_t *out, size_t size) {
    static Result (*orig)(uint64_t *, size_t);
    if (!orig && !(orig = (Result (*)(uint64_t *, size_t))module_sym(mod_named("sdk"), "_ZN2nn2os21AllocateAddressRegionEPmm"))) die("nn::os::AllocateAddressRegion not found in sdk");
    Result r = orig(out, size);
#ifdef DM_TEST_AR_UNLOGGED
    if (r) return r;
#endif
    uint64_t base = (uint64_t)(uintptr_t)g_alias;
    if (r) {
        if (atomic_fetch_add(&g_ar_refused, 1) < 8)
            LOG("[red] address region of %.1f MB refused (result %#x): regions end up to %llu MB of the %zu MB alias region, %zu MB of it mapped", size / 1048576.0, r,
                (unsigned long long)(atomic_load(&g_ar_top) >> 20), g_alias_cap >> 20, g_alias_mapped >> 8);
    } else if (*out >= base && size <= g_alias_cap && *out - base <= g_alias_cap - size) {
        uint64_t end = *out - base + size, t = atomic_load(&g_ar_top);
        while (end > t && !atomic_compare_exchange_weak(&g_ar_top, &t, end)) {}
        if (end > t && end >> 29 != t >> 29) LOG("address regions end up to %llu MB of the %zu MB alias region", (unsigned long long)(end >> 20), g_alias_cap >> 20);
    }
    return r;
}

// For memory accounting (memstat.m): the range of the whole guest address space block (base = NULL when not initialized)
void svc_guest_range(uint8_t **base, size_t *size) { *base = g_as; *size = RESERVE; }
// The parts of that block (memstat.m splits the guest's memory by them): alias region (physical memory the game maps), stack region (MapMemory), heap
void svc_guest_layout(uint8_t **alias, size_t *alias_n, uint8_t **stack, size_t *stack_n, uint8_t **heap, size_t *heap_n) { *alias = g_alias; *alias_n = g_alias_cap; *stack = g_stack; *stack_n = g_stack_cap; *heap = g_heap; *heap_n = g_heap_cap; }

enum { I_CoreMask=0, I_PriorityMask=1, I_AliasAddr=2, I_AliasSize=3, I_HeapAddr=4, I_HeapSize=5,
       I_TotalMem=6, I_UsedMem=7, I_DebuggerAttached=8, I_ResourceLimit=9, I_IdleTick=10, I_RandomEntropy=11,
       I_AslrAddr=12, I_AslrSize=13, I_StackAddr=14, I_StackSize=15, I_SysResTotal=16, I_SysResUsed=17,
       I_ProgramId=18, I_InitProcIdRange=19, I_UserExcAddr=20, I_TotalNonSys=21, I_UsedNonSys=22, I_IsApplication=23,
       I_FreeThreadCount=24, I_AliasExtraSize=28 };

static uint8_t g_seen[256];
static uint64_t sysres_total(void) { return dm_game.npdm_sysres ? dm_game.npdm_sysres : 0x2000000; }   // the NPDM's SystemResourceSize (game.json npdm)
static uint64_t g_entropy[4];
void entropy_init(void) { uint64_t dm_entropy64(void); for (int i = 0; i < 4; i++) g_entropy[i] = dm_entropy64(); }   // GetInfo RandomEntropy

// Registry of mapped ranges (MapMemory stacks, MapPhysicalMemory alias pages), QueryMemory reports from it; the SDK relies on it to find free space.
// Changes happen under g_mapmem_mu (MapMemory / UnmapMemory as a whole) and g_map_lock (the change itself); QueryMemory reads under g_map_lock: an unmap
// moves the last entry into the freed slot, and a scan without the lock missed a moving entry (a mapped stack reported free) or read another one in
// its slot (nxnative 8530042, MIT; t_guest_mem 10)
#define MAX_MAP 4096
static struct { uint64_t base, size, src; uint32_t state; } g_map[MAX_MAP];
static int g_nmap;
static pthread_mutex_t g_map_lock = PTHREAD_MUTEX_INITIALIZER;
static int map_find(uint64_t a, uint64_t n) {       // returns a 1-based index, 0 = no overlap
    for (int i = 0; i < g_nmap; i++) if (a < g_map[i].base + g_map[i].size && g_map[i].base < a + n) return i + 1;
    return 0;
}
static void map_add(uint64_t b, uint64_t n, uint64_t src, uint32_t st) {
    pthread_mutex_lock(&g_map_lock);
    if (g_nmap >= MAX_MAP) die("mapping table full");
    g_map[g_nmap++] = (typeof(g_map[0])){b, n, src, st};
    pthread_mutex_unlock(&g_map_lock);
}
static void map_del(int i) { pthread_mutex_lock(&g_map_lock); g_map[i] = g_map[--g_nmap]; pthread_mutex_unlock(&g_map_lock); }
// The MapMemory mapping holding a: its base and *size, 0 when a is in none (hle_thread.c: a new thread's stack)
uint64_t dm_mapping_of(uint64_t a, uint64_t *size) {
    pthread_mutex_lock(&g_map_lock);
    int k = map_find(a, 1); uint64_t b = k ? g_map[k - 1].base : 0; *size = k ? g_map[k - 1].size : 0;
    pthread_mutex_unlock(&g_map_lock);
    return b;
}
static pthread_mutex_t g_mapmem_mu = PTHREAD_MUTEX_INITIALIZER;   // MapMemory / UnmapMemory as a whole (check, copy, register, release pages)

int svc_thread(int num, uint64_t *regs);   // hle_thread.c: thread and synchronization kind

// Host calls (svc 0x80–0xff, hle.h): handlers registered by the game's hook code
static int (*g_hcall[0x80])(uint64_t *regs);
void dm_host_call_register(int num, int (*fn)(uint64_t *regs)) {
    if (num < 0x80 || num > 0xff) die("host call number %#x out of range (0x80–0xff)", num);
    if (g_hcall[num - 0x80] && g_hcall[num - 0x80] != fn) die("host call %#x registered twice", num);
    g_hcall[num - 0x80] = fn;
}

// ---------- Managed heap census (diagnostic): DM_HEAP_CENSUS=<second>[,<second>…] after start ----------
// An IL2CPP object begins with a pointer to its Il2CppClass; the class holds its name and namespace strings at +0x10 / +0x18 and a field that
// points back at the class itself (il2cpp-class-internals.h, "hack to pretend we are a MonoVTable"). Its offset is found at the first census,
// the one most candidate words agree on. A census reads every mapped page of the alias region word by word, under the alias lock so no page
// goes away meanwhile, and counts each word that points at a class as one object of that class (references held in other structures count too,
// they are few). The 40 most numerous classes and the 40 that grew most since the previous census are logged.
static int alias_readable(uint64_t a, size_t n) {   // caller holds g_alias_lock
    uint64_t b = (uint64_t)(uintptr_t)g_alias;
    if (a < b || a + n > b + g_alias_cap || a + n < a) return 0;
    for (size_t i = (a - b) >> 12, e = (a + n - 1 - b) >> 12; i <= e; i++) if (!alias_bit(i)) return 0;
    return 1;
}
static int census_byte_ok(uint64_t a) {   // a readable byte: a mapped alias page, or a module's read-only data (where IL2CPP keeps some names)
    if (alias_readable(a, 1)) return 1;
    for (int i = 0; i < g_nmod; i++) { uint64_t lo = (uint64_t)g_mod[i].base + g_mod[i].ro_off, hi = (uint64_t)g_mod[i].base + g_mod[i].data_off; if (a >= lo && a < hi) return 1; }
    return 0;
}
static int census_str(uint64_t p, char *out, size_t n) {
    if (!p || !census_byte_ok(p)) return 0;
    size_t k = 0;
    for (; k + 1 < n; k++) { if (!((p + k) & 0xfff) && !census_byte_ok(p + k)) break; char c = ((const char *)p)[k]; if (!c) break; if (c < 0x20 || c > 0x7e) return 0; out[k] = c; }
    out[k] = 0; return k > 0;
}
enum { CEN_N = 1 << 17 };
static struct cen_ent { uint64_t klass; uint32_t n, prev; } *g_cen;
static int g_cen_self = -1;   // offset of the class's pointer to itself
static void heap_census(int round) {
    uint64_t t0 = now_ns();
    pthread_mutex_lock(&g_alias_lock);
    if (!g_alias_bits) { pthread_mutex_unlock(&g_alias_lock); return; }
    size_t np = g_alias_cap >> 12; uint64_t b = (uint64_t)(uintptr_t)g_alias;
    if (g_cen_self < 0) {   // find the offset: words pointing into the alias region whose target holds the same value at +off, with a name string at +0x10
        uint32_t hits[32] = {0}; uint64_t seen = 0;
        for (size_t i = 0; i < np && seen < 4000000; i++) if (alias_bit(i)) { const uint64_t *w = (const uint64_t *)(b + (i << 12));
            for (int j = 0; j < 512; j++) { uint64_t v = w[j]; if ((v & 7) || !alias_readable(v, 0x140)) continue; seen++;
                for (int o = 0; o < 32; o++) if (*(const uint64_t *)(v + 0x40 + 8 * o) == v) { char nm[64]; if (census_str(*(const uint64_t *)(v + 0x10), nm, sizeof nm)) hits[o]++; } } }
        int best = 0; for (int o = 1; o < 32; o++) if (hits[o] > hits[best]) best = o;
        g_cen_self = 0x40 + 8 * best;
        LOG("heap census: class self-pointer at +%#x (%u candidate words agree; next best +%#x with %u)", g_cen_self, hits[best], 0x40 + 8 * (best ? 0 : 1), hits[best ? 0 : 1]);
        if (!g_cen) g_cen = calloc(CEN_N, sizeof *g_cen);
    }
    for (size_t k = 0; k < CEN_N; k++) { g_cen[k].prev = g_cen[k].n; g_cen[k].n = 0; }
    uint64_t words = 0, objs = 0;
    for (size_t i = 0; i < np; i++) if (alias_bit(i)) { const uint64_t *w = (const uint64_t *)(b + (i << 12));
        for (int j = 0; j < 512; j++) { uint64_t v = w[j]; words++;
            if ((v & 7) || v < b || v >= b + g_alias_cap || !alias_bit((v - b) >> 12) || !alias_readable(v + g_cen_self, 8) || *(const uint64_t *)(v + g_cen_self) != v) continue;
            size_t h = (size_t)((v >> 3) * 0x9E3779B97F4A7C15ull >> 47) & (CEN_N - 1);
            while (g_cen[h].klass && g_cen[h].klass != v) h = (h + 1) & (CEN_N - 1);
            g_cen[h].klass = v; g_cen[h].n++; objs++; } }
    // the 40 most numerous and the 40 that grew most
    struct { uint64_t k; uint32_t n, prev; } top[40] = {{0}}, grow[40] = {{0}};
    for (size_t k = 0; k < CEN_N; k++) { struct cen_ent *e = &g_cen[k]; if (!e->klass) continue;
        for (int t = 0; t < 40; t++) if (e->n > top[t].n) { memmove(&top[t + 1], &top[t], (39 - t) * sizeof top[0]); top[t].k = e->klass; top[t].n = e->n; top[t].prev = e->prev; break; }
        int64_t d = (int64_t)e->n - e->prev;
        for (int t = 0; t < 40 && round; t++) if (d > (int64_t)grow[t].n - grow[t].prev || !grow[t].k) { memmove(&grow[t + 1], &grow[t], (39 - t) * sizeof grow[0]); grow[t].k = e->klass; grow[t].n = e->n; grow[t].prev = e->prev; break; } }
    LOG("heap census #%d at %.0f s: %llu words read, %llu class references, %.1f s", round, (t0 - g_t0_ns) / 1e9, (unsigned long long)words, (unsigned long long)objs, (now_ns() - t0) / 1e9);
    for (int pass = 0; pass < 2; pass++) for (int t = 0; t < 40; t++) {
        uint64_t k = pass ? grow[t].k : top[t].k; if (!k) break; uint32_t n = pass ? grow[t].n : top[t].n, pv = pass ? grow[t].prev : top[t].prev;
        char nm[96] = "?", ns[96] = ""; census_str(*(const uint64_t *)(k + 0x10), nm, sizeof nm); census_str(*(const uint64_t *)(k + 0x18), ns, sizeof ns);
        char dn[96] = ""; uint64_t decl = *(const uint64_t *)(k + 0x50); if (decl && alias_readable(decl + 0x10, 8)) census_str(*(const uint64_t *)(decl + 0x10), dn, sizeof dn);
        LOG("heap census #%d %s %2d: %8u (%+d) %s%s%s%s%s", round, pass ? "growth" : "top", t + 1, n, (int)n - (int)pv, ns, *ns ? "." : "", *dn ? dn : "", *dn ? "/" : "", nm);
    }
    const char *watch = getenv("DM_HEAP_WATCH");   // class names whose counts are always printed (all classes of that name summed: generic instances share it)
    for (const char *q = watch; q && *q; ) {
        char want[96]; size_t n = strcspn(q, ","); snprintf(want, sizeof want, "%.*s", (int)n, q); q += n; if (*q) q++;
        uint64_t cn = 0, cp = 0; int classes = 0;
        for (size_t k = 0; k < CEN_N; k++) if (g_cen[k].klass) { char nm[96]; census_str(*(const uint64_t *)(g_cen[k].klass + 0x10), nm, sizeof nm); if (!strcmp(nm, want)) { cn += g_cen[k].n; cp += g_cen[k].prev; classes++; } }
        LOG("heap census #%d watch %s: %llu (%+lld) in %d classes", round, want, (unsigned long long)cn, (long long)cn - (long long)cp, classes);
    }
    pthread_mutex_unlock(&g_alias_lock);
}
// Who keeps them (DM_HEAP_REFS=<class name>[,<class name>…], traced at the last census): up to 48 instances of each class are sampled; each level
// scans the memory once for words pointing at the current set, takes the object around each such word (the nearest class pointer at or before
// it, within 64 KB), and logs the referring classes; their objects are the next set. Ten levels or until nothing new refers.
static int census_is_class(uint64_t w) { return !(w & 7) && alias_readable(w, (size_t)g_cen_self + 8) && *(const uint64_t *)(w + g_cen_self) == w; }
static uint64_t census_header(uint64_t r) {   // the object holding the word at r: nearest preceding class pointer
    uint64_t b = (uint64_t)(uintptr_t)g_alias;
    for (uint64_t a = r & ~7ull, lim = a > b + 65536 ? a - 65536 : b; a >= lim; a -= 8) {
        if (((a + 8) & 0xfff) == 0 && !alias_readable(a, 8)) return 0;
        if (census_is_class(*(const uint64_t *)a)) return a;
        if (a == lim) break;
    }
    return 0;
}
static void census_name(uint64_t k, char *out, size_t n) {
    char nm[96] = "?", ns[96] = ""; census_str(*(const uint64_t *)(k + 0x10), nm, sizeof nm); census_str(*(const uint64_t *)(k + 0x18), ns, sizeof ns);
    snprintf(out, n, "%s%s%s", ns, *ns ? "." : "", nm);
}
enum { REF_SET = 1 << 14 };
static int ref_has(const uint64_t *set, uint64_t v) { size_t h = (size_t)((v >> 3) * 0x9E3779B97F4A7C15ull >> 50) & (REF_SET - 1); while (set[h]) { if (set[h] == v) return 1; h = (h + 1) & (REF_SET - 1); } return 0; }
static int ref_put(uint64_t *set, uint64_t v) { size_t h = (size_t)((v >> 3) * 0x9E3779B97F4A7C15ull >> 50) & (REF_SET - 1); while (set[h]) { if (set[h] == v) return 0; h = (h + 1) & (REF_SET - 1); } set[h] = v; return 1; }
// Static field storage (no object header: the census sees references from it as "outside any object"). Il2CppClass keeps instance_size and
// static_fields_size and a static_fields pointer; their offsets are found by checking known classes (System.Object 16 bytes, System.Int32 20 bytes
// boxed), and static_fields_size / static_fields are read relative to instance_size as in il2cpp-class-internals.h (+0x14 and −0x40).
static struct sf_blk { uint64_t lo, hi, klass; } *g_sf; static size_t g_nsf; static int g_isz_off = -1;
static int sf_cmp(const void *a, const void *b) { const struct sf_blk *x = a, *y = b; return x->lo < y->lo ? -1 : x->lo > y->lo; }
static void sf_build(void) {
    uint64_t obj = 0, i32 = 0;
    for (size_t k = 0; k < CEN_N; k++) if (g_cen[k].klass) { char nm[96], ns[96] = ""; census_str(*(const uint64_t *)(g_cen[k].klass + 0x10), nm, sizeof nm); census_str(*(const uint64_t *)(g_cen[k].klass + 0x18), ns, sizeof ns);
        if (!strcmp(ns, "System") && !strcmp(nm, "Object")) obj = g_cen[k].klass; if (!strcmp(ns, "System") && !strcmp(nm, "Int32")) i32 = g_cen[k].klass; }
    if (g_isz_off < 0 && obj && i32 && alias_readable(obj, 0x140) && alias_readable(i32, 0x140))
        for (int o = 0xc0; o < 0x130; o += 4) if (*(const uint32_t *)(obj + o) == 16 && *(const uint32_t *)(i32 + o) == 20) { g_isz_off = o; break; }
    LOG("heap census: instance_size at +%#x (System.Object %s, System.Int32 %s)", g_isz_off, obj ? "found" : "missing", i32 ? "found" : "missing");
    if (g_isz_off < 0) return;
    free(g_sf); g_sf = calloc(CEN_N, sizeof *g_sf); g_nsf = 0;
    for (size_t k = 0; k < CEN_N; k++) { uint64_t c = g_cen[k].klass; if (!c || !alias_readable(c, (size_t)g_isz_off + 0x18)) continue;
        uint64_t sf = *(const uint64_t *)(c + g_isz_off - 0x40); uint32_t sz = *(const uint32_t *)(c + g_isz_off + 0x14);
        if (sf && sz && sz < (1u << 24) && !(sf & 7) && alias_readable(sf, sz)) g_sf[g_nsf++] = (struct sf_blk){sf, sf + sz, c}; }
    qsort(g_sf, g_nsf, sizeof *g_sf, sf_cmp);
    LOG("heap census: %zu classes with static fields", g_nsf);
}
static uint64_t sf_find(uint64_t a, uint64_t *off) {   // the class whose static fields hold address a
    size_t lo = 0, hi = g_nsf; while (lo < hi) { size_t m = (lo + hi) / 2; if (g_sf[m].lo <= a) lo = m + 1; else hi = m; }
    if (lo && a < g_sf[lo - 1].hi) { *off = a - g_sf[lo - 1].lo; return g_sf[lo - 1].klass; } return 0;
}
static void heap_refs(const char *cls) {   // caller holds g_alias_lock
    size_t np = g_alias_cap >> 12; uint64_t b = (uint64_t)(uintptr_t)g_alias;
    uint64_t *cur = calloc(REF_SET, 8), *next = calloc(REF_SET, 8), *seen = calloc(REF_SET, 8); size_t ncur = 0;
    struct { uint64_t k; uint32_t n, off; } *agg = calloc(4096, sizeof *agg);
    // instances: words equal to a class pointer named cls
    uint64_t klass[8]; int nk = 0;
    for (size_t k = 0; k < CEN_N && nk < 8; k++) if (g_cen[k].klass) { char nm[96]; census_str(*(const uint64_t *)(g_cen[k].klass + 0x10), nm, sizeof nm); if (!strcmp(nm, cls)) klass[nk++] = g_cen[k].klass; }
    if (!nk) { LOG("heap refs %s: no such class", cls); goto out; }
    uint64_t found = 0;
    for (size_t i = 0; i < np; i++) if (alias_bit(i)) { const uint64_t *w = (const uint64_t *)(b + (i << 12));
        for (int j = 0; j < 512; j++) for (int q = 0; q < nk; q++) if (w[j] == klass[q]) { found++; if ((found % 37) == 1 && ncur < 48) { ref_put(cur, (uint64_t)&w[j]); ref_put(seen, (uint64_t)&w[j]); ncur++; } } }
    LOG("heap refs %s: %llu instances, tracing %zu", cls, (unsigned long long)found, ncur);
    for (int lvl = 1; lvl <= 10 && ncur; lvl++) {
        memset(next, 0, REF_SET * 8); size_t nnext = 0, nagg = 0; uint64_t refs = 0, orphan = 0, orphan_at = 0;
        memset(agg, 0, 4096 * sizeof *agg);
        for (size_t i = 0; i < np; i++) if (alias_bit(i)) { const uint64_t *w = (const uint64_t *)(b + (i << 12));
            for (int j = 0; j < 512; j++) { uint64_t v = w[j]; if ((v & 7) || v < b || v >= b + g_alias_cap || !ref_has(cur, v)) continue;
                uint64_t r = (uint64_t)&w[j]; refs++;
                { uint64_t so; uint64_t sk = g_sf ? sf_find(r, &so) : 0;
                  if (sk) { char nm[200]; census_name(sk, nm, sizeof nm); static int said; if (said++ < 400) LOG("heap refs %s level %d: static field %s +%#llx", cls, lvl, nm, (unsigned long long)so); continue; } }
                uint64_t h = census_header(r);
                if (!h) { orphan++; orphan_at = r; continue; }
                uint64_t k = *(const uint64_t *)h; uint32_t off = (uint32_t)(r - h);
                size_t a = 0; while (a < nagg && agg[a].k != k) a++;
                if (a == nagg && nagg < 4096) { agg[a].k = k; agg[a].off = off; nagg++; }
                if (a < nagg) agg[a].n++;
                if (nnext < REF_SET / 2 && ref_put(seen, h)) { ref_put(next, h); nnext++; } } }
        LOG("heap refs %s level %d: %llu references from %zu classes, %llu outside any object (e.g. %#llx)", cls, lvl, (unsigned long long)refs, nagg, (unsigned long long)orphan, (unsigned long long)orphan_at);
        for (int t = 0; t < 10; t++) { size_t best = SIZE_MAX; for (size_t a = 0; a < nagg; a++) if (agg[a].n && (best == SIZE_MAX || agg[a].n > agg[best].n)) best = a;
            if (best == SIZE_MAX) break; char nm[200]; census_name(agg[best].k, nm, sizeof nm);
            LOG("heap refs %s level %d: %6u from %s (e.g. field +%#x)", cls, lvl, agg[best].n, nm, agg[best].off); agg[best].n = 0; }
        uint64_t *t = cur; cur = next; next = t; ncur = nnext;
    }
out:
    free(cur); free(next); free(seen); free(agg);
}
// The string fields of a class's objects (DM_HEAP_STRINGS=<class name>, at the last census): for up to 2000 instances, every word in its first 0x80
// bytes that points at a System.String is printed (as ASCII; other characters as '?'), one line per instance
static void heap_strings(const char *cls) {   // caller holds g_alias_lock
    size_t np = g_alias_cap >> 12; uint64_t b = (uint64_t)(uintptr_t)g_alias; uint64_t klass[8], strk = 0; int nk = 0;
    for (size_t k = 0; k < CEN_N; k++) if (g_cen[k].klass) { char nm[96], ns[96] = ""; census_str(*(const uint64_t *)(g_cen[k].klass + 0x10), nm, sizeof nm); census_str(*(const uint64_t *)(g_cen[k].klass + 0x18), ns, sizeof ns);
        if (!strcmp(nm, cls) && nk < 8) klass[nk++] = g_cen[k].klass; if (!strcmp(nm, "String") && !strcmp(ns, "System")) strk = g_cen[k].klass; }
    if (!nk || !strk) { LOG("heap strings %s: class or System.String not found", cls); return; }
    int shown = 0;
    for (size_t i = 0; i < np && shown < 2000; i++) if (alias_bit(i)) { const uint64_t *w = (const uint64_t *)(b + (i << 12));
        for (int j = 0; j < 512 && shown < 2000; j++) { int q = 0; while (q < nk && w[j] != klass[q]) q++; if (q == nk) continue;
            uint64_t o = (uint64_t)&w[j]; if (!alias_readable(o, 0x80)) continue;
            char line[600]; size_t lo = 0;
            for (int f = 2; f < 16 && lo < sizeof line - 160; f++) { uint64_t v = ((const uint64_t *)o)[f];
                if ((v & 7) || !alias_readable(v, 0x18) || *(const uint64_t *)v != strk) continue;
                int32_t len = *(const int32_t *)(v + 0x10); if (len < 0 || len > 120 || !alias_readable(v + 0x14, (size_t)len * 2)) continue;
                lo += (size_t)snprintf(line + lo, sizeof line - lo, " +%#x=\"", f * 8);
                for (int c = 0; c < len && lo < sizeof line - 4; c++) { uint16_t ch = ((const uint16_t *)(v + 0x14))[c]; line[lo++] = ch >= 0x20 && ch < 0x7f ? (char)ch : '?'; }
                line[lo++] = '"'; line[lo] = 0; }
            LOG("heap strings %s %p:%s", cls, (void *)o, lo ? line : " (no string fields)"); shown++; } }
}
// A dictionary held by a class's objects (DM_HEAP_DICT=<class name>:<field offset>, at the last census): IL2CPP's Dictionary`2 keeps its entries
// array at +0x18 and its count at +0x20; an entry of reference key and value is {hash, next, key at +8, value at +16}. Each entry is printed with
// the key's and value's classes and first string field; a value that is itself a collection (HashSet / List / Dictionary) has its elements'
// classes counted.
static void census_obj_desc(uint64_t o, char *out, size_t n) {   // "<class> \"<first string field>\""
    if (!o) { snprintf(out, n, "null"); return; }
    if ((o & 7) || !alias_readable(o, 0x40) || !census_is_class(*(const uint64_t *)o)) { snprintf(out, n, "%#llx", (unsigned long long)o); return; }
    char nm[200]; census_name(*(const uint64_t *)o, nm, sizeof nm); size_t lo = (size_t)snprintf(out, n, "%s", nm);
    for (int f = 2; f < 8; f++) { uint64_t v = ((const uint64_t *)o)[f]; if ((v & 7) || !alias_readable(v, 0x18) || !census_is_class(*(const uint64_t *)v)) continue;
        char vn[200]; census_name(*(const uint64_t *)v, vn, sizeof vn); if (strcmp(vn, "System.String")) continue;
        int32_t len = *(const int32_t *)(v + 0x10); if (len <= 0 || len > 80 || !alias_readable(v + 0x14, (size_t)len * 2)) continue;
        lo += (size_t)snprintf(out + lo, n - lo, " \""); for (int c = 0; c < len && lo + 3 < n; c++) { uint16_t ch = ((const uint16_t *)(v + 0x14))[c]; out[lo++] = ch >= 0x20 && ch < 0x7f ? (char)ch : '?'; }
        if (lo + 2 < n) { out[lo++] = '"'; out[lo] = 0; } break; }
}
static void census_coll_desc(uint64_t c, char *out, size_t n) {   // element classes of a HashSet / List / Dictionary value
    out[0] = 0; if (!c || (c & 7) || !alias_readable(c, 0x30) || !census_is_class(*(const uint64_t *)c)) return;
    char nm[200]; census_name(*(const uint64_t *)c, nm, sizeof nm);
    uint64_t arr = ((const uint64_t *)c)[nm[0] && strstr(nm, "List`1") ? 2 : 3]; int32_t cnt = *(const int32_t *)(c + (strstr(nm, "List`1") ? 0x18 : 0x20));
    int stride = strstr(nm, "HashSet`1") ? 16 : strstr(nm, "List`1") ? 8 : strstr(nm, "Dictionary`2") ? 24 : 0, off = strstr(nm, "HashSet`1") ? 8 : strstr(nm, "Dictionary`2") ? 16 : 0;
    if (!stride || !arr || !alias_readable(arr, 0x20)) return;
    uint64_t len = *(const uint64_t *)(arr + 0x18); if (cnt < 0 || len > 100000 || !alias_readable(arr + 0x20, (size_t)len * stride)) return;
    size_t lo = (size_t)snprintf(out, n, " [%s, %d]:", nm, cnt);
    for (uint64_t i = 0; i < len && i < 64 && lo + 80 < n; i++) { uint64_t e = *(const uint64_t *)(arr + 0x20 + i * stride + off); if (!e) continue; char d[160]; census_obj_desc(e, d, sizeof d); lo += (size_t)snprintf(out + lo, n - lo, " %s;", d); }
}
static void heap_dict(const char *spec) {   // caller holds g_alias_lock
    char cls[96]; unsigned off = 0; if (sscanf(spec, "%95[^:]:%x", cls, &off) != 2) { LOG("heap dict: expected <class>:<hex offset>, got %s", spec); return; }
    size_t np = g_alias_cap >> 12; uint64_t b = (uint64_t)(uintptr_t)g_alias; uint64_t klass[8]; int nk = 0;
    for (size_t k = 0; k < CEN_N && nk < 8; k++) if (g_cen[k].klass) { char nm[96]; census_str(*(const uint64_t *)(g_cen[k].klass + 0x10), nm, sizeof nm); if (!strcmp(nm, cls)) klass[nk++] = g_cen[k].klass; }
    int inst = 0;
    for (size_t i = 0; i < np && inst < 4; i++) if (alias_bit(i)) { const uint64_t *w = (const uint64_t *)(b + (i << 12));
        for (int j = 0; j < 512 && inst < 4; j++) { int q = 0; while (q < nk && w[j] != klass[q]) q++; if (q == nk) continue;
            uint64_t o = (uint64_t)&w[j]; if (!alias_readable(o, off + 8)) continue; uint64_t d = *(const uint64_t *)(o + off);
            if (!d || (d & 7) || !alias_readable(d, 0x28) || !census_is_class(*(const uint64_t *)d)) continue;
            char dn[200]; census_name(*(const uint64_t *)d, dn, sizeof dn); inst++;
            uint64_t ent = *(const uint64_t *)(d + 0x18); int32_t cnt = *(const int32_t *)(d + 0x20);
            LOG("heap dict %s %p +%#x: %s, count %d", cls, (void *)o, off, dn, cnt);
            if (!ent || !alias_readable(ent, 0x20)) continue; uint64_t len = *(const uint64_t *)(ent + 0x18);
            if (len > 200000 || !alias_readable(ent + 0x20, (size_t)len * 24)) continue;
            for (uint64_t e = 0; e < len && e < (uint64_t)cnt + 64; e++) { const uint64_t *en = (const uint64_t *)(ent + 0x20 + e * 24); if (!en[1] && !en[2]) continue;
                char kd[200], vd[200], cd[1200]; census_obj_desc(en[1], kd, sizeof kd); census_obj_desc(en[2], vd, sizeof vd); census_coll_desc(en[2], cd, sizeof cd);
                LOG("heap dict %s [%llu] key %s → %s%s", cls, (unsigned long long)e, kd, vd, cd); } } }
}
static void *census_main(void *arg) {
    const char *e = arg; int round = 0;
    for (const char *p = e; *p; ) {
        double at = atof(p); while (now_ns() - g_t0_ns < (uint64_t)(at * 1e9)) usleep(100000);
        heap_census(round++);
        p = strchr(p, ','); if (!p) break; p++;
    }
    if (g_cen && getenv("DM_HEAP_REFS")) { pthread_mutex_lock(&g_alias_lock); sf_build(); pthread_mutex_unlock(&g_alias_lock); }
    const char *dicts = getenv("DM_HEAP_DICT");
    if (dicts && g_cen) for (const char *q = dicts; *q; ) { char one[128]; size_t n = strcspn(q, ","); snprintf(one, sizeof one, "%.*s", (int)n, q);
        pthread_mutex_lock(&g_alias_lock); heap_dict(one); pthread_mutex_unlock(&g_alias_lock); q += n; if (*q) q++; }
    const char *strs = getenv("DM_HEAP_STRINGS");
    if (strs && g_cen) { pthread_mutex_lock(&g_alias_lock); heap_strings(strs); pthread_mutex_unlock(&g_alias_lock); }
    const char *refs = getenv("DM_HEAP_REFS");   // after the last census
    if (refs && g_cen) for (const char *q = refs; *q; ) {
        char cls[96]; size_t n = strcspn(q, ","); snprintf(cls, sizeof cls, "%.*s", (int)n, q);
        pthread_mutex_lock(&g_alias_lock); heap_refs(cls); pthread_mutex_unlock(&g_alias_lock);
        q += n; if (*q) q++;
    }
    return NULL;
}
static void census_start(void) {
    static int done; if (done) return; done = 1;
    const char *e = getenv("DM_HEAP_CENSUS"); if (!e || !*e) return;
    pthread_t t; if (!pthread_create(&t, NULL, census_main, (void *)e)) pthread_detach(t);
}

// An SDK abort (svc Break) tells more with what came before it: the last OutputDebugString texts, kept silently (a release build may pass the abort
// message there; mush8 hooks the AbortImpl overloads instead, idea only, but their message forms are variadic, and Horizon passes variadic
// arguments in registers where Apple's ABI expects them on the stack, so they are not taken over from the host), and the address
// nn::diag::detail::AbortImpl was called from (the SDK stores it in g_AbortImplReturnAddress before it breaks)
enum { DBGN = 8, DBGLEN = 512 };
static char g_dbg[DBGN][DBGLEN]; static unsigned g_dbg_n; static pthread_mutex_t g_dbg_mu = PTHREAD_MUTEX_INITIALIZER;
static void dbg_keep(const char *s, uint64_t n) {
    if (!s) return;
    size_t k = n < DBGLEN - 1 ? (size_t)n : DBGLEN - 1;
    pthread_mutex_lock(&g_dbg_mu); char *d = g_dbg[g_dbg_n++ % DBGN]; memcpy(d, s, k); d[k] = 0;
    while (k && (d[k - 1] == '\n' || d[k - 1] == '\r')) d[--k] = 0;
    pthread_mutex_unlock(&g_dbg_mu);
}
static void abort_context(void) {
    pthread_mutex_lock(&g_dbg_mu);
    unsigned n = g_dbg_n < DBGN ? g_dbg_n : DBGN;
    if (n) LOG("Break: the last %u debug output line(s) before it:", n);
    for (unsigned i = g_dbg_n - n; i != g_dbg_n; i++) LOG("  [guest] %s", g_dbg[i % DBGN]);
    pthread_mutex_unlock(&g_dbg_mu);
    for (int i = 0; i < g_nmod; i++) {
        const uint64_t *ra = module_sym(&g_mod[i], "_ZN2nn4diag6detail24g_AbortImplReturnAddressE");
        if (!ra || !*ra) continue;
        module_t *m = module_of((void *)(uintptr_t)*ra);
        LOG("Break: nn::diag::detail::AbortImpl was called from %s+%#llx", m ? m->name : "?", m ? (unsigned long long)(*ra - (uint64_t)m->base) : (unsigned long long)*ra);
        break;
    }
}
uint64_t svc_dispatch(int num, uint64_t *regs, uint64_t orig_addr) {
    { void hlecount_svc(int); hlecount_svc(num); }   // DM_HLECOUNT: svc counts by number
    mem_init(); census_start();
    if (num >= 0x80 && num <= 0xff) {
        if (g_hcall[num - 0x80] && g_hcall[num - 0x80](regs)) return regs[0];
        die("host call %#x from %s has no handler (the product's cuts and the game's hooks disagree)", num, module_of((void *)orig_addr) ? module_of((void *)orig_addr)->name : "?");
    }
    if (svc_thread(num, regs)) return regs[0];
    switch (num) {
    case 0x01: {  // SetHeapSize(out_addr*, size)  → x1 = heap base
        // Horizon's rules (switchbrew SVC; plan item 143, totk-mobile's vectors in tests/t_guest_mem.c section 11): a multiple of 2 MiB and at most
        // 4 GiB, else InvalidSize (0xCA01; it took any size); above the heap region OutOfMemory (0xD001; it said 0xD401); a shrink over memory that
        // is mapped elsewhere (a MapMemory source: a thread's stack) InvalidCurrentMemory (0xD401); memory given back and taken again reads zeros
        // (it kept its old contents behind PROT_NONE)
        size_t size = regs[1];
#ifndef DM_TEST_HEAP_OLD   // negative control of t_guest_mem only: as before
        if (size & 0x1fffff || size > 0x100000000ull) { regs[0] = 0xCA01; return 0; }
        if (size > g_heap_cap) { LOG("SetHeapSize: %#zx is above the heap region (%#zx)", size, g_heap_cap); regs[0] = 0xD001; return 0; }
#else
        if (size > g_heap_cap) { LOG("SetHeapSize OOM: req %#zx > cap %#zx", size, g_heap_cap); regs[0] = 0xD401; return 0; }
#endif
        if (size + image_bytes() > app_pool()) { g_heap_rejects++; regs[0] = 0x10801; return 0; }   // exceeds the application memory pool: ResultLimitReached. At startup the game retries in 2 MB decrements until it fits (main+0x1b66184)
#ifndef DM_TEST_HEAP_OLD
        if (size < g_heap_cur) { int busy = 0; pthread_mutex_lock(&g_map_lock);
            for (int i = 0; i < g_nmap && !busy; i++) busy = g_map[i].src && g_map[i].src < (uint64_t)(g_heap + g_heap_cur) && (uint64_t)(g_heap + size) < g_map[i].src + g_map[i].size;
            pthread_mutex_unlock(&g_map_lock);
            if (busy) { LOG("SetHeapSize: %#zx would free heap memory a MapMemory still holds (InvalidCurrentMemory)", size); regs[0] = 0xD401; return 0; } }
#endif
        int rc = 0;
        if (size > g_heap_cur) rc = mprotect(g_heap + g_heap_cur, size - g_heap_cur, PROT_READ | PROT_WRITE);
#ifndef DM_TEST_HEAP_OLD
        else if (size < g_heap_cur) rc = mmap(g_heap + size, g_heap_cur - size, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == MAP_FAILED;   // given back: fresh pages
#else
        else if (size < g_heap_cur) rc = mprotect(g_heap + size, g_heap_cur - size, PROT_NONE);
#endif
        if (g_flag.svc) LOG("SetHeapSize %#zx -> base %p mprotect=%d errno=%d", size, g_heap, rc, errno);
        { static int said; if (!said && size) { said = 1; LOG("SetHeapSize: heap first set to %zu MB (application pool %llu MB − images %zu MB; %d refusals before)", size >> 20, (unsigned long long)(app_pool() >> 20), image_bytes() >> 20, g_heap_rejects); } }
        g_heap_cur = size;
        regs[0] = 0; regs[1] = (uint64_t)g_heap; return 0;
    }
    case 0x02: {  // SetMemoryPermission(x0 address, x1 size, w2 permission), Horizon's checks (Atmosphère's svc): address and size page-aligned, size not 0,
                  // no wrap, permission none / read / read-write. Read-write is what guest memory already is here; none and read are reported once and
                  // not applied (Tears of the Kingdom only asks for read-write: 188 calls in the alias region)
        uint64_t a = regs[0], sz = regs[1]; unsigned perm = (unsigned)regs[2];
        regs[0] = (a & 0xFFF) ? 0xCC01 : (sz & 0xFFF) || !sz ? 0xCA01 : a + sz <= a ? 0xD401 : (perm != 0 && perm != 1 && perm != 3) ? 0xD801 : 0;
        if (!regs[0] && perm != 3) { static int once; if (!once++) LOG("[red] SetMemoryPermission %#llx +%#llx permission %u: only read-write is modelled, left as it is", (unsigned long long)a, (unsigned long long)sz, perm); }
        return 0;
    }
    case 0x03: regs[0] = 0; return 0;   // SetMemoryAttribute
    case 0x04: {  // MapMemory(dst, src, size): when creating a thread the SDK moves the stack on the heap into the stack region
        uint64_t dst = regs[0], src = regs[1], size = regs[2];
        pthread_mutex_lock(&g_mapmem_mu);   // with UnmapMemory: a 16 KB page can hold the ends of two stacks, created and retired on different threads
        if ((uint8_t *)dst < g_stack || (uint8_t *)dst + size > g_stack + g_stack_cap || map_find(dst, size)) {
            pthread_mutex_unlock(&g_mapmem_mu); LOG("MapMemory: target invalid or in use %#llx+%#llx", dst, size); regs[0] = 0xDC01; return 0; }
        // Apple pages are 16K, Horizon pages 4K: the accessible region is extended to a 16K boundary (the extra pages opened are unused pages of the same region, semantics unaffected)
        uint64_t lo = dst & ~0x3FFFull, hi = (dst + size + 0x3FFF) & ~0x3FFFull;
        if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE)) die("MapMemory mprotect errno=%d", errno);
        memcpy((void *)dst, (void *)src, size);   // move semantics: the source region is not accessed while mapped (Horizon makes it inaccessible)
        map_add(dst, size, src, 0x0B /*MappedMemory*/);
        pthread_mutex_unlock(&g_mapmem_mu);
        if (g_flag.svc) LOG("MapMemory %#llx <- %#llx +%#llx", dst, src, size);
        regs[0] = 0; return 0;
    }
    case 0x05: {  // UnmapMemory(dst, src, size): contents are moved back to the source region; the host pages no other mapping shares are released (below)
        uint64_t dst = regs[0], src = regs[1], size = regs[2];
        pthread_mutex_lock(&g_mapmem_mu);
        int k = map_find(dst, size);
        if (!k || g_map[k - 1].base != dst || g_map[k - 1].size != size || g_map[k - 1].src != src) {
            pthread_mutex_unlock(&g_mapmem_mu); LOG("UnmapMemory: does not match a mapping %#llx+%#llx", dst, size); regs[0] = 0xD401; return 0; }
        memcpy((void *)src, (void *)dst, size);
        map_del(k - 1);
        // Give the pages back: on the console the stack region only aliases the heap's memory, here it holds a copy, which used to stay resident
        // after the thread ended (Unity's thread pool comes and goes at every scene load: about 6 MB more per load). A host page no other mapping
        // touches is replaced with a fresh inaccessible one, as an unmapped page is on the console; a page shared with a neighbour stays as it is
        for (uint64_t h = dst & ~(uint64_t)(vm_page_size - 1); h < dst + size; h += vm_page_size)
            if (!map_find(h, vm_page_size) && mmap((void *)h, vm_page_size, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == MAP_FAILED) die("UnmapMemory: replacing page %#llx failed, errno=%d", h, errno);
        pthread_mutex_unlock(&g_mapmem_mu);
        regs[0] = 0; return 0;
    }
    case 0x06: {  // QueryMemory(MemoryInfo* out, PageInfo* pout, addr) → x1 = pageinfo
        uint64_t mi = regs[0], addr = regs[2];
        // MemoryInfo: base(0) size(8) state(0x10) attr(0x14) perm(0x18) ...
        uint8_t *a = (uint8_t *)addr;
        uint64_t base, size, state, perm;
        int k;
        pthread_mutex_lock(&g_map_lock);   // over the whole chain: the registry branches read g_map, the others are cheap
        if (a >= g_heap && a < g_heap + g_heap_cur) { base=(uint64_t)g_heap; size=g_heap_cur; state=5/*Heap*/; perm=3; }
        else if ((k = map_find(addr, 1))) { base=g_map[k-1].base; size=g_map[k-1].size; state=g_map[k-1].state; perm=3; }
        else if (a >= g_as && a < g_as + RESERVE) {   // free: up to the adjacent mapping/heap boundary
            uint64_t lo = (uint64_t)g_as, hi = (uint64_t)g_as + RESERVE;
            if (a >= g_heap) lo = (uint64_t)g_heap + g_heap_cur;
            else if (hi > (uint64_t)g_heap) hi = (uint64_t)g_heap;
            for (int i = 0; i < g_nmap; i++) {
                uint64_t e = g_map[i].base + g_map[i].size;
                if (e <= addr && e > lo) lo = e;
                if (g_map[i].base > addr && g_map[i].base < hi) hi = g_map[i].base;
            }
            base=lo; size=hi-lo; state=0; perm=0; }
        else {
            // The address lies in "the current host thread's stack": report the whole stack (state 0xB = stack). When the SDK initializes the main thread record it queries here with the sp at that moment and takes the reported range as the main thread stack;
            // later nn::os::GetCurrentStackInfo asserts that sp is still inside that range and aborts if not. Previously every address outside the guest address space was reported as "the 4 KiB page containing it",
            // so the main thread stack was recorded as one page. Whether the sp at init and a later sp share a page depends on the absolute position of the host stack top, which moves with the total length of environment variables / launch arguments (a different environment occasionally aborts at startup).
            // This Diablo build does not call GetCurrentStackInfo (main / subsdk0 do not import it and the SDK has no internal call site; the host reaches the main menu with 12 different environment padding lengths), so it was never hit; reporting "one page" is wrong in itself, so fix it anyway.
            uint64_t top = (uint64_t)pthread_get_stackaddr_np(pthread_self()), ssz = pthread_get_stacksize_np(pthread_self());
            if (addr < top && addr >= top - ssz) { base = top - ssz; size = ssz; state = 0xB/*Stack*/; perm = 3; }
            else { base=addr & ~0xFFFull; size=0x1000; state=3/*Code*/; perm=5; } }
        pthread_mutex_unlock(&g_map_lock);
        if (mi) { uint64_t *m=(uint64_t*)mi; m[0]=base; m[1]=size; ((uint32_t*)mi)[4]=(uint32_t)state; ((uint32_t*)mi)[5]=0; ((uint32_t*)mi)[6]=(uint32_t)perm; ((uint32_t*)mi)[7]=0; ((uint32_t*)mi)[8]=0; }
        regs[0] = 0; regs[1] = 0; return 0;
    }
    case 0x2C: case 0x2D: {  // MapPhysicalMemory / UnmapPhysicalMemory(addr, size): only allowed inside the alias region (used by the SDK's VammManager)
        uint8_t *a = (uint8_t *)regs[0]; size_t size = regs[1];
#ifndef DM_TEST_PHYS_OLD   // negative control of t_guest_mem only: as before
        // Horizon's results (switchbrew; plan item 128, totk-mobile's vectors): address not 4 KiB aligned InvalidAddress, size 0 or not aligned
        // InvalidSize, a range that wraps or leaves the alias region InvalidMemoryRegion (it said 0xD401, died on misalignment, and a wrapping range
        // passed the range check)
        if ((uintptr_t)a & 0xfff) { regs[0] = 0xCC01; return 0; }
        if (!size || size & 0xfff) { regs[0] = 0xCA01; return 0; }
        if (size > UINT64_MAX - (uint64_t)(uintptr_t)a || a < g_alias || a + size > g_alias + g_alias_cap) { LOG("SVC %#x outside the alias region %p+%#zx", num, a, size); regs[0] = 0xDC01; return 0; }
#else
        if (a < g_alias || a + size > g_alias + g_alias_cap) { LOG("SVC %#x outside the alias region %p+%#zx", num, a, size); regs[0] = 0xD401; return 0; }
        if (((uintptr_t)a | size) & 0xfff) die("SVC %#x: address / size not 4 KB aligned %p+%#zx (the SDK never does this)", num, a, size);
#endif
        // The physical memory the game may map: the application pool it is told about (GetInfo TotalMemory) less the images and the heap, as on the console
        size_t pool = (size_t)app_pool(), used = image_bytes() + g_heap_cur, limit = pool > used ? (pool - used) / 0x1000 : 0;
        if (alias_phys(a, size, num == 0x2C, limit)) {
            static _Atomic unsigned long long refused; unsigned long long k = atomic_fetch_add(&refused, 1) + 1; size_t mm, hm; dm_alias_stat(&mm, &hm);
            if (k <= 8 || !(k & (k - 1))) LOG("MapPhysicalMemory refused (LimitReached, #%llu): %zu KB more would pass the %zu MB the application pool leaves for it (%zu MB mapped)", k, size >> 10, limit >> 8, mm);
            regs[0] = 0x10801; return 0;
        }
        if (g_flag.svc) LOG("%sPhysicalMemory %p+%#zx", num == 0x2C ? "Map" : "Unmap", a, size);
        regs[0] = 0; return 0;
    }
    case 0x26: {  // Break(reason, ptr, size): SDK fatal abort
        uint32_t res = (regs[1] && regs[2] >= 4) ? *(uint32_t *)regs[1] : 0;
        module_t *ms = module_of((void *)orig_addr), *mm = module_of((void *)regs[30]);   // the svc's own module and its caller's (they were mixed: one's name, the other's offset)
        static int nb;
        LOG("Break#%d reason=%#llx Result=%#x, svc at %s+%#llx, called from %s+%#llx", ++nb, regs[0], res, ms ? ms->name : "?", ms ? orig_addr - (uint64_t)ms->base : orig_addr,
            mm ? mm->name : "?", mm ? regs[30] - (uint64_t)mm->base : regs[30]);
        abort_context();
        dm_backtrace(regs[29]);
        if (regs[0] & 0x80000000ull) return 0;   // notification only, can continue
        _exit(42);
    }
    case 0x1E: regs[0] = 0; regs[1] = guest_ns() * 12 / 625; return 0;   // GetSystemTick
    case 0x27: {  // OutputDebugString(str, len): printed with the debug flag, and the last few kept for a Break (abort_context)
        if (g_flag.dbg) fprintf(stderr, "[guest] %.*s\n", (int)regs[1], (char *)regs[0]);
        dbg_keep((const char *)regs[0], regs[1]);
        regs[0] = 0; return 0;
    }
    case 0x29: {  // GetInfo(out*, id, handle, subid) → x1 = value
        uint64_t id = regs[1], v = 0;
        switch (id) {
        case I_CoreMask: v = 7; break;
        case I_PriorityMask: { int hi = dm_game.npdm_prio_hi, lo = dm_game.npdm_prio_lo; if (!hi && !lo) { hi = 28; lo = 59; }   // the NPDM's ThreadInfo range (both games: 28-59); it was 0
            v = 0; for (int p = hi; p <= lo && p < 64; p++) v |= 1ull << p; break; }
        case I_AliasAddr: v = (uint64_t)g_alias; break;   case I_AliasSize: v = g_alias_cap; break;
        case I_HeapAddr:  v = (uint64_t)g_heap;  break;   case I_HeapSize:  v = g_heap_cap;  break;
        case I_AslrAddr:  v = (uint64_t)g_as;    break;   case I_AslrSize:  v = RESERVE;     break;
        case I_StackAddr: v = (uint64_t)g_stack; break;   case I_StackSize: v = g_stack_cap; break;
        // Total = TotalNonSystem + the system resource, Used = UsedNonSystem + the system resource (Horizon's accounting, totk-mobile's vectors;
        // the two pairs were equal): the non-system totals are what the game's own memory is checked against (SetHeapSize above)
        case I_TotalMem: v = app_pool() + sysres_total(); break;  case I_TotalNonSys: v = app_pool(); break;
        case I_UsedMem:  v = g_heap_cur + image_bytes() + sysres_total(); break;  case I_UsedNonSys: v = g_heap_cur + image_bytes(); break;   // Used = heap + image, the same accounting as the check in SetHeapSize (it used to be hard-coded to 64 MB). The sdk's QueryMemoryInfo (sdk+0x34f4ec) only uses it to compute the "program usage" field;
                                                 // The "total available" field is simply the pool total, so at startup the game still has to step down from the whole pool by 2 MB at a time until pool - image (about 34 refusals) when setting its heap -- this is how the original behaves on the real device, not a mistake in our accounting
        case I_SysResTotal: v = sysres_total(); break; case I_SysResUsed: v = 0x400000; break;
        case I_DebuggerAttached: v = 0; break;
        // Four values fixed for the process, handle 0 and subtypes 0-3 only (switchbrew; it drew a new value every call, with any arguments)
        case I_RandomEntropy: { static pthread_once_t once = PTHREAD_ONCE_INIT;
#ifndef DM_TEST_HEAP_OLD
            if (regs[2]) { regs[0] = 0xE401; return 0; }
            if (regs[3] > 3) { regs[0] = 0xE801; return 0; }
            pthread_once(&once, entropy_init); v = g_entropy[regs[3]]; break; }   // drawn from a deterministic stream when DM_DETCLOCK is on (hle_os.c)
#else
            uint64_t dm_entropy64(void); (void)once; v = dm_entropy64(); break; }
#endif
        case I_ProgramId: v = dm_game.program_id; break;
        case I_IsApplication: v = 1; break;
        // User exception context area (switchbrew SVC GetInfo 20, a process value): SDK 20's SetDyingMessageRegion writes the region at +0x1c0 (TotK
        // sdk 0x1438dc; 0 wrote through a null pointer). One page of host memory, as nxnative (seedds, MIT)
        case I_UserExcAddr: { static _Alignas(0x1000) uint8_t ctx[0x1000]; v = (uint64_t)ctx; break; }
        // Threads the process may still create (Unity checks this before starting each worker and silently skips the thread at 0, then waits
        // forever for it). The host has no real limit besides its handle table; 128 is a generous cap of the host's own choosing
        case I_FreeThreadCount: { void thr_counts(int *, int *, int *); int nh, nl, no; thr_counts(&nh, &nl, &no); v = nl < 128 ? (uint64_t)(128 - nl) : 0; break; }
        case I_AliasExtraSize: v = 0; break;   // FW 18+: part of the alias region the kernel keeps for itself; none here
        default:   // an unknown type: InvalidEnumValue (0xF001), as Horizon's kernel answers (Atmosphère's GetInfo; Touchable 5cab3ac, MIT; plan item 160);
                   // it succeeded with 0. Red once per type with its caller, so a game that asks shows in the log
#ifndef DM_TEST_GETINFO_OLD   // negative control of t_guest_mem only
            { static uint64_t seen[8]; static int nseen; int dup = 0; for (int i = 0; i < nseen; i++) dup |= seen[i] == id;
              if (!dup) { if (nseen < 8) seen[nseen++] = id; module_t *m = module_of((void *)orig_addr);
                  LOG("[red] GetInfo type %llu (subtype %llu) is not one Horizon has: InvalidEnumValue, from %s+%#llx", (unsigned long long)id, (unsigned long long)regs[3], m ? m->name : "?", m ? orig_addr - (uint64_t)m->base : orig_addr); } }
            regs[0] = 0xF001; regs[1] = 0; return 0;
#else
            v = 0; break;
#endif
        }
        regs[0] = 0; regs[1] = v; return 0;
    }
    default:   // red with its caller (the svc instruction's module and offset): no game here reaches it
        if (!g_seen[num & 0xff]) { g_seen[num & 0xff] = 1; module_t *m = module_of((void *)orig_addr);
            LOG("[red] SVC %#x x0=%#llx x1=%#llx x2=%#llx from %s+%#llx: not implemented, returns success", num, regs[0], regs[1], regs[2], m ? m->name : "?", m ? orig_addr - (uint64_t)m->base : orig_addr); }
        regs[0] = 0; return 0;
    }
}
