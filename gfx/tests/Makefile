# Graphics unit tests, seconds: make run (make run-m3 for the older-API build)
# Sources by wildcard, as GFX in host/Makefile (hle_nvn.c is #included by t_nvn.c, so it is left out); the host's crash.c is linked in so the
# write-protect tests use the production fault hook
CFLAGS=-O1 -g -Wall -Wno-unused-function -I../nvn/include -I../../host   # (the host's headers, for its sources the tests link: runtime/crash.c, plat_apple.c, hstack.c)
GFX=$(filter-out ../nvn/hle_nvn.c,$(sort $(wildcard ../nvn/*.c ../nvn/*.m ../nvn/*.S ../nvn/mtl/*.m ../nvn/core/*.c ../tex/*.c ../tex/*.m)))
GFX_H=$(wildcard ../nvn/*.h ../nvn/*.inc ../nvn/mtl/*.inc ../nvn/mtl/*.h ../nvn/hle/*.inc ../nvn/core/*.h ../nvn/include/*.h ../tex/*.h)
FW=-framework Metal -framework MetalFX -framework Foundation -framework QuartzCore -framework ImageIO -framework CoreGraphics -framework UniformTypeIdentifiers
# HSTACK: the NVN trampolines end in the host stack switch (host/runtime/hstack.S)
HSTACK=../../host/runtime/hstack.S ../../host/runtime/hstack.c
CRASH=../../host/runtime/crash.c ../../host/runtime/plat_apple.c   # the host's crash handler, with the platform layer it reads memory through (host/plat.h)
t_nvn: t_nvn.c ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_nvn.c $(CRASH) $(HSTACK) $(GFX) $(FW)
t_dirty_slots: t_dirty_slots.c ../nvn/nvn_dirty.c $(CRASH) ../nvn/nvn_dirty.h
	clang $(CFLAGS) -o $@ t_dirty_slots.c $(CRASH) ../nvn/nvn_dirty.c
t_dirty_shared: t_dirty_shared.c ../nvn/nvn_dirty.c $(CRASH) ../nvn/nvn_dirty.h
	clang $(CFLAGS) -o $@ t_dirty_shared.c $(CRASH) ../nvn/nvn_dirty.c
t_dirty_race: t_dirty_race.c ../nvn/nvn_dirty.c $(CRASH) ../nvn/nvn_dirty.h
	clang $(CFLAGS) -o $@ t_dirty_race.c $(CRASH)
t_dirty_stamp: t_dirty_stamp.c ../nvn/nvn_dirty.c $(CRASH) ../nvn/nvn_dirty.h
	clang $(CFLAGS) -o $@ t_dirty_stamp.c $(CRASH)
t_deswz_gpu: t_deswz_gpu.m ../tex/nvn_deswizzle.c ../tex/nvn_deswizzle_gpu.m ../tex/nvn_deswizzle.h ../tex/nvn_deswizzle_gpu.h ../nvn/nvn_tex.c ../nvn/nvn_tex.h
	clang $(CFLAGS) -fobjc-arc -o $@ t_deswz_gpu.m ../tex/nvn_deswizzle.c ../tex/nvn_deswizzle_gpu.m ../nvn/nvn_tex.c -framework Metal -framework Foundation
t_tex_auto: t_tex_auto.c ../nvn/nvn_tex.c ../nvn/nvn_tex.h ../tex/nvn_deswizzle.c ../tex/nvn_deswizzle.h
	clang -O1 -Wall -o $@ t_tex_auto.c ../nvn/nvn_tex.c ../tex/nvn_deswizzle.c
t_d16clear: t_d16clear.m ../nvn/nvn_depth_clear.h
	clang $(CFLAGS) -fobjc-arc -o $@ t_d16clear.m -framework Metal -framework Foundation
# gfx/nvn/core: the replay's decisions in plain C (plan item 264), each with a test that needs no GPU. Every test links all the modules, the
# backend's hooks as weak defaults it overrides where it needs to (core_fakes.c) and the host's crash handler; its checks: core_test.h
CORE_H=$(wildcard ../nvn/core/*.h)
CORE_C=$(sort $(wildcard ../nvn/core/*.c)) core_fakes.c $(CRASH)
CORE_DEP=$(CORE_C) $(CORE_H) core_test.h ../nvn/nvn_obj.h ../nvn/nvn_backend.h ../nvn/nvn_refl.h ../../host/dm.h
t_core_capture: t_core_capture.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_capture.c $(CORE_C)
t_core_cond: t_core_cond.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_cond.c $(CORE_C)
t_core_defer: t_core_defer.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_defer.c $(CORE_C)
t_core_exec: t_core_exec.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_exec.c $(CORE_C)
t_core_frame: t_core_frame.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_frame.c $(CORE_C)
t_core_fuse: t_core_fuse.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_fuse.c $(CORE_C)
t_core_live: t_core_live.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_live.c $(CORE_C)
t_core_pass: t_core_pass.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_pass.c $(CORE_C)
# The null backend (gfx/nvn/null, the Android host's until Vulkan: plan item 301c), on its own
t_null_be: t_null_be.c ../nvn/null/null.c core_test.h $(GFX_H)
	clang $(CFLAGS) -Werror -o $@ t_null_be.c ../nvn/null/null.c
t_null_be_early: t_null_be.c ../nvn/null/null.c core_test.h $(GFX_H)
	clang $(CFLAGS) -Werror -DDM_TEST_NULL_EARLY -o $@ t_null_be.c ../nvn/null/null.c
t_core_pend: t_core_pend.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_pend.c $(CORE_C)
t_core_prog: t_core_prog.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_prog.c $(CORE_C)
t_core_rt: t_core_rt.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_rt.c $(CORE_C)
t_core_state: t_core_state.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_state.c $(CORE_C)
t_core_store_index: t_core_store_index.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_store_index.c $(CORE_C)
t_core_tbdr: t_core_tbdr.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_tbdr.c $(CORE_C)
t_core_upd: t_core_upd.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_upd.c $(CORE_C)
t_core_usnap: t_core_usnap.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -o $@ t_core_usnap.c $(CORE_C)
# Its negative controls for plan item 301e3 (an earlier submission's renamed updates laid over the snapshot)
t_core_usnap_allknown: t_core_usnap.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -DDM_TEST_US_GW_ALL_KNOWN -o $@ t_core_usnap.c $(CORE_C)
t_core_usnap_nolay: t_core_usnap.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -DDM_TEST_US_NO_LAY -o $@ t_core_usnap.c $(CORE_C)
t_core_usnap_laydone: t_core_usnap.c $(CORE_DEP)
	clang $(CFLAGS) -Werror -DDM_TEST_PEND_LAY_DONE -o $@ t_core_usnap.c $(CORE_C)
t_hmap: t_hmap.c ../nvn/nvn_hmap.h
	clang -O1 -Wall -o $@ t_hmap.c
t_cover: t_cover.c ../nvn/nvn_cover.h
	clang -O1 -Wall -o $@ t_cover.c
t_pace: t_pace.c ../nvn/nvn_pace.h
	clang -O1 -Wall -o $@ t_pace.c
t_ts: t_ts.c ../nvn/nvn_ts.h
	clang -O1 -Wall -o $@ t_ts.c
t_fstep: t_fstep.c ../nvn/nvn_fstep.h
	clang -O1 -Wall -o $@ t_fstep.c
t_fptrial: t_fptrial.c ../nvn/nvn_fptrial.h
	clang -O1 -Wall -o $@ t_fptrial.c
t_copy_pitch: t_copy_pitch.c ../nvn/nvn_copy_pitch.h
	clang -O1 -Wall -Werror -o $@ t_copy_pitch.c
t_pool_find: t_pool_find.c ../nvn/nvn_pool_find.h
	clang -O1 -Wall -Werror -o $@ t_pool_find.c
t_warm_list: t_warm_list.c ../nvn/nvn_warm_list.h
	clang -O1 -Wall -Werror -o $@ t_warm_list.c
t_retry: t_retry.m ../nvn/nvn_retry.h
	clang -O1 -Wall -fobjc-arc -o $@ t_retry.m -framework Foundation
t_fpcontract: t_fpcontract.m
	clang -O1 -Wall -fobjc-arc -o $@ t_fpcontract.m -framework Metal -framework Foundation
t_aniso: t_aniso.c ../nvn/nvn_aniso.h
	clang -O1 -Wall -Werror -o $@ t_aniso.c
t_perfline: t_perfline.c ../nvn/nvn_perfline.h
	clang -O1 -Wall -Werror -o $@ t_perfline.c
t_m3buf: t_m3buf.c ../nvn/nvn_m3buf.h ../nvn/nvn_pool_find.h
	clang -O1 -Wall -Werror -o $@ t_m3buf.c
t_cull_prim: t_cull_prim.c ../nvn/nvn_obj.h
	clang $(CFLAGS) -Werror -o $@ t_cull_prim.c
t_attless: t_attless.c ../nvn/nvn_obj.h
	clang $(CFLAGS) -Werror -o $@ t_attless.c
t_sync_order: t_sync_order.c ../nvn/nvn_obj.h
	clang -O1 -Wall -Werror -I../nvn/include -o $@ t_sync_order.c
t_tex_fit: t_tex_fit.m ../nvn/nvn_tex_fit.h
	clang $(CFLAGS) -fobjc-arc -o $@ t_tex_fit.m -framework Metal -framework Foundation
t_fuse_check: t_fuse_check.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_fuse_check.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_rt_srgb_view: t_rt_srgb_view.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_rt_srgb_view.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_present_fx: t_present_fx.m ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_present_fx.m $(CRASH) $(HSTACK) $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_present_crop: t_present_crop.m ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_present_crop.m $(CRASH) $(HSTACK) $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_reorder_chain: t_reorder_chain.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_reorder_chain.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_defer_chain: t_defer_chain.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_defer_chain.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_zskip: t_zskip.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_zskip.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_xv: t_xv.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_xv.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_ub: t_ub.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_ub.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_drvseg: t_drvseg.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_drvseg.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_restart: t_restart.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_restart.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_live: t_live.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_live.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_wb: t_wb.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_wb.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_pend: t_pend.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_pend.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_keepalive: t_keepalive.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_keepalive.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_chunk: t_chunk.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_chunk.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_esfilter: t_esfilter.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_esfilter.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_vbelide: t_vbelide.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_vbelide.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_sbufpast: t_sbufpast.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_sbufpast.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_psouser: t_psouser.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_psouser.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_uppool: t_uppool.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_uppool.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_upbound: t_upbound.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_upbound.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_retire: t_retire.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_retire.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_ppred: t_ppred.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_ppred.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_present_res: t_present_res.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_present_res.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_bc1astc: t_bc1astc.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_bc1astc.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_clearquad: t_clearquad.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_clearquad.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_bcd: t_bcd.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_bcd.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_fmt_swz: t_fmt_swz.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_fmt_swz.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_writetexels: t_writetexels.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_writetexels.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_flushtexels: t_flushtexels.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_flushtexels.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_vfetch: t_vfetch.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_vfetch.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_cubearr: t_cubearr.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_cubearr.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_libcopy: t_libcopy.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_libcopy.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_findrain: t_findrain.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_findrain.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_texcheck: t_texcheck.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_texcheck.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_trace: t_trace.c ../nvn/nvn_trace.c ../nvn/nvn_trace.h
	clang $(CFLAGS) -o $@ t_trace.c ../nvn/nvn_trace.c
t_nvn64a: t_nvn64a.m ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_nvn64a.m $(CRASH) $(HSTACK) $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_copycmd: t_copycmd.m ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_copycmd.m $(CRASH) $(HSTACK) $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_dbounds: t_dbounds.m ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_dbounds.m $(CRASH) $(HSTACK) $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_nvn64d: t_nvn64d.m ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_nvn64d.m $(CRASH) $(HSTACK) $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_pools: t_pools.m ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_pools.m $(CRASH) $(HSTACK) $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_blkunp: t_blkunp.m ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_blkunp.m $(CRASH) $(HSTACK) $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_fxp: t_fxp.m ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_fxp.m $(CRASH) $(HSTACK) $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_xlat: t_xlat.m ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_xlat.m $(CRASH) $(HSTACK) $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_mtlguard: t_mtlguard.m ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_mtlguard.m $(CRASH) $(HSTACK) $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_bindfreeze: t_bindfreeze.m ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_bindfreeze.m $(CRASH) $(HSTACK) $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_rqueue: t_rqueue.c ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_rqueue.c $(CRASH) $(HSTACK) $(GFX) $(FW)
t_rt_continue: t_rt_continue.m ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_rt_continue.m $(CRASH) $(HSTACK) $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_upsoak: t_upsoak.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_upsoak.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_ubren: t_ubren.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_ubren.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_linkfifo: t_linkfifo.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_linkfifo.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_rtheap: t_rtheap.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_rtheap.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_present_vp: t_present_vp.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_present_vp.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_feedback_pool: t_feedback_pool.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_feedback_pool.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_border: t_border.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_border.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_fout: t_fout.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_fout.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_pluma: t_pluma.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_pluma.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_pairs: t_pairs.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_pairs.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_alpha: t_alpha.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_alpha.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_ussort: t_ussort.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_ussort.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_rstream: t_rstream.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_rstream.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_layer: t_layer.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_layer.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_stor: t_stor.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_stor.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_poolcheck: t_poolcheck.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_poolcheck.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_respool: t_respool.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_respool.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_snapreap: t_snapreap.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_snapreap.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_usnap: t_usnap.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_usnap.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_fbx: t_fbx.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_fbx.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_shmap: t_shmap.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_shmap.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_cmdalloc: t_cmdalloc.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_cmdalloc.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_extent: t_extent.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_extent.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_passgpu: t_passgpu.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_passgpu.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_visibility: t_visibility.m ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_visibility.m $(CRASH) $(HSTACK) $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_mip_extent: t_mip_extent.m ../nvn/hle_nvn.c $(CRASH) $(HSTACK) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_mip_extent.m $(CRASH) $(HSTACK) $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_view_levels: t_view_levels.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_view_levels.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_pso_skey: t_pso_skey.m ../nvn/nvn_pso_skey.h ../nvn/nvn_obj.h
	clang $(CFLAGS) -fobjc-arc -o $@ t_pso_skey.m -framework Foundation
t_pso_canon: t_pso_canon.m ../nvn/nvn_pso_skey.h ../nvn/nvn_obj.h
	clang $(CFLAGS) -fobjc-arc -o $@ t_pso_canon.m -framework Foundation
t_refl: t_refl.m ../nvn/nvn_refl.m ../nvn/nvn_refl.h
	clang $(CFLAGS) -fobjc-arc -o $@ t_refl.m ../nvn/nvn_refl.m -framework Metal -framework Foundation
t_gputime: t_gputime.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_gputime.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_vfmt: t_vfmt.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_vfmt.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_upwait: t_upwait.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_upwait.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_poolcache: t_poolcache.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_poolcache.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_samp: t_samp.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_samp.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_pfv: t_pfv.m $(CRASH) $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_pfv.m $(CRASH) $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
run: \
	t_alpha \
	t_aniso \
	t_attless \
	t_border \
	t_chunk \
	t_copy_pitch \
	t_core_capture \
	t_core_cond \
	t_core_defer \
	t_core_exec \
	t_core_frame \
	t_core_fuse \
	t_core_live \
	t_core_pend \
	t_core_pass \
	t_core_prog \
	t_core_rt \
	t_core_state \
	t_core_store_index \
	t_core_tbdr \
	t_core_upd \
	t_core_usnap \
	t_core_usnap_allknown \
	t_core_usnap_nolay \
	t_core_usnap_laydone \
	t_cover \
	t_cull_prim \
	t_d16clear \
	t_defer_chain \
	t_dirty_shared \
	t_drvseg \
	t_cubearr \
	t_deswz_gpu \
	t_dirty_race \
	t_dirty_stamp \
	t_dirty_slots \
	t_esfilter \
	t_vbelide \
	t_sbufpast \
	t_psouser \
	t_extent \
	t_findrain \
	t_fmt_swz \
	t_fpcontract \
	t_feedback_pool \
	t_fout \
	t_fbx \
	t_shmap \
	t_cmdalloc \
	t_upsoak \
	t_rt_continue \
	t_rqueue \
	t_bindfreeze \
	t_trace \
	t_bc1astc \
	t_bcd \
	t_clearquad \
	t_nvn64a \
	t_copycmd \
	t_dbounds \
	t_nvn64d \
	t_pools \
	t_blkunp \
	t_fxp \
	t_xlat \
	t_mtlguard \
	t_fuse_check \
	t_gputime \
	t_hmap \
	t_keepalive \
	t_layer \
	t_libcopy \
	t_linkfifo \
	t_live \
	t_m3buf \
	t_mip_extent \
	t_null_be \
	t_null_be_early \
	t_nvn \
	t_pace \
	t_pairs \
	t_pluma \
	t_present_vp \
	t_rtheap \
	t_passgpu \
	t_ts \
	t_fstep \
	t_fptrial \
	t_pend \
	t_perfline \
	t_pfv \
	t_pool_find \
	t_poolcache \
	t_poolcheck \
	t_respool \
	t_ppred \
	t_present_crop \
	t_present_fx \
	t_present_res \
	t_pso_canon \
	t_pso_skey \
	t_refl \
	t_restart \
	t_retry \
	t_rstream \
	t_retire \
	t_reorder_chain \
	t_view_levels \
	t_rt_srgb_view \
	t_samp \
	t_snapreap \
	t_stor \
	t_sync_order \
	t_tex_auto \
	t_tex_fit \
	t_texcheck \
	t_ub \
	t_ubren \
	t_uppool \
	t_upbound \
	t_upwait \
	t_usnap \
	t_vfetch \
	t_vfmt \
	t_ussort \
	t_visibility \
	t_warm_list \
	t_wb \
	t_writetexels \
	t_flushtexels \
	t_xv \
	t_zskip
	./t_nvn
	./t_dirty_slots
	./t_dirty_race
	./t_dirty_stamp
	./t_dirty_shared
	./t_deswz_gpu
	./t_pace
	./t_ts
	./t_fstep
	./t_fptrial
	./t_hmap
	./t_core_capture
	./t_core_cond
	./t_core_defer
	./t_core_exec
	./t_core_frame
	./t_core_fuse
	./t_core_live
	./t_core_pend
	./t_core_pass
	./t_core_prog
	./t_core_rt
	./t_core_state
	./t_core_store_index
	./t_core_tbdr
	./t_core_upd
	./t_core_usnap
	@status=0; ./t_core_usnap_allknown > t_core_usnap_allknown.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' t_core_usnap_allknown.log)" -ne 2 ]; then \
	   cat t_core_usnap_allknown.log; echo "t_core_usnap_allknown: unexpected negative-control result ($$status)"; exit 1; \
	 else echo 't_core_usnap_allknown: every GPU write taken as renamed updates fails 2, as expected'; fi
	@status=0; ./t_core_usnap_nolay > t_core_usnap_nolay.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' t_core_usnap_nolay.log)" -ne 2 ]; then \
	   cat t_core_usnap_nolay.log; echo "t_core_usnap_nolay: unexpected negative-control result ($$status)"; exit 1; \
	 else echo 't_core_usnap_nolay: the updates not laid over the copy fail 2, as expected'; fi
	@status=0; ./t_core_usnap_laydone > t_core_usnap_laydone.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' t_core_usnap_laydone.log)" -ne 1 ]; then \
	   cat t_core_usnap_laydone.log; echo "t_core_usnap_laydone: unexpected negative-control result ($$status)"; exit 1; \
	 else echo 't_core_usnap_laydone: completed writes laid too fail 1, as expected'; fi
	./t_cover
	./t_copy_pitch
	./t_pool_find
	./t_warm_list
	./t_sync_order
	./t_aniso
	./t_perfline
	./t_m3buf
	./t_cull_prim
	./t_attless
	./t_poolcache
	./t_poolcheck
	./t_respool
	./t_tex_auto
	./t_d16clear
	./t_fuse_check
	./t_gputime
	./t_present_crop
	MTL_DEBUG_LAYER=1 ./t_present_fx
	MTL_DEBUG_LAYER=1 ./t_reorder_chain
	MTL_DEBUG_LAYER=1 ./t_defer_chain
	./t_zskip
	./t_xv
	./t_ub
	MTL_DEBUG_LAYER=1 ./t_usnap
	./t_ussort
	MTL_DEBUG_LAYER=1 ./t_visibility
	DM_NVN_UB_RENAME=0 ./t_nvn > t_nvn_norename.log 2>&1 && tail -1 t_nvn_norename.log || { grep "✗" t_nvn_norename.log; tail -1 t_nvn_norename.log; exit 1; }   # uniform updates copied on the GPU timeline (plan item 179)
	MTL_DEBUG_LAYER=1 ./t_fbx
	DM_NVN_FBX=all MTL_DEBUG_LAYER=1 ./t_fbx
	DM_NVN_FBX=split MTL_DEBUG_LAYER=1 ./t_fbx
	@if echo "$(CFLAGS)" | grep -q DM_NVN_M3; then DM_TEST_NO_RESIDENCY_SET=1 MTL_DEBUG_LAYER=1 ./t_fbx > t_fbx_noset.log 2>&1; \
	  if ! grep -q "no residency set: " t_fbx_noset.log || ! grep -q "26 checks, 0 failed" t_fbx_noset.log; then cat t_fbx_noset.log; echo "t_fbx without a residency set (plan item 176): failed"; exit 1; fi; \
	  echo "t_fbx without a residency set (a virtual GPU's path, plan item 176): 26 checks, 0 failed"; fi
	MTL_DEBUG_LAYER=1 ./t_extent
	TMPDIR=. MTL_DEBUG_LAYER=1 ./t_passgpu
	TMPDIR=. MTL_DEBUG_LAYER=1 ./t_passgpu --every
	TMPDIR=. MTL_DEBUG_LAYER=1 ./t_pairs
	TMPDIR=. MTL_DEBUG_LAYER=1 ./t_pluma
	./t_present_vp
	./t_feedback_pool
	MTL_DEBUG_LAYER=1 ./t_border
	TMPDIR=. ./t_shmap
	TMPDIR=. ./t_cmdalloc
	./t_upsoak
	MTL_DEBUG_LAYER=1 ./t_rt_continue
	DM_NVN_TBDR=0 MTL_DEBUG_LAYER=1 ./t_rt_continue   # the TBDR rewrites off, Tears of the Kingdom's Mac default: every case two passes (plan item 179)
	./t_rqueue
	./t_bindfreeze
	DM_NVN_UBO_SNAPSHOT=0 ./t_bindfreeze   # the walk for deferred handles alone (plan item 61b)
	TMPDIR=. ./t_trace
	MTL_DEBUG_LAYER=1 ./t_bc1astc
	./t_bc1astc off   # DM_NVN_BC1_ASTC=0: BC1 decompressed as before (plan item 62c)
	MTL_DEBUG_LAYER=1 ./t_bcd
	MTL_DEBUG_LAYER=1 ./t_clearquad
	./t_nvn64a
	./t_copycmd
	MTL_DEBUG_LAYER=1 ./t_dbounds
	MTL_DEBUG_LAYER=1 ./t_nvn64d
	./t_pools
	MTL_DEBUG_LAYER=1 ./t_xlat
	MTL_DEBUG_LAYER=1 ./t_blkunp
	MTL_DEBUG_LAYER=1 ./t_fxp
	DM_NVN_XLAT=1 ./t_rqueue   # the render-queue cases with the translation thread (plan item 61)
	DM_NVN_XLAT=1 ./t_nvn > t_nvn_xlat.log 2>&1 && tail -1 t_nvn_xlat.log || { grep "✗" t_nvn_xlat.log; tail -1 t_nvn_xlat.log; exit 1; }
	MTL_DEBUG_LAYER=1 ./t_mtlguard
	MTL_DEBUG_LAYER=1 ./t_flushtexels
	MTL_DEBUG_LAYER=1 ./t_fout
	MTL_DEBUG_LAYER=1 ./t_rtheap
	./t_linkfifo
	./t_null_be
	@status=0; ./t_null_be_early > t_null_be_early.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' t_null_be_early.log)" -ne 4 ]; then \
	   cat t_null_be_early.log; echo "t_null_be_early: unexpected negative-control result ($$status)"; exit 1; \
	 else echo 't_null_be_early: completion writes landing at replay fail 4, as expected'; fi
	python3 -I t_alpha_src.py 1 > t_alpha_1.metal && python3 -I t_alpha_src.py 0 > t_alpha_0.metal && ./t_alpha t_alpha_1.metal t_alpha_0.metal
	DM_ROOT=$${DM_ROOT:-$(HOME)/dm-work} python3 -I t_xlate_ops.py
	DM_ROOT=$${DM_ROOT:-$(HOME)/dm-work} python3 -I t_early_z.py
	@status=0; DM_TEST_EARLY_Z_OFF=1 DM_ROOT=$${DM_ROOT:-$(HOME)/dm-work} python3 -I t_early_z.py > t_early_z_off.log 2>&1 || status=$$?; \
	 if grep -q 'skipped' t_early_z_off.log; then echo 't_early_z_off: skipped (no translator)'; elif [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' t_early_z_off.log)" -ne 1 ]; then \
	   cat t_early_z_off.log; echo "t_early_z_off: unexpected negative-control result ($$status)"; exit 1; \
	 else echo 't_early_z_off: without the field no early fragment tests, as expected'; fi
	MTL_DEBUG_LAYER=1 ./t_vfmt
	DM_NVN_RENDER_STREAM=1 MTL_DEBUG_LAYER=1 ./t_rstream
	DM_NVN_RENDER_STREAM=0 ./t_rstream --off
	MTL_DEBUG_LAYER=1 ./t_layer
	MTL_DEBUG_LAYER=1 ./t_stor
	DM_NVN_LAYER_MERGE=0 ./t_layer --off
	./t_retire
	./t_snapreap
	./t_findrain
	MTL_DEBUG_LAYER=1 ./t_texcheck
	MTL_DEBUG_LAYER=1 ./t_ubren
	DM_NVN_UB_RENAME=0 ./t_ubren --expect-split
	./t_live
	./t_wb
	DM_NVN_WB_IDLE=1 ./t_wb --idle
	./t_pend
	./t_ppred
	MTL_DEBUG_LAYER=1 ./t_present_res
	./t_keepalive
	MTL_DEBUG_LAYER=1 ./t_chunk
	./t_chunk --no-chunks
	MTL_DEBUG_LAYER=1 ./t_chunk --tbdr-nomerge
	MTL_DEBUG_LAYER=1 ./t_chunk --tbdr-all
	MTL_DEBUG_LAYER=1 ./t_esfilter
	MTL_DEBUG_LAYER=1 ./t_vbelide
	MTL_DEBUG_LAYER=1 ./t_sbufpast
	./t_psouser
	./t_uppool
	./t_upbound
	./t_upwait
	MTL_DEBUG_LAYER=1 ./t_restart
	MTL_DEBUG_LAYER=1 ./t_drvseg
	MTL_DEBUG_LAYER=1 ./t_mip_extent
	MTL_DEBUG_LAYER=1 ./t_mip_extent --stencil-only
	DM_NVN_UNWRITTEN_DC=0 MTL_DEBUG_LAYER=1 ./t_view_levels
	MTL_DEBUG_LAYER=1 ./t_view_levels   # DontCare on by default: the expected attachment bytes save one depth-stencil store per pass
	DM_NVN_UNWRITTEN_DC=0 MTL_DEBUG_LAYER=1 ./t_view_levels --small-depth
	DM_NVN_UNWRITTEN_DC=0 MTL_DEBUG_LAYER=1 ./t_view_levels --sample-attachment
	./t_pso_skey
	./t_pso_canon
	MTL_DEBUG_LAYER=1 ./t_samp
	MTL_DEBUG_LAYER=1 ./t_fmt_swz
	MTL_DEBUG_LAYER=1 ./t_vfetch
	./t_writetexels 2>/dev/null
	./t_cubearr 2>/dev/null
	DM_NVN_LIB_COPY=1 ./t_libcopy
	./t_retry 2>/dev/null
	python3 -I ../tools/msl_helpers.py t_fpcontract.metal > t_fpcontract_inl.metal && ./t_fpcontract t_fpcontract.metal t_fpcontract_inl.metal
	./t_refl
	MTL_DEBUG_LAYER=1 ./t_pfv
	MTL_DEBUG_LAYER=1 ./t_rt_srgb_view
	MTL_DEBUG_LAYER=1 ./t_tex_fit
.PHONY: run
# The older-API build (DM_NVN_M3=1, macOS 15.0): the same tests built and run in a sibling tests_m3 directory
run-m3:
	mkdir -p ../tests_m3 && rsync -a Makefile *.c *.h *.m *.py t_fpcontract.metal ../tests_m3/   # with their times: make rebuilds only what changed
	$(MAKE) -C ../tests_m3 run CFLAGS="$(CFLAGS) -DDM_NVN_M3=1 -mmacosx-version-min=15.0"
