// Manosaba: the shell's options from the iOS Settings app (Settings.bundle/Root.plist).
#pragma once
#import <Foundation/Foundation.h>

extern "C" BOOL ManosabaSettingBool(NSString* key);
extern "C" NSInteger ManosabaSettingInt(NSString* key);
