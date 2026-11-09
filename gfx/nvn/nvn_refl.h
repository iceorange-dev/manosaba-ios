// Shader reflection json → fixed-length C arrays (A: PC chain reflection; B: reflection produced by translating the original Switch shaders, route = "B", output of tools/dmsh_build.py).
// Kinds added by B: SBUFP = storage buffer device pointer (NVN storage buffer slot); SUPPORT = Ryujinx auxiliary constants; C1 = the shader's own constant segment (c[1]);
//            DRV0 / DRV2 = NVN driver constant segments c[0] (storage buffer address table) / c[2] (texture and sampler handle table)
// PC shader reflection json → fixed-length C arrays (converted once when the shader is fetched; each draw only walks the arrays and does not touch NSDictionary / NSString).
// Unit test tests/t_refl.m computes the expectations by hand from real reflection.
#pragma once
#include <stdint.h>

enum { NVN_RB_CBUF = 1, NVN_RB_SAMP, NVN_RB_TEX, NVN_RB_IMG, NVN_RB_TBUF,
       NVN_RB_SBUFP, NVN_RB_SUPPORT, NVN_RB_C1, NVN_RB_DRV0, NVN_RB_DRV2, NVN_RB_SAMPT, NVN_RB_UBOP, NVN_RB_SBUFU };   // SBUFU: storage buffer whose address (and size) the shader reads from uniform slot r at byte off   // UBOP: device pointer of compute-stage uniform slot r (the Maxwell compute stage reads uniforms via the c0 address table + global memory)   // SAMPT: the sampler is taken from the combined handle of texture slot r (high bits = sampler ID)   // Binding kind (type in the json); IMG = writable texture (D3D u_r, NVN image slot r); TBUF = typed buffer (D3D t_r Buffer<T>, buffer texture in NVN texture slot r)
enum { NVN_DPF_FLOAT = 0, NVN_DPF_UINT, NVN_DPF_SINT, NVN_DPF_DEPTH };            // Dummy texture pixel class (determined by msl_type)
enum { NVN_CT_FLOAT = 0, NVN_CT_UINT, NVN_CT_INT };                               // Vertex input component type
#define NVN_REFL_MAXB 64
#define NVN_REFL_MAXV 32

typedef struct {
    uint8_t kind, rw;        // rw: storage buffer access is read_write / write / device
    uint8_t dtt, dpf;        // Dummy texture when the texture slot is unbound: MTLTextureType value, pixel class
    uint8_t decl_tt;         // The texture type the shader declares (MTLTextureType, multisample included): the type check at binding (nvn_mtl.m tex_fit)
    uint16_t reg, aid;       // D3D register number, index within the argument buffer
    uint16_t off;            // SBUFU: byte offset of the address in the uniform buffer
    uint8_t six;             // TEX / IMG: Ryujinx's resolution-scale index + 1 (render_scale[six] / render_scale_y[six]; 0: none; plan item 320f)
} nvn_rbind;
typedef struct {
    int nb, abi, nv;         // Binding count, argument buffer slot, vertex input count
    nvn_rbind b[NVN_REFL_MAXB];
    struct { uint8_t loc, ct, nvn; } v[NVN_REFL_MAXV];   // loc: Metal vertex attribute slot (DXBC input register); nvn: NVN vertex attribute number (the n of HLSL semantic ATTRn)
    uint16_t tg[3];          // Compute thread group size (none = 0)
    uint8_t route_b, has_c1; // Route B; has a c1 constant segment
    uint8_t frag_coord;      // B: reads the fragment coordinate (the reflection's frag_coord; plan item 320f)
    uint8_t ubo_addr;        // B: has an SBUFU binding (its argument depends on uniform buffer contents: argument blocks are not reused)
    uint8_t drv_read[3];     // B: reads driver segments c0 / c2 at runtime (indices 0, 2; gathered statically from the MSL at packaging time, tools/dmsh_build.py drv_reads)
    uint16_t c1_len;         // Length of the c1 constant segment (B; 0 / 256 / 512 / 768 across the whole library)
    // For the fully-covering-first-draw proof (dmsh_build.py raster_facts; missing field = not provable): vertex - position xyz taken as-is from attribute pos_attr (-1 = no), w is the constant 1 (pos_w1) or taken from the same attribute's .w;
    // fragment - may discard, writes sample_mask, bitmap of the color numbers written
    int8_t pos_attr; uint8_t pos_w1, may_discard, writes_smask, color_mask, color_known;   // color_known: the reflection lists the colors (color_mask can be trusted)
    // Merge B step 2 (readable-tile version <key>_bf, dmsh_fetch.py): fragment - nfc redirectable samples (the i-th: texture slot fc_reg, interpolated output fc_locn),
    // bitmap fc_mixed of texture slots with mixed use (these slots must not be redirected to read the tile), nfo outputs (each with color number fo_color); vertex - .xy of interpolated output locn is taken as-is from attribute uv_attr[locn] (-1 = no)
    uint8_t nfc, fc_reg[32], fc_locn[32], nfo, fo_color[4]; uint64_t fc_mixed[2]; int8_t uv_attr[32];
    // Vertex - .x / .y of interpolated output locn = fma(.x / .y of attribute uv_aff_attr[locn], scale, offset), where scale / offset reference a uniform: (slot << 8) | (vector << 2) | component
    int8_t uv_aff_attr[32]; uint16_t uv_aff[32][4];   // [sx, ox, sy, oy]
    // Fragment - bit i: fetch call i samples at in.m.xy * (1 / in.m.w) of its input ("proj"); vertex - bit L: output locn L is ((w + x) / 2, (w - y) / 2, -, w) of
    // the final position (dmsh_fetch.py screen_locn). Together with a viewport over the whole target: the call reads this pixel's texel
    uint32_t fc_proj, scr_locn;
    uint8_t c1[4096];        // c1 constant segment as-is (B)
} nvn_refl;

#ifdef __OBJC__
#import <Foundation/Foundation.h>
// 0 on success; on failure returns non-0 and writes the reason into err (unknown binding type, fixed-length limit exceeded, missing field)
int nvn_refl_from_json(NSDictionary *j, nvn_refl *out, char *err, int errlen);
#endif
