// Manosaba: keep the game's saves in step across the user's devices through iCloud.
// Local saves live in Documents/Saves_v1 (Application.persistentDataPath); the shared copy lives
// in the app's iCloud container under Saves_v1, together with the game's Settings.json.
//   * launch, before Unity starts and the game reads its saves: full two-way sync (<= 6 s)
//   * background / every 2 min / terminate: push local changes only
// Without the iCloud entitlement (or with iCloud Drive off) every step is a logged no-op.
#import <UIKit/UIKit.h>
#import <stdatomic.h>
#import "ICloudSaveSyncCore.h"
#import "ManosabaSettings.h"

extern "C" NSString* const kUnityWillFinishLaunchingWithOptions;  // PluginBase/AppDelegateListener.h

static dispatch_queue_t sQueue;
static NSURL *sLocal, *sCloud, *sManifest;
static atomic_bool sLaunchPullOpen = true;

static void ManosabaLogSync(const char* what, NSDictionary<NSString*, NSNumber*>* s, CFAbsoluteTime t0)
{
    printf("[icloud] %s: pulled=%d pushed=%d deletedLocal=%d deletedCloud=%d conflicts=%d skipped=%d errors=%d (%.2fs)\n", what,
           s[@"pulled"].intValue, s[@"pushed"].intValue, s[@"deletedLocal"].intValue, s[@"deletedCloud"].intValue,
           s[@"conflicts"].intValue, s[@"skipped"].intValue, s[@"errors"].intValue, CFAbsoluteTimeGetCurrent() - t0);
    fflush(stdout);
}

static BOOL ManosabaCloudReady(NSURL* url)
{
    NSString* status = nil;
    [url getResourceValue:&status forKey:NSURLUbiquitousItemDownloadingStatusKey error:nil];
    if (status == nil || [status isEqualToString:NSURLUbiquitousItemDownloadingStatusCurrent])
        return YES;
    [[NSFileManager defaultManager] startDownloadingUbiquitousItemAtURL:url error:nil];
    return NO;
}

// Ask iCloud for every save it knows about, then wait (bounded) for the downloads.
static void ManosabaDownloadCloudSaves(CFAbsoluteTime deadline)
{
    NSFileManager* fm = [NSFileManager defaultManager];
    for (;;)
    {
        int pending = 0;
        for (NSString* f in [fm contentsOfDirectoryAtPath:sCloud.path error:nil])
        {
            NSString* name = [f hasPrefix:@"."] && [f hasSuffix:@".icloud"] ? [[f substringFromIndex:1] stringByDeletingPathExtension] : f;
            if (!ManosabaIsSyncedName(name)) continue;
            if (!ManosabaCloudReady([sCloud URLByAppendingPathComponent:name])) pending++;
        }
        if (pending == 0 || CFAbsoluteTimeGetCurrent() > deadline)
        {
            if (pending) printf("[icloud] %d save(s) still downloading; they sync on a later launch\n", pending);
            return;
        }
        usleep(100 * 1000);
    }
}

static BOOL ManosabaResolve(void)
{
    static bool loggedUnavailable = false;
    if (sCloud) return YES;
    if (!ManosabaSettingBool(@"manosaba_icloud_sync"))  // Settings app: "iCloud 存档同步"
    {
        if (!loggedUnavailable) printf("[icloud] sync is off in Settings\n");
        loggedUnavailable = true;
        return NO;
    }
    NSURL* container = [[NSFileManager defaultManager] URLForUbiquityContainerIdentifier:nil];  // may block
    if (!container)
    {
        if (!loggedUnavailable) printf("[icloud] unavailable (no iCloud entitlement, not signed in, or iCloud Drive off)\n");
        loggedUnavailable = true;
        return NO;
    }
    sCloud = [container URLByAppendingPathComponent:@"Saves_v1" isDirectory:YES];
    printf("[icloud] container %s\n", container.path.UTF8String);
    return YES;
}

static void ManosabaPush(const char* why, NSTimeInterval settle)
{
    if (!ManosabaResolve()) return;
    CFAbsoluteTime t0 = CFAbsoluteTimeGetCurrent();
    NSDictionary* s = ManosabaSyncSaves(sLocal, sCloud, sManifest, ManosabaSyncModePushOnly, settle, ^BOOL(NSURL* u) { return ManosabaCloudReady(u); }, nil);
    if ([s[@"pushed"] intValue] || [s[@"deletedCloud"] intValue] || [s[@"errors"] intValue])
        ManosabaLogSync(why, s, t0);
}

static void ManosabaLaunchSync(void)
{
    CFAbsoluteTime t0 = CFAbsoluteTimeGetCurrent();
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    dispatch_async(sQueue, ^{
        if (ManosabaResolve())
        {
            ManosabaDownloadCloudSaves(t0 + 4.0);
            NSDictionary* s = ManosabaSyncSaves(sLocal, sCloud, sManifest, ManosabaSyncModeFull, 0,
                                                ^BOOL(NSURL* u) { return ManosabaCloudReady(u); },
                                                ^BOOL { return atomic_load(&sLaunchPullOpen); });
            ManosabaLogSync("launch sync", s, t0);
        }
        dispatch_semaphore_signal(done);
    });
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 6 * NSEC_PER_SEC)) != 0)
    {
        atomic_store(&sLaunchPullOpen, false);  // the game is about to read its saves: stop pulling
        printf("[icloud] launch sync timed out after 6 s; continuing without the rest of the pull\n");
        fflush(stdout);
    }
}

__attribute__((constructor)) static void ManosabaInstallICloudSaveSync(void)
{
    sQueue = dispatch_queue_create("manosaba.icloud-sync", DISPATCH_QUEUE_SERIAL);
    NSFileManager* fm = [NSFileManager defaultManager];
    sLocal = [[fm URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject URLByAppendingPathComponent:@"Saves_v1" isDirectory:YES];
    sManifest = [[fm URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject URLByAppendingPathComponent:@"manosaba-icloud-sync.json"];

    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    [nc addObserverForName:kUnityWillFinishLaunchingWithOptions object:nil queue:nil usingBlock:^(NSNotification*) { ManosabaLaunchSync(); }];
    [nc addObserverForName:UIApplicationDidEnterBackgroundNotification object:nil queue:nil usingBlock:^(NSNotification*) {
        __block UIBackgroundTaskIdentifier task = [UIApplication.sharedApplication beginBackgroundTaskWithName:@"manosaba.icloud-push" expirationHandler:^{
            [UIApplication.sharedApplication endBackgroundTask:task];
        }];
        dispatch_async(sQueue, ^{
            ManosabaPush("background push", 1);
            dispatch_async(dispatch_get_main_queue(), ^{ [UIApplication.sharedApplication endBackgroundTask:task]; });
        });
    }];
    [nc addObserverForName:UIApplicationWillTerminateNotification object:nil queue:nil usingBlock:^(NSNotification*) {
        dispatch_sync(sQueue, ^{ ManosabaPush("exit push", 1); });
    }];

    static dispatch_source_t timer;
    timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, sQueue);
    dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 120 * NSEC_PER_SEC), 120 * NSEC_PER_SEC, 10 * NSEC_PER_SEC);
    dispatch_source_set_event_handler(timer, ^{ ManosabaPush("periodic push", 3); });
    dispatch_resume(timer);
}
