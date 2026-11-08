// mprotect failures in the dirty-page tracking (gfx/nvn/nvn_dirty.c mp, plan item 314a; S2Arm 4c0bec5, Apache-2.0, written anew), each failure
// injected for one call (g_test_mp_fail): 1. a game write's fault whose unprotect fails is not claimed and the page stays protected (it was marked
// writable and the fault claimed, so the store faulted for ever); 2. a host write's unprotect that fails leaves the pages protected; 3. a protection
// that fails leaves the page unprotected, and a write to it then counts as written since the stamp (it was marked protected, so the write was
// missed); 4. protected again, those unknown writes count once for the new stamp, then the page is clean; the failures are counted. The result
// dropped, as before (-DDM_TEST_DIRTY_MP_IGNORED): 7 fail (the page taken as writable stays read-only: its store faults for ever).
#include "../nvn/nvn_dirty.c"
#include <stdio.h>
#include <mach/mach.h>

module_t g_mod[MAX_MOD];
int g_nmod;
module_t *module_of(const void *p) { (void)p; return NULL; }
void crash_install(void);

static int g_fail;
#define EXPECT(name, want, got) do { \
    long long want_ = (long long)(want), got_ = (long long)(got); \
    printf("%-96s expected %-4lld → got %-4lld %s\n", name, want_, got_, want_ == got_ ? "✓" : "✗"); \
    if (want_ != got_) g_fail++; \
} while (0)
static int writable(const void *p) {   // the page's protection as the kernel has it
    vm_address_t a = (vm_address_t)p; vm_size_t sz = 0; vm_region_basic_info_data_64_t ri; mach_msg_type_number_t c = VM_REGION_BASIC_INFO_COUNT_64; mach_port_t o = MACH_PORT_NULL;
    if (vm_region_64(mach_task_self(), &a, &sz, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&ri, &c, &o) != KERN_SUCCESS || a > (vm_address_t)p) return -1;
    return (ri.protection & VM_PROT_WRITE) != 0;
}
static int state_of(const uint8_t *p) { range_t *r = find_span((uintptr_t)p, 1); return r ? atomic_load(&r->state[((uintptr_t)p - r->base) >> g_shift]) : -1; }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); crash_install();
    size_t pg = (size_t)getpagesize();
    uint8_t *m = mmap(NULL, 4 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (m == MAP_FAILED) { perror("mmap"); return 2; }
    nvn_dirty_add_range(m, 4 * pg);

    nvn_dirty_protect(m + pg, pg); g_test_mp_fail = 1;
    int claimed = on_fault(m + pg + 8, 1, NULL);
    EXPECT("1. a game write's fault whose unprotect fails: not claimed [-DDM_TEST_DIRTY_MP_IGNORED]", 0, claimed);
    EXPECT("... the page still protected, as the kernel has it [-DDM_TEST_DIRTY_MP_IGNORED]", 1, state_of(m + pg) == PG_PROT && writable(m + pg) == 0);
    EXPECT("... the next fault (mprotect working) claimed and the page writable: no fault for ever [-DDM_TEST_DIRTY_MP_IGNORED]", 1, on_fault(m + pg + 8, 1, NULL) && writable(m + pg) == 1);

    nvn_dirty_protect(m + 2 * pg, pg); g_test_mp_fail = 1; dm_guest_write_prepare(m + 2 * pg, 16);
    EXPECT("2. a host write whose unprotect fails: the page stays protected [-DDM_TEST_DIRTY_MP_IGNORED]", 1, state_of(m + 2 * pg) == PG_PROT && writable(m + 2 * pg) == 0);
    dm_guest_write_prepare(m + 2 * pg, 16);

    g_test_mp_fail = 1; uint64_t s3 = nvn_dirty_protect_stamp(m + 3 * pg, pg);
    EXPECT("3. a protection whose mprotect fails: the page not taken as protected [-DDM_TEST_DIRTY_MP_IGNORED]", 1, state_of(m + 3 * pg) == PG_NONE && writable(m + 3 * pg) == 1);
    ((volatile uint8_t *)m)[3 * pg + 4] = 9;   // no fault: the page is writable
    EXPECT("... a write to it then counts as written since the stamp [-DDM_TEST_DIRTY_MP_IGNORED]", 1, nvn_dirty_since(m + 3 * pg, 64, s3));

    uint64_t s4 = nvn_dirty_protect_stamp(m + 3 * pg, pg);
    EXPECT("4. protected again: its unknown writes count once for the new stamp [-DDM_TEST_DIRTY_MP_IGNORED]", 1, nvn_dirty_since(m + 3 * pg, 64, s4) && writable(m + 3 * pg) == 0);
    uint64_t s5 = nvn_dirty_protect_stamp(m + 3 * pg, pg);
    EXPECT("... then clean", 0, nvn_dirty_since(m + 3 * pg, 64, s5));
    int e = 0; uint64_t nf = nvn_dirty_mp_failures(&e);
    EXPECT("the three failures counted, with their errno (ENOMEM)", 1, nf == 3 && e == ENOMEM);
    printf("t_dirty_mpfail: %s\n", g_fail ? "failed" : "all passed"); return g_fail != 0;
}
