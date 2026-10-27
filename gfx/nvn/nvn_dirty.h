// Write-protect dirty-page tracking for guest memory (nvn_dirty.c)
#pragma once
#include <stddef.h>
#include <stdint.h>

void nvn_dirty_add_range(const void *mem, size_t size);           // Register a memory pool as a tracked range (the first call registers the page-fault hook and the host pre-write hook)
void nvn_dirty_stat(int *live, int *peak, int *cap);                // number of registered memory pools, peak, tracking table capacity (for the memory readout line)
void nvn_dirty_remove_range(const void *mem);                     // Pool destruction: restore protected pages to writable and mark invalid
int nvn_dirty_tracked(const void *addr, size_t len);
void nvn_dirty_protect(const void *addr, size_t len);              // Set the covered pages read-only
uint64_t nvn_dirty_now(void);                                      // Current generation: taken before upload, as the stamp of that content
uint64_t nvn_dirty_protect_stamp(const void *addr, size_t len);    // The stamp of the content about to be read, then the pages set read-only (in this order)
int nvn_dirty_since(const void *addr, size_t len, uint64_t stamp); // Covered pages were written after the stamp -> 1
int nvn_dirty_state(const void *addr, size_t len, uint64_t stamp); // nvn_dirty_tracked and nvn_dirty_since in one lookup: 0 = clean, 1 = written after the stamp, 2 = not tracked
int nvn_dirty_state_hint(const void *addr, size_t len, uint64_t stamp, int *hint);   // the same, remembering the range's slot in *hint (-1 at first)
uint64_t nvn_dirty_hint_walks(void);                               // how often nvn_dirty_state_hint had to walk the ranges
void nvn_dirty_stats(uint64_t *faults, uint64_t *preps);           // Cumulative: number of page faults triggered by game writes, number of pages unprotected before host writes
void nvn_dirty_mp_stats(uint64_t *calls, uint64_t *pages, uint64_t *ns);   // Cumulative: this module's mprotect call count, pages covered, time spent
void nvn_dirty_slot_stats(int *live, int *used_slots, uint64_t *adds_total); // Currently live slots, slot table high-water mark, total number of registrations
