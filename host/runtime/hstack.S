// Host calls on a per-thread host stack (after MacNativeGames games/totk/runtime/hle_stack.S and gate.S, MIT; mush8 has the same idea).
// The host functions that replace SDK functions used to run on the calling guest thread's stack: a few KiB of path buffers, or a libc time zone
// load, on a guest stack of 4 KiB (Tears of the Kingdom makes such threads) wrote below it, and a 16 KiB host page makes the guest's 4 KiB guard
// page writable. Each guest thread's host pthread stack top sits in a TSD slot (dm_hstk_off = key * 8 from tpidrro_el0, hstack.c), set by
// dm_thread_enter. A call takes the stack by clearing the slot and gives it back on return, so calls made while on it (a host function calling
// guest code that calls the host again), the main thread and host threads stay where they are.
// The record at the top of the host stack, DM_HREC = 192 bytes (dm.h): [0] guest x29, [8] guest x30 (a frame record: host backtraces continue
// into the guest caller), [16] guest sp, [24] host top, [32..96] x0..x8, [104] kind (1 host call, 2 svc gate, entry.S), [112..184] x19..x28.
// GetThreadContext3 reports a thread inside a host call from it (hle_thread.c): the IL2CPP garbage collector scans the guest stack from that sp,
// and these registers.
#include "asm.h"
    .text
// x17 = the host function; x16 passes through to it (the NVN thunks' slot address); x0..x8 and q0..q7 are its arguments; x9..x12 are used.
// No host function the guest calls takes arguments on the stack (gfx/tools/gen_protos.py checks the NVN ones, tools/hle_args.py the others).
    .globl C(dm_hcall)
    .globl C(dm_hcall_ret)
    .p2align 2
C(dm_hcall):
    adrp x10, PAGE(dm_hstk_off)
    ldr x10, [x10, PAGEOFF(dm_hstk_off)]
    cbz x10, 1f                  // switched off (DM_HSTACK=0)
    SLOTBASE x9
    ldr x11, [x9, x10]           // this thread's host stack top; 0: none here, or already on it
    cbz x11, 1f
    str xzr, [x9, x10]
    mov x12, sp
    sub sp, x11, #192
    stp x29, x30, [sp]
    stp x12, x11, [sp, #16]
    stp x0, x1, [sp, #32]
    stp x2, x3, [sp, #48]
    stp x4, x5, [sp, #64]
    stp x6, x7, [sp, #80]
    mov x9, #1
    stp x8, x9, [sp, #96]
    stp x19, x20, [sp, #112]
    stp x21, x22, [sp, #128]
    stp x23, x24, [sp, #144]
    stp x25, x26, [sp, #160]
    stp x27, x28, [sp, #176]
    mov x29, sp
    blr x17
C(dm_hcall_ret):                   // DM_GUEST_RA (dm.h) recognises a host function called from here
    ldp x12, x11, [sp, #16]
    ldp x29, x30, [sp]
    mov sp, x12
    SLOTBASE x9
    adrp x10, PAGE(dm_hstk_off)
    ldr x10, [x10, PAGEOFF(dm_hstk_off)]
    str x11, [x9, x10]           // the stack is free again
    ret
1:  br x17

// Wrappers handed to the guest in place of host functions (hstack.c dm_hwrap): wrapper i (8 bytes) calls dm_hthunk_fn[i] through dm_hcall.
// Static code: iOS runs no code written at run time.
    .globl C(dm_hthunks)
    .p2align 3
C(dm_hthunks):
    .set hk, 0
    .rept 2048                   // = hstack.c DM_HTHUNKS
    mov x17, #hk
    b L(hthunk)
    .set hk, hk + 1
    .endr
L(hthunk):
    adrp x16, PAGE(dm_hthunk_fn)
    add x16, x16, PAGEOFF(dm_hthunk_fn)
    ldr x17, [x16, x17, lsl #3]
    b C(dm_hcall)
