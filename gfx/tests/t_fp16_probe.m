// What this GPU stores for a float above float16's range (gfx/nvn/mtl/fp16_probe.h, plan item 310; nxnative PR #1128: an M1 Pro stores 65504,
// an A19 Pro +inf): the probe runs, 65504 comes back as itself from the RGBA16Float target and from half(), and 1e5 / -1e5 come back as 65504 or
// +inf and its negative; which one this GPU gives is printed. The backend logs the same with DM_FP16_PROBE=1 (the iPhone's run of it is in
// DEVELOPMENT.md). Plan item 315d: 1.0007 and -1.0007 come back from the target rounded to nearest (0x3c01 / 0xbc01) or toward zero (0x3c00 /
// 0xbc00), the same way for both signs, and 1 + 2^-11 as 0x3c00 either way; which one is printed (and half()'s). f16_edge_count over a row of
// halves: 2 at ±65504, 2 at ±inf, 2 NaN, the rest not counted. An RGBA16Unorm target (-DDM_TEST_FP16_PROBE_UNORM): 4 fail; the sign kept when
// counting (-DDM_TEST_F16_COUNT_POSITIVE): 1 fails.
#import <Foundation/Foundation.h>
#include <stdio.h>
#include "../nvn/mtl/fp16_probe.h"
static int fail, total;
static void check(const char *name, int ok) { total++; fail += !ok; printf("%-100s %s\n", name, ok ? "✓" : "✗"); }
int main(void) { @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); uint16_t h[16] = {0};
    int rc = fp16_probe(dev, h);
    check("the probe ran", rc == 0);
    printf("%s: 1e5, -1e5, 65504, 65520 -> target %s %s %s %s (%04x %04x %04x %04x), half() %s %s %s %s\n", dev.name.UTF8String,
           fp16_name(h[0]), fp16_name(h[1]), fp16_name(h[2]), fp16_name(h[3]), h[0], h[1], h[2], h[3], fp16_name(h[4]), fp16_name(h[5]), fp16_name(h[6]), fp16_name(h[7]));
    check("65504 comes back as 65504 from the target and from half() [-DDM_TEST_FP16_PROBE_UNORM]", h[2] == 0x7bff && h[6] == 0x7bff);
    check("1e5 and -1e5 from the target: 65504 or +inf and its negative [-DDM_TEST_FP16_PROBE_UNORM]", (h[0] == 0x7bff || h[0] == 0x7c00) && h[1] == (h[0] | 0x8000));
    check("1e5 and -1e5 from half(): 65504 or +inf and its negative", (h[4] == 0x7bff || h[4] == 0x7c00) && h[5] == (h[4] | 0x8000));
    printf("%s: 1.0007, -1.0007, 1 + 2^-11 -> target %04x %04x %04x (rounded %s), half() %04x %04x %04x (rounded %s)\n", dev.name.UTF8String,
           h[8], h[9], h[10], fp16_rounding(h[8]), h[12], h[13], h[14], fp16_rounding(h[12]));
    check("1.0007 from the target rounded to nearest or toward zero, -1.0007 the same way [-DDM_TEST_FP16_PROBE_UNORM]", (h[8] == 0x3c01 || h[8] == 0x3c00) && h[9] == (h[8] | 0x8000));
    check("1 + 2^-11 from the target: 0x3c00 (even, and toward zero) [-DDM_TEST_FP16_PROBE_UNORM]", h[10] == 0x3c00);
    const uint16_t row[10] = {0x7bff, 0xfbff, 0x7c00, 0xfc00, 0x7e00, 0xfe01, 0x3c00, 0x7bfe, 0x0000, 0x8000}; uint64_t c[3] = {0};
    f16_edge_count(row, 10, c);
    check("f16_edge_count: 2 at ±65504, 2 at ±inf, 2 NaN [-DDM_TEST_F16_COUNT_POSITIVE]", c[0] == 2 && c[1] == 2 && c[2] == 2);
    printf("FP16_PROBE %s %d/%d\n", fail ? "FAIL" : "OK", total - fail, total);
    return fail != 0;
} }
