#include "runtime/asm.h"
    .arch_extension lse
// Common entry for unimplemented NVN functions: when the thunk comes in, x16 = this function's slot address (nvn_slot[i]).
// Calls nvn_unimpl_c(slot, x0, x1, x2, lr, x8) to report a failure and log the name (with the call site and the indirect result pointer x8), returns 0.
    .text
    .globl C(nvn_unimpl_entry)
    .p2align 2
C(nvn_unimpl_entry):
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    mov x5, x8
    mov x4, x30
    mov x3, x2
    mov x2, x1
    mov x1, x0
    mov x0, x16
    bl C(nvn_unimpl_c)
    ldp x29, x30, [sp], #16
    ret

// Timing wrapper entry (the slot table points here when DM_NVN_PROF=1): when the thunk comes in, x16 = this function's slot address.
// Save x0-x7 / q0-q7 (NVN functions pass no arguments on the stack, verified by the generator), take mach_absolute_time -> call the real implementation (nvn_real[i]) -> take it again,
// atomically add the difference to nvn_prof_ticks[i] (mach timebase units); return values x0/x1, q0-q3 are passed back unchanged.
// cntvct_el0 is not used: measured on this machine it is not in a fixed ratio to the system timebase (cntfrq reports 1 GHz, increment ratio over the same interval 98.9), so it cannot be converted to time.
    .globl C(nvn_prof_entry)
    .p2align 2
C(nvn_prof_entry):
    sub sp, sp, #240
    stp x29, x30, [sp]
    mov x29, sp
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    stp x4, x5, [sp, #48]
    stp x6, x7, [sp, #64]
    stp q0, q1, [sp, #96]
    stp q2, q3, [sp, #128]
    stp q4, q5, [sp, #160]
    stp q6, q7, [sp, #192]
    adrp x9, PAGE(nvn_slot)
    add x9, x9, PAGEOFF(nvn_slot)
    sub x16, x16, x9
    lsr x16, x16, #4                 // Slot index i
    str x16, [sp, #80]
    bl MACH_TICKS
    str x0, [sp, #88]
    ldr x16, [sp, #80]
    adrp x9, PAGE(nvn_real)
    add x9, x9, PAGEOFF(nvn_real)
    ldr x17, [x9, x16, lsl #3]
    ldp x0, x1, [sp, #16]
    ldp x2, x3, [sp, #32]
    ldp x4, x5, [sp, #48]
    ldp x6, x7, [sp, #64]
    ldp q0, q1, [sp, #96]
    ldp q2, q3, [sp, #128]
    ldp q4, q5, [sp, #160]
    ldp q6, q7, [sp, #192]
    blr x17
    stp x0, x1, [sp, #16]
    stp q0, q1, [sp, #96]
    stp q2, q3, [sp, #128]
    bl MACH_TICKS
    mov x9, x0
    ldr x10, [sp, #88]
    sub x9, x9, x10
    ldr x16, [sp, #80]
    adrp x10, PAGE(nvn_prof_ticks)
    add x10, x10, PAGEOFF(nvn_prof_ticks)
    add x10, x10, x16, lsl #3
    stadd x9, [x10]
    ldp x0, x1, [sp, #16]
    ldp q0, q1, [sp, #96]
    ldp q2, q3, [sp, #128]
    ldp x29, x30, [sp]
    add sp, sp, #240
    ret
