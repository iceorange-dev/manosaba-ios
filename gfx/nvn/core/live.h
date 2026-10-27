// gfx/nvn/core/live.h: which texture lives at a storage address, and the host copies of texture objects that recorded commands use (plan item
// 264 moved them here from the Metal backend). Checked without a GPU by gfx/tests/t_core_live.c; with the backend by t_live.m, t_snapreap.m, t_nvn
#pragma once
#include <stdatomic.h>
#include <stdint.h>
#include "../nvn_obj.h"
#include "core.h"

// ---------- The live texture at a storage address (MacNativeGames' live_put / nvn_tex_live_at, MIT; written anew here) ----------
// A game may finalize a texture and initialize a new one at the same storage every frame without registering its texture ID again (Tears of the
// Kingdom's transient G-buffer targets); on the console the descriptor names the memory, so the ID reads the new texture. Here the ID kept the
// finalized texture: its old content for FIN_GRACE submissions, then a blank one created again (56 "created again" red lines in play). tex_by_id
// now follows a registration whose texture was finalized to the texture living at its storage, when that one has the same shape (t_live).
// g_live: storage address → copy of the texture last initialized there and not finalized; g_dead: finalized serials not released yet; g_fin_gen
// counts finalizations (tex_by_id looks again only after one)
extern _Atomic uint64_t g_fin_gen, g_live_follows;   // finalizations so far; registrations a lookup followed to the texture living at their storage
void mtl_tex_live(const tex_t *t);   // hle_nvn.c nvnTextureInitialize: the texture now living at its storage
void live_finalized(const tex_t *t); // nvnTextureFinalize (mtl_tex_release)
void live_released(uint64_t serial); // its release (the backend's fin_drain)
int live_follow(const tex_t *reg, int released, tex_t *out);   // The texture to read for a registration of reg, when reg was finalized and replaced (1: *out)

// Host copies of texture objects for recorded commands, one per serial (hle_nvn.c records them). A command (render targets, copies, clears) records
// which texture it uses; NVN puts what a command needs into command memory when it is recorded, so the game may finalize or reuse its NVNtexture
// before the command is submitted. Recording the game's pointer let replay read whatever was there by then (a garbage format at a scene load, a
// driver crash in a copy encoder). Commands point at this copy instead; it is freed SNAP_KEEP submissions after the texture's release (fin_drain),
// long after any command recorded before the finalize has been submitted
enum { SNAP_KEEP = 600 };   // about 10 s at 60 fps
const tex_t *mtl_tex_snap(const tex_t *t);         // The host copy of a texture object (by serial)
void tex_snap_retire(uint64_t serial, uint64_t due); // The texture was released: its copy is freed at submission index due
void tex_snap_reap(uint64_t now);                   // Free the copies due by submission index now
uint64_t mtl_tex_snap_count(void);
int tex_snap_copy(tex_t *out, int cap);             // The live copies into out (at most cap) for a census; how many there are
