// Whether this GPU applies a sampler's LOD bias and min / max reduction (gfx/nvn/mtl/samp_probe.h, plan item 318): the probe runs, its control
// (no bias) reads mip 0 and level(1) reads mip 1, so the texture and the sampling are right; on an Apple10 GPU (Apple's tables give both from
// there; this M5) the bias and both reductions are applied; on another GPU what it does is printed only. The probe's sampler without its bias
// (-DDM_TEST_SAMP_PROBE_NO_BIAS): 1 fails on an Apple10 GPU.
#import <Foundation/Foundation.h>
#include <stdio.h>
#include "../nvn/mtl/samp_probe.h"
static int fail, total;
static void check(const char *name, int ok) { total++; fail += !ok; printf("%-100s %s\n", name, ok ? "✓" : "✗"); }
int main(void) { @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); int r[5] = {0}, rc = samp_probe(dev, r), a10 = 0;
    if (@available(macOS 26.0, *)) a10 = [dev supportsFamily:MTLGPUFamilyApple10];
    printf("%s (Apple10 %s): LOD bias %s, min reduction %s, max reduction %s\n", dev.name.UTF8String, a10 ? "yes" : "no", r[0] ? "applied" : "ignored", r[1] ? "applied" : "ignored", r[2] ? "applied" : "ignored");
    check("the probe ran", rc == 0);
    check("... its control (no bias) read mip 0, level(1) mip 1", !r[3] && r[4]);
    if (a10) { check("an Apple10 GPU applies the LOD bias [-DDM_TEST_SAMP_PROBE_NO_BIAS]", r[0]); check("... and the min and max reductions", r[1] && r[2]); }
    printf("SAMP_PROBE %s %d/%d\n", fail ? "FAIL" : "OK", total - fail, total);
    return fail != 0;
} }
