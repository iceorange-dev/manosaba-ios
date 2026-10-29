// Where the guest address space goes (host/guest_as.h, plan item 62a). The choice over made-up maps: the lowest gap from 4 GiB that holds the
// most (6 GiB here), a gap below 4 GiB not used, the start 2 MiB aligned; a larger size in a higher gap before a smaller one in a lower gap; the
// size stepping down by 256 MiB (4,864 MiB in a 5,020 MiB gap); nothing below the least; a gap reaching past 2^39 used only below it, one wholly
// past it not at all. The gap log: gaps of 256 MiB or more, one past 2^39 marked. This process's own map: its gaps sorted and apart, a 6 GiB
// reservation (4 GiB at least) placed where nothing is mapped, ending below 2^39, and a second one on top of it refused (fixed, no overwrite).
// The end not held to 2^39 (-DDM_TEST_AS_NO_LIMIT): 2 fail.
#include "../guest_as.h"
#include <mach/mach.h>   // (the reservation released as plat_apple.c made it)
#include <string.h>
#include <unistd.h>

static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
#define G (1ull << 30)
#define M (1ull << 20)

int main(void) {
    uint64_t at, size;
    as_gap a[] = {{4 * G, 12 * G}};
    check("one 8 GiB gap at 4 GiB: 6 GiB at its start", as_pick(a, 1, 6 * G, 4 * G, 256 * M, 2 * M, &at, &size) && at == 4 * G && size == 6 * G);
    as_gap b[] = {{512 * M, 3840 * M}, {8 * G + 4096, 16 * G}};
    check("a gap below 4 GiB not used; the next one's start aligned to 2 MiB", as_pick(b, 2, 6 * G, 4 * G, 256 * M, 2 * M, &at, &size) && at == 8 * G + 2 * M && size == 6 * G);
    as_gap c[] = {{4 * G, 7 * G}, {10 * G, 20 * G}};
    check("6 GiB in the higher gap before less in the lower one", as_pick(c, 2, 6 * G, 2 * G, 256 * M, 2 * M, &at, &size) && at == 10 * G && size == 6 * G);
    as_gap d[] = {{4 * G, 9 * G - 100 * M}};
    check("a 5,020 MiB gap: 6 GiB down by 256 MiB to 4,864 MiB", as_pick(d, 1, 6 * G, 4 * G, 256 * M, 2 * M, &at, &size) && at == 4 * G && size == 4864 * M);
    as_gap e[] = {{4 * G, 7 * G}};
    check("a 3 GiB gap when 4,224 MiB is the least: nothing", !as_pick(e, 1, 6 * G, 4224 * M, 256 * M, 2 * M, &at, &size));
    as_gap f[] = {{AS_LIMIT - 2 * G, AS_LIMIT + 100 * G}};
    check("a gap reaching past 2^39: only its 2 GiB below counts, so 4 GiB does not fit", !as_pick(f, 1, 6 * G, 4 * G, 256 * M, 2 * M, &at, &size));
    as_gap g2[] = {{AS_LIMIT + G, AS_LIMIT + 20 * G}};
    check("a gap wholly past 2^39: not used", !as_pick(g2, 1, 6 * G, 4 * G, 256 * M, 2 * M, &at, &size) || at + size <= AS_LIMIT);

    char buf[1024] = ""; FILE *mf = fmemopen(buf, sizeof buf, "w");
    as_gap h[] = {{4 * G, 4 * G + 100 * M}, {8 * G, 9 * G}, {AS_LIMIT - 512 * M, AS_LIMIT + 512 * M}};
    as_log_gaps(mf, h, 3); fclose(mf);
    check("the gap log: the 1 GiB gap and the one past 2^39 (marked), not the 100 MiB one",
          !strstr(buf, "0x100000000..") && strstr(buf, "free 0x200000000..0x240000000 (1024 MB)\n") && strstr(buf, "(1024 MB), past 2^39\n"));

    static as_gap live[4096]; int ng = as_gaps(live, 4096, AS_FROM, AS_LIMIT), sorted = ng > 0;
    for (int i = 0; i < ng; i++) sorted &= live[i].lo >= AS_FROM && live[i].hi > live[i].lo && (!i || live[i].lo >= live[i - 1].hi);
    check("this process's free gaps from 4 GiB: sorted, apart", sorted);
    void *p = as_pick(live, ng, 6 * G, 4 * G, 256 * M, 2 * M, &at, &size) ? as_reserve_at(at, size) : NULL;
    check("a 6 GiB reservation (4 GiB at least) where nothing is mapped, ending below 2^39", p && (uint64_t)(uintptr_t)p == at && at + size <= AS_LIMIT && !(at & (2 * M - 1)));
    check("a second one on top of it: refused (fixed, no overwrite)", p && !as_reserve_at(at, size));
    if (p) vm_deallocate(mach_task_self(), (vm_address_t)at, (vm_size_t)size);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
