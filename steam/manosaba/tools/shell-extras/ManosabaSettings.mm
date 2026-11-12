// Manosaba: the shell's options from the iOS Settings app (Settings.bundle/Root.plist).
// iOS only stores a value once the user changes it, so the plist's defaults are registered
// here as well (kept equal to Root.plist by tests/test_tools.py).
#import "ManosabaSettings.h"

static NSUserDefaults* ManosabaDefaults(void)
{
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
        [d registerDefaults:@{
            @"manosaba_play_in_silent_mode": @NO,
            @"manosaba_thermal_cap": @YES,
            @"manosaba_max_fps": @0,
            @"manosaba_icloud_sync": @YES,
            @"manosaba_diagnostics": @YES,
        }];
        NSString* version = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
        [d setObject:[NSString stringWithFormat:@"%@（Steam 版移植）", version ?: @"?"] forKey:@"manosaba_version"];
    });
    return NSUserDefaults.standardUserDefaults;
}

extern "C" BOOL ManosabaSettingBool(NSString* key) { return [ManosabaDefaults() boolForKey:key]; }
extern "C" NSInteger ManosabaSettingInt(NSString* key) { return [ManosabaDefaults() integerForKey:key]; }
