// The device's thermal state for the logs, the fps / power lines and perf.txt (plan item 193): NSProcessInfo's, 0 nominal, 1 fair, 2 serious,
// 3 critical, on the Mac and in the iOS app alike. Before, only gfx/nvn/hle_nvn.c's weak default existed, so every iOS "[perf] thermal" line read
// -1 while main.m's frame rate policy read the real state. Checked by host/tests/t_thermal.m
#import <Foundation/Foundation.h>
int dm_thermal_now(void) { return (int)NSProcessInfo.processInfo.thermalState; }
int dm_low_power_now(void) { return NSProcessInfo.processInfo.isLowPowerModeEnabled; }   // Low Power Mode (plan item 321: the frame step's probes wait while it is on)
