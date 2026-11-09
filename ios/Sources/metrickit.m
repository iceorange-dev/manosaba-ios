// MetricKit's reports (plan item 317a; S2Arm ios/diag.m's idea, Apache-2.0, written anew): the system's own count of how the app's sessions
// ended (in the foreground: normally, the memory limit, the watchdog, a bad access, an illegal instruction, abnormally; in the background those
// and more) and its crash, hang, CPU and disk-write diagnostics, which reach the app at a later launch. The boot log knew only that a session ended
// in the foreground without an orderly exit (main.m rundiag); now each report is saved whole as JSON in Documents/metrickit and named in the log,
// each once (mxseen.h: by its kind and time span; the system hands the earlier ones again at every launch). Whether a free account's install gets
// reports is logged too: how many earlier ones the system had at this launch
#import <Foundation/Foundation.h>
#import <MetricKit/MetricKit.h>
#include "mxseen.h"
void plog(const char *fmt, ...);   // main.m
const char *dm_docs_path(void);    // main.m
@interface DmMX : NSObject <MXMetricManagerSubscriber>
@end
static NSString *mx_dir(void) {
    NSString *d = [@(dm_docs_path()) stringByAppendingPathComponent:@"metrickit"];
    [NSFileManager.defaultManager createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:nil];
    return d;
}
static NSString *mx_when(NSDate *d) { NSDateFormatter *f = [NSDateFormatter new]; f.dateFormat = @"yyyy-MM-dd HH:mm"; f.timeZone = [NSTimeZone timeZoneWithAbbreviation:@"UTC"]; return [f stringFromDate:d]; }
// 1 when the report is new (its JSON saved), 0 when taken before
static int mx_new(const char *kind, NSDate *b, NSDate *e, NSData *json) {
    char key[96]; snprintf(key, sizeof key, "%s %lld %lld", kind, (long long)b.timeIntervalSince1970, (long long)e.timeIntervalSince1970);
    NSString *dir = mx_dir();
    int r = mx_seen_add([dir stringByAppendingPathComponent:@"seen.txt"].fileSystemRepresentation, key);
    if (r < 0) plog("[red] MetricKit: Documents/metrickit/seen.txt cannot be written");
    if (r != 0 && json) [json writeToFile:[dir stringByAppendingPathComponent:[NSString stringWithFormat:@"%s_%lld.json", kind, (long long)b.timeIntervalSince1970]] atomically:YES];
    return r != 0;
}
static void mx_metric(MXMetricPayload *p) {
    if (!mx_new("metric", p.timeStampBegin, p.timeStampEnd, p.JSONRepresentation)) return;
    MXAppExitMetric *x = p.applicationExitMetrics;
    if (!x) { plog("MetricKit: a report for %s - %s UTC (version %s), no exit counts", mx_when(p.timeStampBegin).UTF8String, mx_when(p.timeStampEnd).UTF8String, p.latestApplicationVersion.UTF8String); return; }
    MXForegroundExitData *f = x.foregroundExitData; MXBackgroundExitData *g = x.backgroundExitData;
    int bad = f.cumulativeMemoryResourceLimitExitCount || f.cumulativeAppWatchdogExitCount || f.cumulativeBadAccessExitCount || f.cumulativeIllegalInstructionExitCount || f.cumulativeAbnormalExitCount;
    plog("%sMetricKit: sessions ended %s - %s UTC (version %s): in the foreground normally %lu, at the memory limit %lu, by the watchdog %lu, bad access %lu, illegal "
         "instruction %lu, abnormally %lu; in the background normally %lu, memory limit %lu, memory pressure %lu, CPU limit %lu, watchdog %lu, bad access %lu, illegal "
         "instruction %lu, abnormally %lu, a locked file %lu, a background task's time %lu", bad ? "[red] " : "", mx_when(p.timeStampBegin).UTF8String, mx_when(p.timeStampEnd).UTF8String,
         p.latestApplicationVersion.UTF8String, (unsigned long)f.cumulativeNormalAppExitCount, (unsigned long)f.cumulativeMemoryResourceLimitExitCount, (unsigned long)f.cumulativeAppWatchdogExitCount,
         (unsigned long)f.cumulativeBadAccessExitCount, (unsigned long)f.cumulativeIllegalInstructionExitCount, (unsigned long)f.cumulativeAbnormalExitCount,
         (unsigned long)g.cumulativeNormalAppExitCount, (unsigned long)g.cumulativeMemoryResourceLimitExitCount, (unsigned long)g.cumulativeMemoryPressureExitCount,
         (unsigned long)g.cumulativeCPUResourceLimitExitCount, (unsigned long)g.cumulativeAppWatchdogExitCount, (unsigned long)g.cumulativeBadAccessExitCount,
         (unsigned long)g.cumulativeIllegalInstructionExitCount, (unsigned long)g.cumulativeAbnormalExitCount, (unsigned long)g.cumulativeSuspendedWithLockedFileExitCount,
         (unsigned long)g.cumulativeBackgroundTaskAssertionTimeoutExitCount);
}
static void mx_diag(MXDiagnosticPayload *p) {
    if (!mx_new("diag", p.timeStampBegin, p.timeStampEnd, p.JSONRepresentation)) return;
    for (MXCrashDiagnostic *c in p.crashDiagnostics)
        plog("[red] MetricKit: a crash between %s and %s UTC (version %s): exception type %s, code %s, signal %s%s%s (the call stack in Documents/metrickit/diag_%lld.json)",
             mx_when(p.timeStampBegin).UTF8String, mx_when(p.timeStampEnd).UTF8String, c.applicationVersion.UTF8String, c.exceptionType.stringValue.UTF8String ?: "-",
             c.exceptionCode.stringValue.UTF8String ?: "-", c.signal.stringValue.UTF8String ?: "-", c.terminationReason ? ", " : "", c.terminationReason.UTF8String ?: "",
             (long long)p.timeStampBegin.timeIntervalSince1970);
    for (MXHangDiagnostic *h in p.hangDiagnostics)
        plog("[red] MetricKit: a hang of %.1f s between %s and %s UTC (version %s)", [h.hangDuration measurementByConvertingToUnit:NSUnitDuration.seconds].doubleValue,
             mx_when(p.timeStampBegin).UTF8String, mx_when(p.timeStampEnd).UTF8String, h.applicationVersion.UTF8String);
    if (p.cpuExceptionDiagnostics.count || p.diskWriteExceptionDiagnostics.count)
        plog("MetricKit: %lu CPU and %lu disk-write exception reports between %s and %s UTC (Documents/metrickit)", (unsigned long)p.cpuExceptionDiagnostics.count,
             (unsigned long)p.diskWriteExceptionDiagnostics.count, mx_when(p.timeStampBegin).UTF8String, mx_when(p.timeStampEnd).UTF8String);
}
@implementation DmMX
- (void)didReceiveMetricPayloads:(NSArray<MXMetricPayload *> *)payloads { for (MXMetricPayload *p in payloads) mx_metric(p); }
- (void)didReceiveDiagnosticPayloads:(NSArray<MXDiagnosticPayload *> *)payloads { for (MXDiagnosticPayload *p in payloads) mx_diag(p); }
@end
void dm_metrickit_start(void) {   // main.m, at launch
    static DmMX *s; if (s) return; s = [DmMX new];
    MXMetricManager *m = MXMetricManager.sharedManager; [m addSubscriber:s];
    NSArray<MXMetricPayload *> *pm = m.pastPayloads; NSArray<MXDiagnosticPayload *> *pd = m.pastDiagnosticPayloads;
    plog("boot: MetricKit: subscribed; the system had %lu earlier metric and %lu earlier diagnostic reports", (unsigned long)pm.count, (unsigned long)pd.count);
    for (MXMetricPayload *p in pm) mx_metric(p);
    for (MXDiagnosticPayload *p in pd) mx_diag(p);
}
