// What this GPU stores for a float above float16's range (gfx/nvn/mtl/fp16_probe.h, plan item 310; nxnative PR #1128: an M1 Pro stores 65504,
// an A19 Pro +inf): the probe runs, 65504 comes back as itself from the RGBA16Float target and from half(), and 1e5 / -1e5 come back as 65504 or
// +inf and its negative; which one this GPU gives is printed. The backend logs the same with DM_FP16_PROBE=1 (the iPhone's run of it is in
// DEVELOPMENT.md). An RGBA16Unorm target (-DDM_TEST_FP16_PROBE_UNORM): 2 fail.
#import <Foundation/Foundation.h>
#include <stdio.h>
#include "../nvn/mtl/fp16_probe.h"
static int fail, total;
static void check(const char *name, int ok) { total++; fail += !ok; printf("%-100s %s\n", name, ok ? "✓" : "✗"); }
int main(void) { @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); uint16_t h[8] = {0};
    int rc = fp16_probe(dev, h);
    check("the probe ran", rc == 0);
    printf("%s: 1e5, -1e5, 65504, 65520 -> target %s %s %s %s (%04x %04x %04x %04x), half() %s %s %s %s\n", dev.name.UTF8String,
           fp16_name(h[0]), fp16_name(h[1]), fp16_name(h[2]), fp16_name(h[3]), h[0], h[1], h[2], h[3], fp16_name(h[4]), fp16_name(h[5]), fp16_name(h[6]), fp16_name(h[7]));
    check("65504 comes back as 65504 from the target and from half() [-DDM_TEST_FP16_PROBE_UNORM]", h[2] == 0x7bff && h[6] == 0x7bff);
    check("1e5 and -1e5 from the target: 65504 or +inf and its negative [-DDM_TEST_FP16_PROBE_UNORM]", (h[0] == 0x7bff || h[0] == 0x7c00) && h[1] == (h[0] | 0x8000));
    check("1e5 and -1e5 from half(): 65504 or +inf and its negative", (h[4] == 0x7bff || h[4] == 0x7c00) && h[5] == (h[4] | 0x8000));
    printf("FP16_PROBE %s %d/%d\n", fail ? "FAIL" : "OK", total - fail, total);
    return fail != 0;
} }
