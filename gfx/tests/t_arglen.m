// The argument buffer's length from the reflection (gfx/nvn/mtl/shaders.inc shader_arg_len, plan item 314b; S2Arm 4c0bec5, Apache-2.0, written
// anew): a function Metal gives no argument buffer (as after S2Arm's killed compiler service), reflection ids 0..3, gets 32 bytes without asking
// Metal, so the load goes on; DM_NVN_ARGCHECK=1 asks Metal: a real argument buffer of four ids agrees (32), the function without one stops the
// process (the diagnostic). Each case runs in a child process (this program run again). Metal asked at every load, as before
// (-DDM_TEST_ARGLEN_FROM_METAL): 1 fails (the first child stops).
#include "../nvn/nvn_mtl.m"
#include <spawn.h>
#include <sys/wait.h>
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
extern char **environ;
static int fail, count;
#define CHECK(c, ...) do { count++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static int child(const char *self, const char *which, int argcheck) {   // exit status (0 = the length came out 32), or 128 + the signal
    char *argv[] = {(char *)self, (char *)which, NULL}; char ev[] = "DM_NVN_ARGCHECK=1"; char *env[256]; int n = 0;
    for (char **e = environ; *e && n < 250; e++) if (strncmp(*e, "DM_NVN_ARGCHECK=", 16)) env[n++] = *e;
    if (argcheck) env[n++] = ev; env[n] = NULL;
    pid_t pid; if (posix_spawn(&pid, self, NULL, NULL, argv, env)) return -1;
    int st = 0; waitpid(pid, &st, 0);
    return WIFSIGNALED(st) ? 128 + WTERMSIG(st) : WEXITSTATUS(st);
}
int main(int argc, char **argv) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 2) {   // a child: one shader's length
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); NSError *e = nil;
        id<MTLLibrary> l = [dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
            "struct AB { texture2d<float> t0 [[id(0)]]; texture2d<float> t1 [[id(1)]]; texture2d<float> t2 [[id(2)]]; texture2d<float> t3 [[id(3)]]; };\n"
            "fragment float4 real(constant AB &ab [[buffer(0)]]) { return ab.t0.read(uint2(0)) + ab.t1.read(uint2(0)) + ab.t2.read(uint2(0)) + ab.t3.read(uint2(0)); }\n"
            "fragment float4 none() { return float4(1); }\n" options:nil error:&e];
        if (!l) { printf("compile: %s\n", e.localizedDescription.UTF8String); return 2; }
        NvShader *o = [NvShader new]; o->stage = 1; strlcpy(o->key, "t_arglen", sizeof o->key);
        o->fn = [l newFunctionWithName:@(argv[1])]; o->rf.abi = 0; o->rf.nb = 4;
        for (int i = 0; i < 4; i++) o->rf.b[i] = (nvn_rbind){.kind = NVN_RB_TEX, .reg = (uint16_t)i, .dtt = 2, .aid = (uint16_t)i};
        NSUInteger n = shader_arg_len(o); printf("    %s: %lu bytes\n", argv[1], (unsigned long)n);
        return n == 32 ? 0 : 3;
    }
    int a = child(argv[0], "none", 0), b = child(argv[0], "real", 1), c = child(argv[0], "none", 1);
    CHECK(a == 0, "a function Metal gives no argument buffer, reflection ids 0..3: 32 bytes, Metal not asked, the load goes on (child: %d) [-DDM_TEST_ARGLEN_FROM_METAL]", a);
    CHECK(b == 0, "DM_NVN_ARGCHECK=1, a real argument buffer of four ids: Metal agrees (child: %d)", b);
    CHECK(c == 128 + SIGABRT, "DM_NVN_ARGCHECK=1, the function without one: stopped (child: %d)", c);
    printf("t_arglen: %d checks, %d failed\n", count, fail); return fail != 0;
} }
