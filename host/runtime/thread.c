// Guest thread environment: gate slot = host gate entry; x18 slot cleared; tpidr_el0 slot = ELF TLS block; tpidrro_el0 slot = Horizon ThreadLocalRegion (0x200).
// The SDK's internal critical section takes the current thread object pointer from TLR[0x1f8]; here each thread gets a zero-initialized object allocated and written in.
#include "dm.h"
#define TLS_SIZE 8192
#define TLR_SIZE 0x200
#define THREAD_OBJ_SIZE 0x400
extern void dm_gate(void);            // entry.S: common gate entry
static pthread_key_t g_keyown;   // raw pointers of the TLS block / thread-local region allocated by this thread (see the end of the function)
static pthread_once_t g_keyown_once = PTHREAD_ONCE_INIT;
static void own_free(void *p) { void **own = p; free(own[0]); free(own[1]); free(own); }
static void own_key_init(void) { if (pthread_key_create(&g_keyown, own_free)) die("cannot create the key that frees thread-local blocks"); }
void guest_thread_init(void) {
    dm_slot_set(g_keygate, (void *)dm_gate);
    dm_slot_set(g_key18, 0);
    // These two blocks are freed when the thread exits. The guest overwrites the slots of the TLS / thread-local-region keys (it sets its own thread pointer), so they cannot be freed by the current value in the key:
    // the raw pointers that were allocated are recorded separately on this file's own key g_keyown and freed by its destructor (key destructors are called at pthread_exit, same as the signal alternate stack, crash.c altstack_free;
    // the destructor only calls free, which is safe on the small stack of a guest thread as well). They used not to be freed: the game starts short-lived threads frequently, and each leaked 8192 + 0x200 + 0x400 = 9728 bytes.
    // The thread-local region and the current thread object are merged into one block (the slot TLR[0x1f8] may also be overwritten by the SDK)
    void **own = calloc(2, sizeof *own); uint8_t *tlr = calloc(1, TLR_SIZE + THREAD_OBJ_SIZE);
    own[0] = calloc(1, TLS_SIZE); own[1] = tlr;
    *(void **)(tlr + 0x1f8) = tlr + TLR_SIZE;               // current thread object
    pthread_once(&g_keyown_once, own_key_init); dm_slot_set(g_keytls, own[0]); dm_slot_set(g_keytlr, tlr); pthread_setspecific(g_keyown, own);
}
