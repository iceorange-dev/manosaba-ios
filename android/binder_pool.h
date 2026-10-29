// The process's binder thread pool, for an executable run from adb shell (the host without a window, the unit tests): the codecs (MediaCodec,
// movie_android.c) answer through binder, and without the pool their answers never arrive (a decoder took input and gave nothing). An app's
// process has the pool already. ABinderProcess_startThreadPool is not in the NDK's libbinder_ndk: taken from the system's at run time.
#pragma once
#include <dlfcn.h>
static inline int dm_binder_pool_start(void) {
    void *h = dlopen("libbinder_ndk.so", RTLD_NOW);
    int (*started)(void) = h ? (int (*)(void))dlsym(h, "ABinderProcess_isThreadPoolStarted") : 0;
    void (*start)(void) = h ? (void (*)(void))dlsym(h, "ABinderProcess_startThreadPool") : 0;
    if (!start) return -1;
    if (!started || !started()) start();
    return 0;
}
