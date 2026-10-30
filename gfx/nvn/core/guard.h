// gfx/nvn/core/guard.h: reads of guest memory the game may have taken back since it submitted the work that reads it (plan item 290). With a
// translation thread the replay runs after the submit call, and a texture is uploaded at its first use there: if the game unmapped the texture's
// storage in between (MapMemory / UnmapMemory, UnmapPhysicalMemory), the read faulted and the process died (MacNativeGames f3bb714, MIT: SIGBUS in
// the deswizzle, Tears of the Kingdom's Depths; written anew here). Such reads run under guard_run, and the backend drops what it was making. Checked
// without a GPU by gfx/tests/t_core_guard.c; with the Metal backend by t_texgone.m
#pragma once
#include <stddef.h>
#include <stdint.h>

// fn(ctx) on this thread with a fault inside [lo, lo + n) caught: 1 when fn finished, 0 when such a fault ended it (whatever fn had allocated by
// then is the caller's to free: keep it in ctx). A fault elsewhere, or on another thread, is not caught. Not nested: fn calls no guard_run
int guard_run(const void *lo, size_t n, void (*fn)(void *), void *ctx);
uint64_t guard_faults(void);   // faults caught, all along
