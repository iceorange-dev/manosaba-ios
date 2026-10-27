// Metal execution layer of the NVN null device (C interface): memory pool = zero-copy MTLBuffer, submit = MTLCommandBuffer, sync = MTLSharedEvent.
// Ordering convention (to avoid repeating the GO_MAP_SHARE corrupted-picture mistake of the x86 route): every visible effect of "GPU completion" (sync object ready, event values, counters)
// happens only after the Metal command buffer has actually completed; CPU-side waits look only at the completed value of the MTLSharedEvent.
#pragma once
#include <stdint.h>
#include <stddef.h>

void mtl_init(void);
void bf_want_note(const char *key);

// Memory pool: [mem, mem+size) is expanded outward to page boundaries to build a zero-copy buffer; look up the buffer and offset by GPU address (= CPU address)
void mtl_pool_add(const void *mem, uint64_t size);
void mtl_pool_del(const void *mem);
int mtl_addr(uint64_t addr, uint64_t len, void **buf, uint64_t *off);   // 1 on success; buf is a (__bridge) id<MTLBuffer>
int mtl_pool_has(uint64_t addr, uint64_t len);                          // 1 when [addr, addr + len) lies in a registered memory pool (no residency side effect, unlike mtl_addr)

typedef struct {
    uint32_t pso, cps, shaders, dss;
    uint32_t view_nonnull, view_slots, live_tex, sview_nonnull;
    uint32_t pools;
    uint64_t pool_adds;
    uint32_t c1_bufs; uint64_t c1_alloc, c1_data;   // Shader c1 constant segment: buffer count (4 MiB blocks + dedicated 64 KiB), allocated bytes, actual bytes
} mtl_res_count;
void mtl_res_snapshot(mtl_res_count *out);

// One submit: begin -> several operations -> commit (returns the fence value at which this submit completes)
void mtl_sub_begin(void);
void mtl_sub_counter(uint64_t addr, uint32_t type);                       // Write the counter record after completion
uint64_t mtl_sub_commit(void);

uint64_t mtl_last_fence(void);                       // Fence value of the most recent submit (0 = never submitted)
int mtl_wait(uint64_t fence, uint64_t timeout_ns);   // Returns 1 when the GPU has completed up to fence, 0 on timeout
uint64_t mtl_done_fence(void);                       // Highest completed submission fence
void mtl_finish(void);                               // Wait for all submitted commands to complete
void mtl_wb_fence_done(void);                        // After the game's fence waits: small GPU-written targets of CPU-visible pools whose rendering completed, written back (plan item 278)
void mtl_drain(void);                                // Wait for all commands on the queue (including presents) to complete: submit an empty command buffer and wait for it
void mtl_xv_window_tex(uint64_t serial);   // Register a window (swapchain) texture: the cross-submit validity ledger never collects it
