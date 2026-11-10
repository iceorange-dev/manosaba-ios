// Inline uniform updates without a GPU (gfx/nvn/core/upd.c, plan item 264), the arena faked in host memory. The log: kept sorted by start; the
// newest entry overlapping a range, asked again after an add (the kept answer must change); the entries newer than a point applied over a range
// oldest first; entries in the order they were made; a clear. Updates of a submission: logged in both lists, the ones not in memory counted; the
// submission's end keeps the unwritten. Private copies: the snapshot with the submission's updates, one per (range, newest update) shared by later
// draws, a range updated again copied from its previous copy with the updates after it (not from another submission's), none when no update
// reaches the range or when neither a snapshot nor an unwritten update has it (memory has them); without a snapshot, memory with the pending writes.
// An answer kept after the log changed (-DDM_TEST_UL_SEQ_STALE, t_ubren's broken variant): 3 fail.
// Only what a reader reads of a range is copied (plan item 271): a copy of fewer bytes is not reused for a reader of more, whose copy takes
// those bytes from it and the rest from the snapshot. The whole range copied whatever is read, as before (-DDM_TEST_UBSH_WHOLE): 4 fail; a copy
// of fewer bytes no base at all (-DDM_TEST_UBSH_BASE_FULL, the first version: the title screen's copies applied 31 log entries each for 5): 2.
#include "../nvn/core/upd.h"
#include "../nvn/core/usnap.h"
#include "../nvn/core/pend.h"
#include "core_test.h"
#include "../../host/dm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int nvn_be_pool_span(uint64_t lo, uint64_t len) { (void)lo; (void)len; return 1; }
static uint8_t g_arena[1 << 16] __attribute__((aligned(256))); static size_t g_arena_off;   // 256-aligned blocks, as the backend's arena
void nvn_be_arena(size_t len, uint8_t **cpu, uint64_t *gpu, void **buf) { g_arena_off = (g_arena_off + 255) & ~(size_t)255; if (g_arena_off + len > sizeof g_arena) g_arena_off = 0; *cpu = g_arena + g_arena_off; *gpu = (uint64_t)(uintptr_t)*cpu; *buf = g_arena; g_arena_off += len; }

static uint8_t mem[1024] __attribute__((aligned(256))), snap[1024];
static uint64_t M(uint64_t off) { return (uint64_t)(uintptr_t)mem + off; }
static uint8_t *bytes(uint64_t lo, uint64_t gpu) { (void)lo; return (uint8_t *)(uintptr_t)gpu; }

