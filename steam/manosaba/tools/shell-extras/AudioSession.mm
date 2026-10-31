// Manosaba: log audio route changes, interruptions and media-service resets (stdout -> device
// console), and restart FMOD's output after a device route change. On iPad, connecting or
// dropping a Bluetooth output (same 48 kHz, IO buffer 42.7 -> 10 ms) leaves the game silent:
// FMOD only restarts its output on AVAudioSessionInterruptionTypeEnded. Unity's own trampoline
// posts that notification itself after dictation (Keyboard.mm, keyboardDidHide:); do the same.
// Settings app "静音模式下仍播放声音" (manosaba_play_in_silent_mode): FMOD sets up the session as
// Ambient, which Silent Mode mutes; with the option on the category becomes Playback (mixing
// with other apps' audio, so music apps keep playing), applied whenever the app becomes active.
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#import "ManosabaSettings.h"

static NSString* ManosabaPorts(NSArray<AVAudioSessionPortDescription*>* ports)
{
    NSMutableArray* names = [NSMutableArray array];
    for (AVAudioSessionPortDescription* p in ports) [names addObject:[NSString stringWithFormat:@"%@(%@)", p.portType, p.portName]];
    return names.count ? [names componentsJoinedByString:@"+"] : @"none";
}

static void ManosabaLogSession(const char* what)
{
    AVAudioSession* s = [AVAudioSession sharedInstance];
    printf("[audio] %s: out=%s rate=%.0f ch=%ld io=%.1fms vol=%.2f category=%s otherAudio=%d\n", what,
           ManosabaPorts(s.currentRoute.outputs).UTF8String, s.sampleRate, (long)s.outputNumberOfChannels,
           s.IOBufferDuration * 1000, s.outputVolume, s.category.UTF8String, s.isOtherAudioPlaying);
    fflush(stdout);
}

static void ManosabaApplyAudioCategory(void)
{
    AVAudioSession* s = [AVAudioSession sharedInstance];
    BOOL loud = ManosabaSettingBool(@"manosaba_play_in_silent_mode");
    NSString* want = loud ? AVAudioSessionCategoryPlayback : AVAudioSessionCategoryAmbient;
    if ([s.category isEqualToString:want]) return;
    NSError* error = nil;
    if (![s setCategory:want withOptions:AVAudioSessionCategoryOptionMixWithOthers error:&error])
        printf("[audio] setCategory %s failed: %s\n", want.UTF8String, error.localizedDescription.UTF8String);
    ManosabaLogSession(loud ? "category -> Playback (plays in Silent Mode)" : "category -> Ambient");
}

__attribute__((constructor)) static void ManosabaStartAudioRouteLogger(void)
{
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    // After FMOD has set up its session (Unity starts it during launch) and on every return to the
    // app, which is also when a change made in the Settings app shows up.
    [nc addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:nil usingBlock:^(NSNotification*) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{ ManosabaApplyAudioCategory(); });
    }];
    [nc addObserverForName:AVAudioSessionRouteChangeNotification object:nil queue:nil usingBlock:^(NSNotification* n) {
        static const char* reasons[] = {"unknown", "newDevice", "oldDeviceGone", "category", "override", "?", "wake", "noRoute", "routeConfig"};
        NSUInteger r = [n.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];
        AVAudioSessionRouteDescription* prev = n.userInfo[AVAudioSessionRouteChangePreviousRouteKey];
        char what[160];
        snprintf(what, sizeof what, "route change (%s) from %s", r < 9 ? reasons[r] : "?", ManosabaPorts(prev.outputs).UTF8String);
        ManosabaLogSession(what);
        if (r == AVAudioSessionRouteChangeReasonNewDeviceAvailable || r == AVAudioSessionRouteChangeReasonOldDeviceUnavailable ||
            r == AVAudioSessionRouteChangeReasonOverride || r == AVAudioSessionRouteChangeReasonRouteConfigurationChange)
        {
            // Coalesce bursts of route changes into one restart, 0.3 s after the last one
            // (route notifications arrive on a background thread; the counter lives on main).
            dispatch_async(dispatch_get_main_queue(), ^{
                static uint64_t generation = 0;
                uint64_t mine = ++generation;
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                    if (mine != generation) return;
                    printf("[audio] restarting FMOD output (posting interruption ended)\n");
                    fflush(stdout);
                    [[NSNotificationCenter defaultCenter] postNotificationName:AVAudioSessionInterruptionNotification
                                                                        object:[AVAudioSession sharedInstance]
                                                                      userInfo:@{AVAudioSessionInterruptionTypeKey: @(AVAudioSessionInterruptionTypeEnded)}];
                    ManosabaApplyAudioCategory();  // FMOD's restart may reset the category
                });
            });
        }
    }];
    [nc addObserverForName:AVAudioSessionInterruptionNotification object:nil queue:nil usingBlock:^(NSNotification* n) {
        NSUInteger type = [n.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
        NSUInteger options = [n.userInfo[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue];
        printf("[audio] interruption %s options=%lu\n", type == AVAudioSessionInterruptionTypeBegan ? "began" : "ended", (unsigned long)options);
        fflush(stdout);
    }];
    [nc addObserverForName:AVAudioSessionMediaServicesWereResetNotification object:nil queue:nil usingBlock:^(NSNotification*) {
        ManosabaLogSession("media services were reset");
    }];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{ ManosabaLogSession("session at start"); });
}
