// The Android host's front end without a window (host/front.h; plan item 301c, the host under adb): the guest runs as the Mac host's does without
// DM_WINDOW, on a thread of its own. The APK's front end (NativeActivity, its window, input and text entry) takes this file's place in 301d.
#include "front.h"
#include "dm.h"

// On a thread of its own with a fixed 16 MB stack: the process's main stack grows on demand on Linux, and the range the guest is told its stack is
// (svc QueryMemory, hle_svc.c) must hold for as long as it runs
static void *guest_main(void *g) { ((void (*)(void))g)(); return NULL; }
void front_run(void (*guest)(void)) {
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 16u << 20); pthread_t th;
    if (pthread_create(&th, &a, guest_main, (void *)guest)) die("cannot start the guest main thread");
    pthread_attr_destroy(&a); pthread_join(th, NULL);
}
void *front_metal_layer(void) { return NULL; }
int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap) {
    (void)title; (void)sub; (void)guide; (void)initial; (void)max_chars; (void)out; (void)out_cap; return -1;   // (no window: nothing shown)
}
void front_save_failed(int err) { LOG("[red] a save transaction failed (errno %d); the committed save is unchanged", err); }
void dm_front_controller_key(void) {}
