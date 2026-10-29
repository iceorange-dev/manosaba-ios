// Android build (plan item 301c): Darwin's instruction cache invalidation for the code the host writes (hle_count.c, diag.c), as the compiler's own
// __builtin___clear_cache (dc cvau / ic ivau over the range, the same work on arm64 Linux).
#pragma once
#include <stddef.h>
static inline void sys_icache_invalidate(void *start, size_t len) { __builtin___clear_cache((char *)start, (char *)start + len); }
