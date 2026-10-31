// Manosaba: memory telemetry for the transplanted game (stdout -> device console).
// Samples every 0.5 s and prints when the footprint moves by >= 64 MB, on memory warnings,
// and every 10 s: total phys_footprint, its graphics (GPU) share, the kernel-tracked peak,
// and the headroom jetsam leaves (os_proc_available_memory), plus the device thermal state
// (also logged on every change: the A12Z throttles the GPU hard at serious/critical).
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include <mach/mach.h>
#include <os/proc.h>
#import "ManosabaSettings.h"

static const char* ManosabaThermal(void)
{
    switch (NSProcessInfo.processInfo.thermalState)
    {
        case NSProcessInfoThermalStateNominal: return "nominal";
        case NSProcessInfoThermalStateFair: return "fair";
        case NSProcessInfoThermalStateSerious: return "serious";
        case NSProcessInfoThermalStateCritical: return "critical";
    }
    return "?";
}

static void ManosabaLogMemory(const char* tag, bool force)
{
    static int64_t lastPrinted = -1;
    static int ticks = 0;
    task_vm_info_data_t info = {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS)
        return;
    int64_t fp = (int64_t)(info.phys_footprint >> 20);
    bool moved = lastPrinted < 0 || llabs(fp - lastPrinted) >= 64;
    if (!force && !moved && (++ticks % 20) != 0)
        return;
    lastPrinted = fp;
    int64_t gfx = info.ledger_tag_graphics_footprint >> 20;
    int64_t media = info.ledger_tag_media_footprint >> 20;
    int64_t peak = info.ledger_phys_footprint_peak >> 20;
    int64_t avail = (int64_t)(os_proc_available_memory() >> 20);
    printf("[mem] %s footprint=%lld MB graphics=%lld MB other=%lld MB media=%lld MB peak=%lld MB available=%lld MB thermal=%s\n",
           tag, fp, gfx, fp - gfx, media, peak, avail, ManosabaThermal());
    fflush(stdout);
}

__attribute__((constructor)) static void ManosabaStartMemoryLogger(void)
{
    if (!ManosabaSettingBool(@"manosaba_diagnostics")) return;  // Settings app: "调试日志"
    static dispatch_source_t timer;
    timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0));
    dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 0), NSEC_PER_SEC / 2, NSEC_PER_SEC / 20);
    dispatch_source_set_event_handler(timer, ^{ ManosabaLogMemory("tick", false); });
    dispatch_resume(timer);
    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidReceiveMemoryWarningNotification
                                                      object:nil queue:nil
                                                  usingBlock:^(NSNotification*) { ManosabaLogMemory("WARNING", true); }];
    [[NSNotificationCenter defaultCenter] addObserverForName:NSProcessInfoThermalStateDidChangeNotification
                                                      object:nil queue:nil
                                                  usingBlock:^(NSNotification*) {
        printf("[thermal] %s\n", ManosabaThermal());
        fflush(stdout);
    }];
}
