// The interrupted registers in a signal handler's context (crash.c, diag.c and the fault hooks), on Darwin and on Linux (plan item 301c). Each is an
// lvalue: a hook may move the pc on. uc_esr reads the fault's syndrome (the WnR bit: a write), which Linux keeps in an esr_context record.
#pragma once
#include <signal.h>
#include <stdint.h>
#include <sys/ucontext.h>
#ifdef __APPLE__
#define UC_X(uc, i) ((uc)->uc_mcontext->__ss.__x[i])   // x0-x28
#define UC_FP(uc) ((uc)->uc_mcontext->__ss.__fp)
#define UC_LR(uc) ((uc)->uc_mcontext->__ss.__lr)
#define UC_SP(uc) ((uc)->uc_mcontext->__ss.__sp)
#define UC_PC(uc) ((uc)->uc_mcontext->__ss.__pc)
#define UC_XS(uc) ((uint64_t *)(uc)->uc_mcontext->__ss.__x)   // x0-x28 as an array
#define UC_FAR(uc) ((uc)->uc_mcontext->__es.__far)   // the faulting address
static inline uint64_t uc_esr(const ucontext_t *uc) { return uc->uc_mcontext->__es.__esr; }
static inline __uint128_t uc_v(const ucontext_t *uc, int r) { return uc->uc_mcontext->__ns.__v[r]; }   // q0-q31
#else
#include <asm/sigcontext.h>
#define UC_X(uc, i) ((uc)->uc_mcontext.regs[i])
#define UC_FP(uc) ((uc)->uc_mcontext.regs[29])
#define UC_LR(uc) ((uc)->uc_mcontext.regs[30])
#define UC_SP(uc) ((uc)->uc_mcontext.sp)
#define UC_PC(uc) ((uc)->uc_mcontext.pc)
#define UC_XS(uc) ((uint64_t *)(uc)->uc_mcontext.regs)
#define UC_FAR(uc) ((uc)->uc_mcontext.fault_address)
static inline const struct _aarch64_ctx *uc_record(const ucontext_t *uc, uint32_t magic) {   // a record of the frame's extra area
    const uint8_t *p = uc->uc_mcontext.__reserved, *end = p + sizeof uc->uc_mcontext.__reserved;
    for (const struct _aarch64_ctx *h = (const void *)p; (const uint8_t *)h + sizeof *h <= end && h->magic; h = (const void *)((const uint8_t *)h + h->size))
        if (h->magic == magic) return h;
    return 0;
}
static inline uint64_t uc_esr(const ucontext_t *uc) { const struct _aarch64_ctx *h = uc_record(uc, ESR_MAGIC); return h ? ((const struct esr_context *)h)->esr : 0; }
static inline __uint128_t uc_v(const ucontext_t *uc, int r) { const struct _aarch64_ctx *h = uc_record(uc, FPSIMD_MAGIC); return h ? ((const struct fpsimd_context *)h)->vregs[r] : 0; }
#endif
