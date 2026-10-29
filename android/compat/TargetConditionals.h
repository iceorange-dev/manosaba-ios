// Android build (plan item 301c): Apple's TargetConditionals.h, for the shared host sources that test TARGET_OS_* to tell the Mac from iOS. On Android
// neither is the target, so every one is 0 and those sources take their non-Apple path (each such path is checked by the Android build itself).
#pragma once
#define TARGET_OS_MAC 0
#define TARGET_OS_OSX 0
#define TARGET_OS_IPHONE 0
#define TARGET_OS_IOS 0
#define TARGET_OS_SIMULATOR 0
#define TARGET_OS_VISION 0
#define TARGET_OS_TV 0
#define TARGET_OS_WATCH 0
