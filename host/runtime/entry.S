// Two common entries + a static stub table.
// 0) unimpl_stubs: named stubs for unimplemented imports, the k-th = movz x16,#k; b unimpl_entry (static code; iOS does not allow runtime generation)
// 1) unimpl_entry: stub entry for unimplemented imports, x16 = stub number; on the thread's host stack (hstack.S dm_hcall) save x0-x8+lr and call unimpl_c(idx, regs).
// 2) dm_gate: the rewriter replaces svc / cntpct reads with "bl to this entry". On entry lr = the gate block return point (gate_dispatch uses it to look up which gate this is);
//    take the thread's host stack (hstack.S), save x0-x30 there, call gate_dispatch(ret, regs), write back x0 (gate return value), return.
#include "asm.h"
    .text
    .globl C(unimpl_stubs)
    .p2align 3
C(unimpl_stubs):
    .set k, 0
    .rept 4096                // = hle.c MAX_STUB
    movz x16, #k
    b C(unimpl_entry)
    .set k, k + 1
    .endr

    .globl C(unimpl_entry)
    .p2align 2
C(unimpl_entry):
    adr x17, L(unimpl_body)
    b C(dm_hcall)               // x16 (the stub number) passes through
L(unimpl_body):
    stp x29, x30, [sp, #-96]!
    mov x29, sp
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    stp x4, x5, [sp, #48]
    stp x6, x7, [sp, #64]
    str x8, [sp, #80]
    mov x0, x16
    add x1, sp, #16
    bl C(unimpl_c)
    ldp x29, x30, [sp], #96
    ret

    .globl C(dm_gate)
    .p2align 2
C(dm_gate):
    // Take the thread's host stack (hstack.S) with x16 / x17 as scratch, kept on the guest stack: the guest stack holds 32 bytes, these and the gate
    // block's x29 / x30. Then the record (DM_HREC = 192 bytes, kind 2) at the top of the host stack, or on the current stack when it is not taken:
    // [0] guest x29 [8] gate return point (a frame record) [16] guest sp [24] host top or 0 [104] kind; GetThreadContext3 reads it (hle_thread.c)
    stp x16, x17, [sp, #-16]!
    adrp x17, PAGE(dm_hstk_off)
    ldr x17, [x17, PAGEOFF(dm_hstk_off)]
    cbz x17, 1f
    SLOTBASE x16
    add x16, x16, x17
    ldr x17, [x16]
    cbz x17, 1f
    str xzr, [x16]
    add x16, sp, #32
    sub sp, x17, #192
    stp x16, x17, [sp, #16]
    b 2f
1:  add x16, sp, #32
    sub sp, sp, #192
    stp x16, xzr, [sp, #16]
2:  stp x29, x30, [sp]
    mov x17, #2
    str x17, [sp, #104]
    ldp x16, x17, [x16, #-32]   // the guest's x16 / x17 back
    // Open a 256-byte frame and save x0..x30 (x0..x29 in pairs, x30 alone); the gate may read and modify x0..x30
    sub sp, sp, #800
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    stp q0, q1, [sp, #256]
    stp q2, q3, [sp, #288]
    stp q4, q5, [sp, #320]
    stp q6, q7, [sp, #352]
    stp q8, q9, [sp, #384]
    stp q10, q11, [sp, #416]
    stp q12, q13, [sp, #448]
    stp q14, q15, [sp, #480]
    stp q16, q17, [sp, #512]
    stp q18, q19, [sp, #544]
    stp q20, q21, [sp, #576]
    stp q22, q23, [sp, #608]
    stp q24, q25, [sp, #640]
    stp q26, q27, [sp, #672]
    stp q28, q29, [sp, #704]
    stp q30, q31, [sp, #736]
    mrs x9, nzcv
    str x9, [sp, #768]
    mrs x9, fpsr
    str x9, [sp, #776]
    mrs x9, fpcr
    str x9, [sp, #784]
    add x29, sp, #800        // the record: host backtraces continue into the guest
    mov x0, x30              // Return point = gate identifier
    mov x1, sp               // regs
    bl C(gate_dispatch)
    ldr x9, [sp, #824]       // give the host stack back
    cbz x9, 3f
    SLOTBASE x10
    adrp x11, PAGE(dm_hstk_off)
    ldr x11, [x11, PAGEOFF(dm_hstk_off)]
    str x9, [x10, x11]
3:  ldr x9, [sp, #784]
    msr fpcr, x9
    ldr x9, [sp, #776]
    msr fpsr, x9
    ldr x9, [sp, #768]
    msr nzcv, x9
    ldp q0, q1, [sp, #256]
    ldp q2, q3, [sp, #288]
    ldp q4, q5, [sp, #320]
    ldp q6, q7, [sp, #352]
    ldp q8, q9, [sp, #384]
    ldp q10, q11, [sp, #416]
    ldp q12, q13, [sp, #448]
    ldp q14, q15, [sp, #480]
    ldp q16, q17, [sp, #512]
    ldp q18, q19, [sp, #544]
    ldp q20, q21, [sp, #576]
    ldp q22, q23, [sp, #608]
    ldp q24, q25, [sp, #640]
    ldp q26, q27, [sp, #672]
    ldp q28, q29, [sp, #704]
    ldp q30, q31, [sp, #736]
    // A gate may modify any general-purpose register: svc out-parameters are in x0..x8, and the target of mrs cntpct can be any xN (14 of the 33 sites in sdk are x19..x23)
    // ⇒ read back all of x0..x29 (x18 is the Apple platform register; the guest's x18 has been rewritten to the thread slot, so skip it and leave it alone)
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldr x19, [sp, #152]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    ldr x16, [sp, #816]      // back to the guest stack; the final x16 / x17 (a cntpct target can be either) travel through their slot there
    ldr x17, [sp, #128]
    stur x17, [x16, #-32]
    ldr x17, [sp, #136]
    stur x17, [x16, #-24]
    sub sp, x16, #32
    ldp x16, x17, [sp], #16
    ret

// 3) dm_thread_enter(entry, arg, sp, &host top): guest thread start. The host stack below the caller's frame becomes this thread's host stack
//    (hstack.S; its top is also written to *x3), then switch to the guest stack given by the SDK and call entry with x0 = argument;
//    Horizon threads must exit via ExitThread; returning from entry is reported as an error.
    .globl C(dm_thread_enter)
    .p2align 2
C(dm_thread_enter):
    adrp x9, PAGE(dm_hstk_off)
    ldr x9, [x9, PAGEOFF(dm_hstk_off)]
    cbz x9, 1f
    mov x10, sp
    SLOTBASE x11
    str x10, [x11, x9]
    cbz x3, 1f
    str x10, [x3]
1:  mov sp, x2
    mov x29, #0
    mov x16, x0
    mov x0, x1
    adr x30, 2f
    br x16
2:  bl C(dm_thread_returned)
    brk #0
