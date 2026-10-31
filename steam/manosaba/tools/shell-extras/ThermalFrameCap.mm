// Manosaba: lower the frame-rate cap as the device heats up.
// Busy scenes (several characters, each composited into its own render target) keep the A12Z's
// GPU saturated at 60 fps; within minutes the iPad reaches "serious" and throttles to 16-24 fps.
// Rendering fewer frames once it is warm keeps it from getting there: the game's own target
// (60) while nominal, 40 fps at fair, 30 fps at serious/critical (divisors of the 120 Hz
// ProMotion refresh, so pacing stays even). callbackFramerateChange: in
// UnityAppController+Rendering.mm calls ManosabaCapTargetFPS (inserted by patch_xcode.rb).
// Settings app: "发热时自动降帧" (manosaba_thermal_cap) and "最高帧率" (manosaba_max_fps).
#import <Foundation/Foundation.h>
#import "ManosabaSettings.h"

extern "C" void UnityFramerateChangeCallback(int targetFPS);  // UnityAppController+Rendering.mm

extern "C" int ManosabaCapTargetFPS(int targetFPS)
{
    int cap = (int)ManosabaSettingInt(@"manosaba_max_fps");  // 0: follow the game
    if (ManosabaSettingBool(@"manosaba_thermal_cap"))
    {
        int thermal = 0;
        switch (NSProcessInfo.processInfo.thermalState)
        {
            case NSProcessInfoThermalStateNominal: break;
            case NSProcessInfoThermalStateFair: thermal = 40; break;
            case NSProcessInfoThermalStateSerious:
            case NSProcessInfoThermalStateCritical: thermal = 30; break;
        }
        if (thermal && (cap <= 0 || thermal < cap)) cap = thermal;
    }
    int fps = cap <= 0 || (targetFPS > 0 && targetFPS < cap) ? targetFPS : cap;
    static int last = -1;
    if (fps != last)
    {
        printf("[fps] cap %d (game target %d)\n", fps, targetFPS);
        fflush(stdout);
        last = fps;
    }
    return fps;
}

__attribute__((constructor)) static void ManosabaInstallThermalFrameCap(void)
{
    // Re-apply the game's target through the cap; -1 means "ask Unity for the current target".
    // Only once the app has been active (Unity initialised): defaults change during launch too.
    static bool ready = false;
    void (^reapply)(NSNotification*) = ^(NSNotification*) {
        dispatch_async(dispatch_get_main_queue(), ^{ if (ready) UnityFramerateChangeCallback(-1); });
    };
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    [nc addObserverForName:NSProcessInfoThermalStateDidChangeNotification object:nil queue:nil usingBlock:reapply];
    [nc addObserverForName:NSUserDefaultsDidChangeNotification object:nil queue:nil usingBlock:reapply];
    [nc addObserverForName:@"UIApplicationDidBecomeActiveNotification" object:nil queue:nil usingBlock:^(NSNotification* n) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            ready = true;
            UnityFramerateChangeCallback(-1);
        });
    }];
}
