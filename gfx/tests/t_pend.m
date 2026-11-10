// The pending-write table across submissions (nvn_mtl.m pend_note / pend_begin / pend_read_x): writes the GPU has not executed yet stay until their
// submission completes, an inline update reads as its bytes, a GPU copy reads as what its source holds as of that copy (writes recorded before it),
// nested copies too; a copy without a known source, and an overflow, read as unknown. CPU only: the submission fences are set by hand.
// The vertices and indices the coverage proof and the tile read's proof take from memory (mtl/lookahead.inc attr_read, plan item 219): not read
// while a pending write reaches them (reason 12). -DDM_TEST_ATTR_READ_NO_PEND (read whatever is pending, as before) fails 2.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-78s expected %-10llx → got %-10llx %s\n", name, (unsigned long long)w, (unsigned long long)g, w==g?"✓":"✗"); fail += w != g; } while (0)
static uint32_t rd(uint64_t a) { uint32_t v = 0xdeadbeef; return pend_read(a, &v, 4) ? v : 0xdeadbeef; }
int main(void) {
    static uint32_t mem[64]; for (int i = 0; i < 64; i++) mem[i] = 0x1000 + i;
    uint64_t A = (uint64_t)(uintptr_t)&mem[0], B = (uint64_t)(uintptr_t)&mem[16], C = (uint64_t)(uintptr_t)&mem[32];
    static const uint32_t x = 0xaaaa0001, y = 0xbbbb0002;
    g_fence = 10;                                                    // submission 11 is being recorded
    pend_note(A, 4, (const uint8_t *)&x, 0);                         // inline update A = x
    pend_note(B, 4, NULL, A);                                        // GPU copy A → B
    EXPECT("an inline update reads as its bytes", x, rd(A));
    EXPECT("a GPU copy reads as its source as of the copy (the update before it)", x, rd(B));
    g_fence = 11;                                                    // submission 11 committed, 12 recorded
    pend_note(A, 4, (const uint8_t *)&y, 0);                         // A = y after the copy: B keeps x
    pend_note(C, 4, NULL, B);                                        // copy B → C
    EXPECT("the copy into B saw A before the later update", x, rd(B));
    EXPECT("a copy of a copy follows both", x, rd(C));
    EXPECT("A reads as the later update", y, rd(A));
    pend_begin(10);                                                  // 11 and 12 still on the GPU: everything stays
    EXPECT("submission 11 not complete: its writes stay (pending entries)", 4, g_pend.n);
    EXPECT("... and still read", x, rd(B));
    pend_begin(11);                                                  // 11 complete: its writes are in memory (simulated below), 12's stay
    mem[0] = x; mem[16] = x;
    EXPECT("submission 11 complete: only 12's two entries stay", 2, g_pend.n);
    EXPECT("A reads as 12's update", y, rd(A));
    EXPECT("C reads B from memory now", x, rd(C));
    pend_begin(12);
    EXPECT("everything complete: no entries", 0, g_pend.n);
    pend_note(B, 4, NULL, 0);                                        // a copy whose source is not known
    EXPECT("a copy without a source reads as unknown", 0xdeadbeef, rd(B));
    EXPECT("an address no write reaches reads memory", 0x1001, rd(A + 4));
    pend_begin(g_fence + 1);
    {   static float vtx[3][4] = {{-1, -1, 0, 1}, {3, -1, 0, 1}, {-1, 3, 0, 1}}; static uint16_t idx[4] = {0, 1, 2, 0};
        uint64_t V = (uint64_t)(uintptr_t)vtx, IX = (uint64_t)(uintptr_t)idx; static float PP[64][4];
        S.nattr = 1; S.attr[0] = (va_st){.fmt = 0x2E, .offset = 0, .stream = 0}; S.stream[0] = (vs_st){.stride = 16}; S.vbuf[0] = (NVNbufferRange){.address = V, .size = sizeof vtx};
        p_draw dw = {.count = 3}; p_draw_idx di = {.count = 3, .itype = 1, .ibuf = IX};
        EXPECT("attr_read: vertices in no pool, nothing pending: stops at the address (10)", 10, attr_read(0, &dw, NULL, PP));
        pend_note(V, 16, NULL, 0);
        EXPECT("... a pending GPU write reaches a vertex: not read (12) [-DDM_TEST_ATTR_READ_NO_PEND]", 12, attr_read(0, &dw, NULL, PP));
        pend_begin(g_fence + 1); pend_note(IX, 6, NULL, 0);
        EXPECT("... a pending GPU write reaches the indices: not read (12) [-DDM_TEST_ATTR_READ_NO_PEND]", 12, attr_read(0, NULL, &di, PP));
        pend_begin(g_fence + 1);
        EXPECT("... once its submission has completed: the address again (10)", 10, attr_read(0, NULL, &di, PP));
    }
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
}
