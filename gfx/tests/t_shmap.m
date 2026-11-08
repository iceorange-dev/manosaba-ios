// The shader map's fallback by functional key (nvn_mtl.m shmap_resolve, plan item 169): a key in the map is itself; a key not in it whose functional
// key the map has gets that key's twin (the first key listed with it); without a functional key, or with one the map lacks: none. The map file is
// read by shmap_load: its third column fills the functional key table, the first key of a functional key kept; programs' functional keys are
// kept by serial beside prog_t. No fallback (-DDM_TEST_SHMAP_NO_FKEY): 1 fails. The libraries an app packs once (plan item 311b, gfx/tools/
// shader_pack.py): the directory's twins.tsv read with the map, a library it lists opened as the one packed in its place, any other as itself;
// the table not read (-DDM_TEST_TWINS_IGNORED): 1 fails
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define CHECK(c, ...) do { count++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static void hex8(const char *h, uint8_t out[8]) { for (int i = 0; i < 8; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); out[i] = (uint8_t)v; } }
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    char dir[512], path[600]; snprintf(dir, sizeof dir, "%s/t_shmap_%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", getpid()); mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/shader_map.tsv", dir);
    FILE *f = fopen(path, "w");
    fprintf(f, "# shader stage map (test): key, stage, functional key\n1111111111111111\tps\taaaaaaaaaaaaaaaa\n2222222222222222\tps\taaaaaaaaaaaaaaaa\n3333333333333333\tvs\t\n4444444444444444\tvs\n");
    fclose(f);
    char chain[600], twins[700]; snprintf(chain, sizeof chain, "%s/b-%s", dir, DMSH_CHAIN); mkdir(chain, 0755); snprintf(twins, sizeof twins, "%s/twins.tsv", chain);
    f = fopen(twins, "w"); fprintf(f, "# libraries packed once (test)\n2222222222222222_b.metallib\t1111111111111111_b.metallib\n2222222222222222_bf.metallib\t1111111111111111_bf.metallib\n"); fclose(f);
    setenv("DM_NVN_SHMAP", path, 1); setenv("DM_MSL_DIR", dir, 1); shmap_load();
    uint8_t fa[8], fb[8], fz[8] = {0}; hex8("aaaaaaaaaaaaaaaa", fa); hex8("bbbbbbbbbbbbbbbb", fb);
    CHECK([shmap_resolve(@"2222222222222222", fb, 0) isEqual:@"2222222222222222"], "a key in the map: itself");
    CHECK([shmap_resolve(@"9999999999999999", fa, 1) isEqual:@"1111111111111111"], "a key not in the map with a functional key the map has: the first key listed with it");
    CHECK(shmap_resolve(@"9999999999999998", fb, 1) == nil && shmap_resolve(@"9999999999999997", fz, 1) == nil, "with a functional key the map lacks, or none: nothing");
    CHECK(g_fmap.count == 1 && g_shmap.count == 4, "the table: one functional key (rows without one left out), four keys (%lu / %lu)", (unsigned long)g_fmap.count, (unsigned long)g_shmap.count);
    {   // the programs' functional keys kept by serial (prog_t cannot grow): set, read back by stage, dropped with the program
        prog_t pg = {.serial = 98765}; uint8_t fk[6][8] = {{0}}, got[8]; memcpy(fk[1], fa, 8); mtl_prog_fkeys(pg.serial, fk);
        prog_fkey(pg.serial, 1, got); int set = !memcmp(got, fa, 8); prog_fkey(pg.serial, 0, got); int zero0 = !memcmp(got, fz, 8);
        mtl_prog_release(&pg); prog_fkey(98765, 1, got);
        CHECK(set && zero0 && !memcmp(got, fz, 8), "a program's functional keys by serial: stage 1's read back, stage 0 none, gone once the program is released"); }
    CHECK([msl_lib_path(@"2222222222222222_b.metallib") isEqual:[g_msl_dir stringByAppendingPathComponent:@"1111111111111111_b.metallib"]] &&
          [msl_lib_path(@"2222222222222222_bf.metallib") isEqual:[g_msl_dir stringByAppendingPathComponent:@"1111111111111111_bf.metallib"]],
          "a library twins.tsv lists: opened as the one packed in its place [-DDM_TEST_TWINS_IGNORED]");
    CHECK([msl_lib_path(@"3333333333333333_b.metallib") isEqual:[g_msl_dir stringByAppendingPathComponent:@"3333333333333333_b.metallib"]] && g_twins.count == 2,
          "any other library as itself; the table's comment line left out (%lu rows)", (unsigned long)g_twins.count);
    unlink(twins); rmdir(chain); unlink(path); rmdir(dir);
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
