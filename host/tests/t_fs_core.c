// nn::fs calls give the virtual core up while they run (hle_fs.c FS_BLOCKING, plan item 297; valentderah/totk-pc-recomp 101f2c8, MIT): with the
// scheduler's two calls counted (t_fs_core_stub.c, a thread that holds a core), QueryMountRomCacheSize and an OpenFile that fails each give it up
// once and take it back once (an early return too); the core kept (-DDM_TEST_FS_KEEPS_CORE): 2 fail.
#include "../hle/hle_fs.c"
struct dm_flags g_flag; uint64_t g_t0_ns;
uint64_t now_ns(void) { return 0; }
void dm_prof_add(int id, uint64_t ns, uint64_t b) { (void)id; (void)ns; (void)b; }
void dm_guest_write_prepare(void *a, size_t n) { (void)a; (void)n; }
extern int g_aways, g_backs;
static int checks, fails;
#define CHECK(c, text) do { checks++; int ok = !!(c); fails += !ok; printf("%s %s\n", ok ? "ok" : "FAIL", text); } while (0)
int main(void) {
    size_t sz = 0; Result r = h_fs_QueryRomCache(&sz);
    CHECK(r == 0 && sz == 0x1000 && g_aways == 1 && g_backs == 1, "QueryMountRomCacheSize: the core given up once and taken back once");
    uint64_t h = 0; h_fs_OpenFile(&h, "nomount:/x", MODE_READ);
    CHECK(g_aways == 2 && g_backs == 2, "an OpenFile that fails (no such mount): the same, on its early return");
    printf("t_fs_core: %d checks, %d failed\n", checks, fails); return fails != 0;
}