int main(void) {
    static upd_list l = {.lo = UINT64_MAX};
    uint8_t a[16], b[16], c[16]; memset(a, 0xa1, 16); memset(b, 0xb2, 16); memset(c, 0xc3, 16);
    ul_add(&l, (upd_ent){M(0x40), M(0x50), 1, a}); ul_add(&l, (upd_ent){M(0x10), M(0x20), 2, b}); ul_add(&l, (upd_ent){M(0x48), M(0x58), 3, c});
    check("the log kept sorted by start", 1, l.e[0].lo == M(0x10) && l.e[1].lo == M(0x40) && l.e[2].lo == M(0x48));
    check("the newest entry overlapping [0x40, 0x60): 3", 3, (long long)ul_seq(&l, M(0x40), M(0x60)));
    check("none overlapping [0x20, 0x40)", 0, (long long)ul_seq(&l, M(0x20), M(0x40)));
    ul_add(&l, (upd_ent){M(0x44), M(0x46), 4, a});
    check("asked again after an add: the new entry, 4 [-DDM_TEST_UL_SEQ_STALE]", 4, (long long)ul_seq(&l, M(0x40), M(0x60)));
    uint8_t out[32]; memset(out, 0, 32); ul_apply(&l, M(0x40), out, 32, 0);
    check("applied oldest first: 0x40-0x48 a, 0x48-0x58 c over a, 0x44-0x46 a again over it", 1, out[0] == 0xa1 && out[4] == 0xa1 && out[8] == 0xc3 && out[0x17] == 0xc3 && out[0x18] == 0);
    memset(out, 0, 32); ul_apply(&l, M(0x40), out, 32, 2);
    check("only the entries newer than 2: c and the last a", 1, out[0] == 0 && out[4] == 0xa1 && out[8] == 0xc3);
    { const int *o = ul_by_seq(&l, 0, l.n); check("entries in the order they were made", 1, l.e[o[0]].seq == 1 && l.e[o[1]].seq == 2 && l.e[o[2]].seq == 3 && l.e[o[3]].seq == 4); }
    ul_clear(&l); check("a clear empties the log", 0, l.n + (int)ul_seq(&l, M(0), M(0x100)));

    for (int i = 0; i < 1024; i++) mem[i] = snap[i] = (uint8_t)i;
    g_us.on = 1; us_reserve(1); g_us.n = 1; g_us.ent[0] = (us_ent){M(0x100), M(0x200), snap + 0x100, 0, NULL, 0};   // the submission's snapshot of [0x100, 0x200)
    check("no update yet: nothing to copy", 0, upd_any() + (int)upd_shadow_at(M(0x100), 64, &(void *){0}, 7, 0));
    upd_add(M(0x110), M(0x120), a);
    check("an update logged: unwritten, the submission has updates", 1, g_nupd == 1 && upd_any());
    void *buf; uint64_t g1 = upd_shadow_at(M(0x100), 64, &buf, 7, 0);
    check("a private copy: the snapshot with the update", 1, g1 && bytes(M(0x100), g1)[0x10] == 0xa1 && bytes(M(0x100), g1)[0x20] == 0x20 && (g1 & 255) == (M(0x100) & 255));
    check("... the same range again, no update between: the same copy", 1, upd_shadow_at(M(0x100), 64, &buf, 7, 0) == g1 && g_ren.reuse == 1);
    upd_add(M(0x130), M(0x138), b); uint64_t i0 = g_ren.incr, g2 = upd_shadow_at(M(0x100), 64, &buf, 7, 0);
    check("updated again: a new copy made from the previous one with the update after it", 1, g2 && g2 != g1 && g_ren.incr == i0 + 1 && bytes(M(0x100), g2)[0x10] == 0xa1 && bytes(M(0x100), g2)[0x30] == 0xb2);
    upd_add(M(0x138), M(0x13c), c); i0 = g_ren.incr; uint64_t g3 = upd_shadow_at(M(0x100), 64, &buf, 8, 0);
    check("in another submission the previous copy is not the base: from the snapshot", 1, g3 && g_ren.incr == i0 && bytes(M(0x100), g3)[0x10] == 0xa1 && bytes(M(0x100), g3)[0x38] == 0xc3);
    check("a range no update reaches: none", 0, (long long)upd_shadow_at(M(0x180), 16, &buf, 8, 0));
    {   // 271: only what the reader reads is copied (need bytes from the range's start; 0: all of it)
        uint64_t c0 = g_ren.copied, s0 = g_ren.shadows, h1 = upd_shadow_at(M(0x100), 256, &buf, 8, 32);
        check("32 bytes read of a 256-byte range: 32 copied, the snapshot with the update in them [-DDM_TEST_UBSH_WHOLE]", 1, h1 && g_ren.copied == c0 + 32 && bytes(M(0x100), h1)[0x08] == 0x08 && bytes(M(0x100), h1)[0x10] == 0xa1);
        uint64_t i0b = g_ren.incr, h2 = upd_shadow_at(M(0x100), 256, &buf, 8, 64);
        check("a reader of 64 bytes: not the 32-byte copy, one of its own with the later updates [-DDM_TEST_UBSH_WHOLE]", 1, h2 && h2 != h1 && g_ren.shadows == s0 + 2 && bytes(M(0x100), h2)[0x30] == 0xb2 && bytes(M(0x100), h2)[0x38] == 0xc3);
        check("... its first 32 bytes taken from that copy, the rest from the snapshot [-DDM_TEST_UBSH_BASE_FULL]", 1, g_ren.incr == i0b + 1 && bytes(M(0x100), h2)[0x10] == 0xa1 && bytes(M(0x100), h2)[0x28] == 0x28);
        uint64_t r0 = g_ren.reuse;
        check("a reader of 32 bytes again: the 64-byte copy serves it", 1, upd_shadow_at(M(0x100), 256, &buf, 8, 32) == h2 && g_ren.reuse == r0 + 1);
        upd_add(M(0x104), M(0x108), a); uint64_t i1 = g_ren.incr, h3 = upd_shadow_at(M(0x100), 256, &buf, 8, 48);
        check("updated again, 48 bytes read: from the 64-byte copy with the update after it", 1, h3 && g_ren.incr == i1 + 1 && bytes(M(0x100), h3)[0x04] == 0xa1 && bytes(M(0x100), h3)[0x10] == 0xa1 && bytes(M(0x100), h3)[0x2f] == 0x2f);
        i1 = g_ren.incr; c0 = g_ren.copied; uint64_t h4 = upd_shadow_at(M(0x100), 256, &buf, 8, 0);
        check("all of it asked (0): 48 bytes from the 48-byte copy, the other 208 from the snapshot", 1, h4 && h4 != h3 && g_ren.incr == i1 + 1 && g_ren.copied == c0 + 256 && bytes(M(0x100), h4)[0x04] == 0xa1 && bytes(M(0x100), h4)[0x30] == 0xb2 && bytes(M(0x100), h4)[0x38] == 0xc3 && bytes(M(0x100), h4)[0xff] == 0xff);
        check("more asked than the range holds: the range", 1, upd_shadow_at(M(0x100), 256, &buf, 8, 4096) == h4);
    }
    g_us.n = 0; ul_clear(&g_upd); g_nupd = 0;   // the updates written to memory, no snapshot
    check("no snapshot and the updates written: memory has them, none", 0, (long long)upd_shadow_at(M(0x100), 64, &buf, 8, 0));
    upd_add(M(0x140), M(0x144), a); pend_add(M(0x140), 4, a, 0, 9);   // as the backend logs an update: in the update log and among the pending writes
    uint64_t ns = g_ren.noshot, g4 = upd_shadow_at(M(0x140), 16, &buf, 8, 0);
    check("no snapshot, an update unwritten: memory with the pending writes", 1, g4 && g_ren.noshot == ns + 1 && bytes(M(0x140), g4)[0] == 0xa1 && bytes(M(0x140), g4)[8] == 0x48);
    upd_sub_end(); check("the submission's end: its record cleared, the unwritten update kept", 1, g_uh.n == 0 && g_upd.n == 1 && !upd_any());
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
