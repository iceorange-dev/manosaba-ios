// The scheduler's core handoff for a guest thread that holds a core (host hle_thread.c's dm_core_away / dm_core_back, plan item 297), counted for
// t_wb and t_pools; the moment the core comes back reported
int g_core_aways, g_core_backs; void (*g_core_on_back)(void);
int dm_core_away(void) { g_core_aways++; return 1; }
void dm_core_back(int *away) { if (*away) { g_core_backs++; if (g_core_on_back) g_core_on_back(); } }
