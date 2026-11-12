// Manosaba: log every large Metal allocation (>= 16 MB) so GPU-memory spikes can be attributed.
// Swizzles the system MTLDevice's texture/buffer/heap constructors; output goes to stdout.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <stdatomic.h>
#import "ManosabaSettings.h"
#import <objc/runtime.h>
#include <dlfcn.h>
#include <execinfo.h>

static id<MTLDevice> sDevice;
static const NSUInteger kThreshold = 16u << 20;

// Call stack as image+offset frames (symbolize offline: atos for UnityFramework, the IL2CPP
// method map for ManosabaGame).
static void ManosabaLogStack(const char* why)
{
    void* frames[28];
    int n = backtrace(frames, 28);
    printf("[mtl] stack %s:", why);
    for (int i = 2; i < n; i++)
    {
        Dl_info info;
        if (!dladdr(frames[i], &info) || !info.dli_fname) continue;
        const char* image = strrchr(info.dli_fname, '/');
        printf(" %s+0x%lx", image ? image + 1 : info.dli_fname, (unsigned long)((uintptr_t)frames[i] - (uintptr_t)info.dli_fbase));
    }
    printf("\n");
}

static void ManosabaLogTexture(MTLTextureDescriptor* d, const char* via)
{
    // Dynamic font atlases (A8) are re-created on every glyph upload: count them at any size.
    if (d.pixelFormat == MTLPixelFormatA8Unorm && d.width >= 1024 && d.height >= 1024)
    {
        static int uploads = 0;
        static double mb = 0;
        mb += d.width * d.height / 1048576.0;
        if (++uploads % 50 == 0)
        {
            printf("[mtl] A8 atlas uploads=%d total=%.0f MB (last %lux%lu)\n", uploads, mb, (unsigned long)d.width, (unsigned long)d.height);
            fflush(stdout);
        }
    }
    // The 4096x4096 A8 textures are created by the hundred in bursts; record who asks for them.
    if (d.pixelFormat == MTLPixelFormatA8Unorm && d.width == 4096 && d.height == 4096)
    {
        static int count = 0;
        if (++count <= 4 || count % 100 == 0)
        {
            char why[48];
            snprintf(why, sizeof why, "A8-4096 #%d", count);
            ManosabaLogStack(why);
        }
    }
    MTLSizeAndAlign sa = [sDevice heapTextureSizeAndAlignWithDescriptor:d];
    if (sa.size < kThreshold) return;
    printf("[mtl] %s texture %lux%lu fmt=%lu samples=%lu mips=%lu storage=%lu usage=0x%lx size=%.1f MB\n", via,
           (unsigned long)d.width, (unsigned long)d.height, (unsigned long)d.pixelFormat, (unsigned long)d.sampleCount,
           (unsigned long)d.mipmapLevelCount, (unsigned long)d.storageMode, (unsigned long)d.usage, sa.size / 1048576.0);
    fflush(stdout);
}

static void ManosabaLogBuffer(NSUInteger length, MTLResourceOptions options, const char* via)
{
    if (length < kThreshold) return;
    printf("[mtl] %s buffer size=%.1f MB options=0x%lx\n", via, length / 1048576.0, (unsigned long)options);
    fflush(stdout);
}

static void ManosabaSwizzle(Class cls, SEL sel, id block, IMP* original)
{
    Method m = class_getInstanceMethod(cls, sel);
    if (!m) return;
    *original = method_setImplementation(m, imp_implementationWithBlock(block));
}

__attribute__((constructor)) static void ManosabaStartMetalAllocLogger(void)
{
    if (!ManosabaSettingBool(@"manosaba_diagnostics")) return;  // Settings app: "调试日志"
    sDevice = MTLCreateSystemDefaultDevice();
    if (!sDevice) return;
    Class cls = object_getClass(sDevice);

    static IMP texImp, bufLenImp, bufBytesImp, heapImp;
    ManosabaSwizzle(cls, @selector(newTextureWithDescriptor:), ^id(id self_, MTLTextureDescriptor* d) {
        ManosabaLogTexture(d, "device");
        return ((id (*)(id, SEL, MTLTextureDescriptor*))texImp)(self_, @selector(newTextureWithDescriptor:), d);
    }, &texImp);
    ManosabaSwizzle(cls, @selector(newBufferWithLength:options:), ^id(id self_, NSUInteger len, MTLResourceOptions o) {
        ManosabaLogBuffer(len, o, "device");
        return ((id (*)(id, SEL, NSUInteger, MTLResourceOptions))bufLenImp)(self_, @selector(newBufferWithLength:options:), len, o);
    }, &bufLenImp);
    ManosabaSwizzle(cls, @selector(newBufferWithBytes:length:options:), ^id(id self_, const void* p, NSUInteger len, MTLResourceOptions o) {
        ManosabaLogBuffer(len, o, "device+bytes");
        return ((id (*)(id, SEL, const void*, NSUInteger, MTLResourceOptions))bufBytesImp)(self_, @selector(newBufferWithBytes:length:options:), p, len, o);
    }, &bufBytesImp);
    ManosabaSwizzle(cls, @selector(newHeapWithDescriptor:), ^id(id self_, MTLHeapDescriptor* d) {
        if (d.size >= kThreshold) { printf("[mtl] heap size=%.1f MB storage=%lu\n", d.size / 1048576.0, (unsigned long)d.storageMode); fflush(stdout); }
        return ((id (*)(id, SEL, MTLHeapDescriptor*))heapImp)(self_, @selector(newHeapWithDescriptor:), d);
    }, &heapImp);
    // Frame rate: count drawables Unity asks for, report every 10 s.
    static IMP drawableImp;
    static atomic_int frames;
    ManosabaSwizzle([CAMetalLayer class], @selector(nextDrawable), ^id(id self_) {
        atomic_fetch_add(&frames, 1);
        return ((id (*)(id, SEL))drawableImp)(self_, @selector(nextDrawable));
    }, &drawableImp);
    static dispatch_source_t fpsTimer;
    fpsTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
    dispatch_source_set_timer(fpsTimer, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC), 10 * NSEC_PER_SEC, NSEC_PER_SEC);
    dispatch_source_set_event_handler(fpsTimer, ^{
        int n = atomic_exchange(&frames, 0);
        if (n) { printf("[fps] %.1f\n", n / 10.0); fflush(stdout); }
    });
    dispatch_resume(fpsTimer);
    printf("[mtl] allocation logger installed on %s\n", class_getName(cls));
    fflush(stdout);
}
