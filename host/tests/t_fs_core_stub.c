// t_fs_core: the scheduler's core handoff counted, for a thread that holds a core (hle_thread.c's dm_core_away / dm_core_back)
int g_aways, g_backs;
int dm_core_away(void) { g_aways++; return 1; }
void dm_core_back(int *away) { if (*away) g_backs++; }
