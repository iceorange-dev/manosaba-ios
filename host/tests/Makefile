# Host unit tests, seconds: make -C host/tests run (build output in build/, not committed)
B = build
GAME ?= manosaba
GEN = ../gen/$(GAME)/game_profile.c
# One per line, alphabetical
run: \
	$(B)/t_altstack \
	$(B)/t_arbiter \
	$(B)/t_audio \
	$(B)/t_audio_spsc \
	$(B)/t_audio_extras \
	$(B)/t_audio_extras_early \
	$(B)/t_audio_extras_vol \
	$(B)/t_audio_extras_kept \
	$(B)/t_audio_extras_stopkeeps \
	$(B)/t_audren \
	$(B)/t_audren_void \
	$(B)/t_audren_unbounded \
	$(B)/t_audren_pace \
	$(B)/t_audren_pace_out \
	$(B)/t_audren_pace_sink \
	$(B)/t_cloudlist \
	$(B)/t_cloudlist_old \
	$(B)/t_cloudsync \
	$(B)/t_cmdchan \
	$(B)/t_cmdchan_partial \
	$(B)/t_codesig \
	$(B)/t_codesig_broken \
	$(B)/t_cmdchan_noreopen \
	$(B)/t_condvar \
	$(B)/t_condvar_old \
	$(B)/t_waitsync \
	$(B)/t_coremask \
	$(B)/t_coremask_noproc \
	$(B)/t_thrpub \
	$(B)/t_thrpub_early \
	$(B)/t_thrpub_prio \
	$(B)/t_mainqos \
	$(B)/t_mainqos_ignored \
	$(B)/t_crash_bt \
	$(B)/t_crash_bt_unguarded \
	$(B)/t_crash_stderr \
	$(B)/t_crash_stderr_counted \
	$(B)/t_crash_stderr_nodeadman \
	$(B)/t_crash_stderr_alrm \
	$(B)/t_il2cpp_obj \
	$(B)/t_il2cpp_obj_nocheck \
	$(B)/t_envfile \
	$(B)/t_fit_rect \
	$(B)/t_fit_rect_ignore \
	$(B)/t_native_size \
	$(B)/t_native_size_long \
	$(B)/t_native_size_unclamped \
	$(B)/t_freeze \
	$(B)/t_freeze_none \
	$(B)/t_fs \
	$(B)/t_fs_nopin \
	$(B)/t_fs_noreprep \
	$(B)/t_fs_saveyes \
	$(B)/t_fs_strict \
	$(B)/t_fs_case \
	$(B)/t_fs_core \
	$(B)/t_fs_core_kept \
	$(B)/t_fs_index \
	$(B)/t_fs_kill \
	$(B)/t_fs_kill_inplace \
	$(B)/t_gameslot \
	$(B)/t_gate_svc \
	$(B)/t_gate_svc_rel \
	$(B)/t_guest_mem \
	$(B)/t_guest_mem_heapold \
	$(B)/t_guest_mem_physold \
	$(B)/t_guest_mem_perpage \
	$(B)/t_guest_mem_getinfoold \
	$(B)/t_guest_mem_arold \
	$(B)/t_guest_mem_unnoted \
	$(B)/t_hid_receipt \
	$(B)/t_hid_receipt_oneslot \
	$(B)/t_hid_applet \
	$(B)/t_totk_defaults \
	$(B)/t_totk_defaults_noxlat \
	$(B)/t_totk_defaults_tbdr \
	$(B)/t_hid_applet_silent \
	$(B)/t_hid_script \
	$(B)/t_hid_script_nocircle \
	$(B)/t_hid_script_norstick \
	$(B)/t_hstack \
	$(B)/t_hstack_old \
	$(B)/t_hstack_inpatch \
	$(B)/t_ktrace \
	$(B)/t_loader_weak \
	$(B)/t_logstamp \
	$(B)/t_thermal \
	$(B)/t_thermal_weak \
	$(B)/t_logstamp_every \
	$(B)/t_memsnap \
	$(B)/t_haptics \
	$(B)/t_haptics_nogap \
	$(B)/t_haptics_anyfocus \
	$(B)/t_motion \
	$(B)/t_procon \
	$(B)/t_procon_raw \
	$(B)/t_aspect \
	$(B)/t_aspect_nofollow \
	$(B)/t_motion_allstyles \
	$(B)/t_motion_nobias \
	$(B)/t_motion_nograv \
	$(B)/t_motion_nopeak \
	$(B)/t_mods \
	$(B)/t_mods_case \
	$(B)/t_mods_nopin \
	$(B)/t_mouse_route \
	$(B)/t_mouse_route_swallow \
	$(B)/t_movie \
	$(B)/t_movie_evcopy \
	$(B)/t_net \
	$(B)/t_net_bsd \
	$(B)/t_oe \
	$(B)/t_oe_noop \
	$(B)/t_oe_unlocked \
	$(B)/t_oe_noios \
	$(B)/t_movie_fps \
	$(B)/t_movie_fps_any \
	$(B)/t_optref \
	$(B)/t_optref_one \
	$(B)/t_survey \
	$(B)/t_lang \
	$(B)/t_mano_display \
	$(B)/t_mano_display_const \
	$(B)/t_mano_follow \
	$(B)/t_mano_follow_boot \
	$(B)/t_lang_region \
	$(B)/t_lang_regfirst \
	$(B)/t_guest_as \
	$(B)/t_romfs_parts \
	$(B)/t_coolstart \
	$(B)/t_pwrspec \
	$(B)/t_pwrspec_noduty \
	$(B)/t_rtld \
	$(B)/t_rng_font \
	$(B)/t_rng_font_rev \
	$(B)/t_bfttf \
	$(B)/t_bfttf_one \
	$(B)/t_shadercache \
	$(B)/t_shadercache_metal \
	$(B)/t_schedstats \
	$(B)/t_schedstats_start \
	$(B)/t_rtld_order \
	$(B)/t_rtld_ours \
	$(B)/t_coolstart_onelook \
	$(B)/t_romfs_parts_nosha \
	$(B)/t_romfs_parts_anypath \
	$(B)/t_guest_as_nolimit \
	$(B)/t_survey_cols \
	$(B)/t_options \
	$(B)/t_options_nocheck \
	$(B)/t_cheat_vm \
	$(B)/t_cheat_vm_flat \
	$(B)/t_cheats \
	$(B)/t_cheats_norw \
	$(B)/t_padmap \
	$(B)/t_pchtxt \
	$(B)/t_pchtxt_nocheck \
	$(B)/t_pchtxt_oldflags \
	$(B)/t_ns_text \
	$(B)/t_ns_text_default \
	$(B)/t_padmap_posall \
	$(B)/t_quitwait \
	$(B)/t_quitwait_old \
	$(B)/t_relr \
	$(B)/t_relr_unchecked \
	$(B)/t_romfs_validate \
	$(B)/t_romfs_validate_nokeys \
	$(B)/t_romfs_validate_anyorig \
	$(B)/t_rundiag \
	$(B)/t_rundiag_nokeep \
	$(B)/t_rundiag_head \
	$(B)/t_rundiag_noredact \
	$(B)/t_sead \
	$(B)/t_sead_broken \
	$(B)/t_sleep \
	$(B)/t_stickdir \
	$(B)/t_stickdir_none \
	$(B)/t_swkbd \
	$(B)/t_sysevent \
	$(B)/t_threadcpu \
	$(B)/t_threaddump \
	$(B)/t_threaddump_suspended \
	$(B)/t_thread_exit \
	$(B)/t_thread_exit_old \
	$(B)/t_thread_leak \
	$(B)/t_time \
	$(B)/t_time_one \
	$(B)/t_time_ctxold \
	$(B)/t_time_span \
	$(B)/t_tzrule \
	$(B)/t_tzrule_dev \
	$(B)/t_touch \
	$(B)/t_touchpad \
	$(B)/t_touchpad_rehit \
	$(B)/t_touchpad_sensafter \
	$(B)/t_touchpad_custom \
	$(B)/t_touchpad_nowin \
	$(B)/t_touchpad_ctlconnect \
	$(B)/t_watchdog \
	$(B)/t_watchdog_nobg
	$(B)/t_altstack
	$(B)/t_arbiter
	$(B)/t_audio
	$(B)/t_audio_spsc
	$(B)/t_audio_extras
	@status=0; $(B)/t_audio_extras_early > $(B)/t_audio_extras_early.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_audio_extras_early.log)" -ne 1 ]; then \
	   cat $(B)/t_audio_extras_early.log; echo "t_audio_extras_early: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_audio_extras_early: an output in the registry before it is made fails 1, as expected"
	@for v in vol:3 kept:1 stopkeeps:1; do t=$${v%%:*}; want=$${v##*:}; status=0; $(B)/t_audio_extras_$$t > $(B)/t_audio_extras_$$t.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_audio_extras_$$t.log)" -ne "$$want" ]; then \
	   cat $(B)/t_audio_extras_$$t.log; echo "t_audio_extras_$$t: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_audio_extras_$$t: $$want fail, as expected"; done
	$(B)/t_audren 2>/dev/null
	@status=0; $(B)/t_audren_void > $(B)/t_audren_void.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ AcquireAudioDeviceNotificationForOutput: success" $(B)/t_audren_void.log; then \
	   cat $(B)/t_audren_void.log; echo "t_audren_void: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_audren_void: a void notification leaves the event's address as the result, as expected"
	@status=0; $(B)/t_audren_unbounded > $(B)/t_audren_unbounded.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_audren_unbounded.log)" -ne 1 ] || ! grep -q "✗ a command whose size runs past" $(B)/t_audren_unbounded.log; then \
	   cat $(B)/t_audren_unbounded.log; echo "t_audren_unbounded: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_audren_unbounded: a command list walked without its extent reads past its end, as expected"
	$(B)/t_audren_pace
	@status=0; $(B)/t_audren_pace_out > $(B)/t_audren_pace_out.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "no two frames under 1 ms apart *✗" $(B)/t_audren_pace_out.log; then \
	   cat $(B)/t_audren_pace_out.log; echo "t_audren_pace_out: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_audren_pace_out: frames paced by the output come in pairs, as expected"
	@status=0; $(B)/t_audren_pace_sink > $(B)/t_audren_pace_sink.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "after a 2 s suspension: 9-11 pulls.*✗" $(B)/t_audren_pace_sink.log; then \
	   cat $(B)/t_audren_pace_sink.log; echo "t_audren_pace_sink: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_audren_pace_sink: a silent sink never restarted pulls in a burst after a suspension, as expected"
	$(B)/t_cloudlist 2>/dev/null
	@status=0; $(B)/t_cloudlist_old > $(B)/t_cloudlist_old.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "before the call returned *✗" $(B)/t_cloudlist_old.log; then \
	   cat $(B)/t_cloudlist_old.log; echo "t_cloudlist_old: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_cloudlist_old: a listing sent to the main queue from the main thread never starts, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_cloudsync 2>/dev/null
	TMPDIR="$(abspath $(B))" $(B)/t_cmdchan 2>/dev/null
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_cmdchan_partial > $(B)/t_cmdchan_partial.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ ... and runs once, whole" $(B)/t_cmdchan_partial.log; then \
	   cat $(B)/t_cmdchan_partial.log; echo "t_cmdchan_partial: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_cmdchan_partial: running a line as far as it was written misses the line written in two parts, as expected"
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_cmdchan_noreopen > $(B)/t_cmdchan_noreopen.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ the file replaced by another" $(B)/t_cmdchan_noreopen.log; then \
	   cat $(B)/t_cmdchan_noreopen.log; echo "t_cmdchan_noreopen: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_cmdchan_noreopen: following the first file for good misses the replaced file's lines, as expected"
	$(B)/t_condvar
	$(B)/t_condvar_old
	$(B)/t_waitsync
	$(B)/t_coremask 2>/dev/null
	@status=0; $(B)/t_coremask_noproc > $(B)/t_coremask_noproc.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ mask 8, outside the process's cores" $(B)/t_coremask_noproc.log; then \
	   cat $(B)/t_coremask_noproc.log; echo "t_coremask_noproc: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_coremask_noproc: a core mask outside the process's cores let through moves the thread to core 3, as expected"
	$(B)/t_thrpub 2>/dev/null
	@for v in early prio; do status=0; $(B)/t_thrpub_$$v > $(B)/t_thrpub_$$v.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_thrpub_$$v.log)" -ne 1 ]; then \
	   cat $(B)/t_thrpub_$$v.log; echo "t_thrpub_$$v: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_thrpub_$$v: fails 1, as expected"; done
	$(B)/t_mainqos
	$(B)/t_mainqos --ui
	@status=0; $(B)/t_mainqos_ignored --ui > $(B)/t_mainqos_ignored.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_mainqos_ignored.log)" -ne 1 ]; then \
	   cat $(B)/t_mainqos_ignored.log; echo "t_mainqos_ignored: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_mainqos_ignored: with the switch ignored the main thread keeps no class, as expected"
	$(B)/t_crash_bt
	@status=0; $(B)/t_crash_bt_unguarded > $(B)/t_crash_bt_unguarded.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ a corrupt frame pointer" $(B)/t_crash_bt_unguarded.log; then \
	   cat $(B)/t_crash_bt_unguarded.log; echo "t_crash_bt_unguarded: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_crash_bt_unguarded: a backtrace reading frames directly faults on a corrupt frame pointer, as expected"
	$(B)/t_crash_stderr
	@status=0; $(B)/t_crash_stderr_counted > $(B)/t_crash_stderr_counted.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c ' FAIL$$' $(B)/t_crash_stderr_counted.log)" -ne 1 ] || ! grep -q "six times as long.* FAIL" $(B)/t_crash_stderr_counted.log; then \
	   cat $(B)/t_crash_stderr_counted.log; echo "t_crash_stderr_counted: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_crash_stderr_counted: 100 sleeps counted take 6 s when each sleeps longer, as expected"
	@status=0; $(B)/t_crash_stderr_nodeadman > $(B)/t_crash_stderr_nodeadman.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c ' FAIL$$' $(B)/t_crash_stderr_nodeadman.log)" -ne 9 ] || ! grep -q "never returns.* FAIL" $(B)/t_crash_stderr_nodeadman.log; then \
	   cat $(B)/t_crash_stderr_nodeadman.log; echo "t_crash_stderr_nodeadman: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_crash_stderr_nodeadman: a crash report's second part without a limit hangs on a stuck dump, as expected"
	@status=0; $(B)/t_crash_stderr_alrm > $(B)/t_crash_stderr_alrm.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c ' FAIL$$' $(B)/t_crash_stderr_alrm.log)" -ne 3 ] || ! grep -q "SIGALRM's report.* FAIL" $(B)/t_crash_stderr_alrm.log; then \
	   cat $(B)/t_crash_stderr_alrm.log; echo "t_crash_stderr_alrm: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_crash_stderr_alrm: SIGALRM's own report with SIGALRM left blocked hangs, as expected"
	$(B)/t_il2cpp_obj
	@status=0; $(B)/t_il2cpp_obj_nocheck > $(B)/t_il2cpp_obj_nocheck.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_il2cpp_obj_nocheck.log)" -ne 2 ]; then \
	   cat $(B)/t_il2cpp_obj_nocheck.log; echo "t_il2cpp_obj_nocheck: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_il2cpp_obj_nocheck: any string taken for a class name names a binary one, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_envfile
	$(B)/t_native_size
	$(B)/t_fit_rect
	@status=0; $(B)/t_fit_rect_ignore > $(B)/t_fit_rect_ignore.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_fit_rect_ignore.log)" -ne 6 ]; then \
	   cat $(B)/t_fit_rect_ignore.log; echo "t_fit_rect_ignore: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_fit_rect_ignore: the picture left centered over the camera fails 6, as expected"
	@status=0; $(B)/t_native_size_long > $(B)/t_native_size_long.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_native_size_long.log)" -ne 3 ]; then \
	   cat $(B)/t_native_size_long.log; echo "t_native_size_long: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_native_size_long: the long side's 9/16 alone fails 3, as expected"
	@status=0; $(B)/t_native_size_unclamped > $(B)/t_native_size_unclamped.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_native_size_unclamped.log)" -ne 2 ]; then \
	   cat $(B)/t_native_size_unclamped.log; echo "t_native_size_unclamped: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_native_size_unclamped: the screen's size whatever the window textures hold fails 2, as expected"
	$(B)/t_freeze
	@status=0; $(B)/t_freeze_none > $(B)/t_freeze_none.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ freeze: one dump" $(B)/t_freeze_none.log; then \
	   cat $(B)/t_freeze_none.log; echo "t_freeze_none: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_freeze_none: without a test watchdog no freeze is dumped, as expected"
	$(B)/t_fs 2>/dev/null
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_fs_nopin > $(B)/t_fs_nopin.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '  FAIL ' $(B)/t_fs_nopin.log)" -ne 1 ] || ! grep -q "FAIL a pinned file wins" $(B)/t_fs_nopin.log; then \
	   cat $(B)/t_fs_nopin.log; echo "t_fs_nopin: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_fs_nopin: with the pins ignored the overlay's file is served, as expected"
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_fs_noreprep > $(B)/t_fs_noreprep.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '  FAIL ' $(B)/t_fs_noreprep.log)" -ne 1 ] || ! grep -q "FAIL ReadFile into pages protected again" $(B)/t_fs_noreprep.log; then \
	   cat $(B)/t_fs_noreprep.log; echo "t_fs_noreprep: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_fs_noreprep: with one prepare the read into pages protected again fails, as expected"
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_fs_saveyes > $(B)/t_fs_saveyes.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '  FAIL ' $(B)/t_fs_saveyes.log)" -ne 1 ] || ! grep -q "FAIL IsSaveDataExisting" $(B)/t_fs_saveyes.log; then \
	   cat $(B)/t_fs_saveyes.log; echo "t_fs_saveyes: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_fs_saveyes: a save that is not there reported as existing, as expected"
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_fs_strict > $(B)/t_fs_strict.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '  FAIL ' $(B)/t_fs_strict.log)" -ne 2 ]; then \
	   cat $(B)/t_fs_strict.log; echo "t_fs_strict: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_fs_strict: \"/rom:/\" and backslashes refused, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_fs_case 2>/dev/null
	$(B)/t_fs_core 2>/dev/null
	@status=0; $(B)/t_fs_core_kept > $(B)/t_fs_core_kept.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '^FAIL' $(B)/t_fs_core_kept.log)" -ne 2 ]; then \
	   cat $(B)/t_fs_core_kept.log; echo "t_fs_core_kept: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_fs_core_kept: nn::fs calls keeping the core fail 2, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_fs_index 2>/dev/null
	TMPDIR="$(abspath $(B))" $(B)/t_fs_kill
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_fs_kill_inplace > $(B)/t_fs_kill_inplace.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ after every kill" $(B)/t_fs_kill_inplace.log; then \
	   cat $(B)/t_fs_kill_inplace.log; echo "t_fs_kill_inplace: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_fs_kill_inplace: a commit that removes the committed save before cloning in its place tears it under SIGKILL, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_gameslot 2>/dev/null
	$(B)/t_gate_svc
	@status=0; $(B)/t_gate_svc_rel > $(B)/t_gate_svc_rel.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ svc_dispatch gets the svc" $(B)/t_gate_svc_rel.log; then \
	   cat $(B)/t_gate_svc_rel.log; echo "t_gate_svc_rel: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_gate_svc_rel: an svc gate passing the offset alone gives an address in no module, as expected"
	$(B)/t_guest_mem 2>/dev/null
	@status=0; $(B)/t_guest_mem_getinfoold > $(B)/t_guest_mem_getinfoold.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "FAIL an unknown type: InvalidEnumValue" $(B)/t_guest_mem_getinfoold.log; then \
	   cat $(B)/t_guest_mem_getinfoold.log; echo "t_guest_mem_getinfoold: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_guest_mem_getinfoold: an unknown GetInfo type that succeeds with 0, as expected"
	@status=0; $(B)/t_guest_mem_arold > $(B)/t_guest_mem_arold.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "FAIL a refused address region" $(B)/t_guest_mem_arold.log; then \
	   cat $(B)/t_guest_mem_arold.log; echo "t_guest_mem_arold: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_guest_mem_arold: a refused address region left unseen, as expected"
	@status=0; $(B)/t_guest_mem_unnoted > $(B)/t_guest_mem_unnoted.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '^FAIL' $(B)/t_guest_mem_unnoted.log)" -ne 2 ] || ! grep -q "FAIL UnmapPhysicalMemory: one call" $(B)/t_guest_mem_unnoted.log; then \
	   cat $(B)/t_guest_mem_unnoted.log; echo "t_guest_mem_unnoted: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_guest_mem_unnoted: memory given back without the backend's hook fails 2, as expected"
	@status=0; $(B)/t_guest_mem_perpage > $(B)/t_guest_mem_perpage.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '^FAIL' $(B)/t_guest_mem_perpage.log)" -ne 1 ] || ! grep -q "FAIL 4 MB mapped in one system call" $(B)/t_guest_mem_perpage.log; then \
	   cat $(B)/t_guest_mem_perpage.log; echo "t_guest_mem_perpage: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_guest_mem_perpage: a system call for each host page fails 1, as expected"
	@status=0; $(B)/t_guest_mem_physold > $(B)/t_guest_mem_physold.log 2>&1 || status=$$?; \
	 if [ "$$status" -eq 0 ] || ! grep -q "address / size not 4 KB aligned" $(B)/t_guest_mem_physold.log; then \
	   tail -5 $(B)/t_guest_mem_physold.log; echo "t_guest_mem_physold: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_guest_mem_physold: the old MapPhysicalMemory ends the program on an unaligned address, as expected"
	@status=0; $(B)/t_guest_mem_heapold > $(B)/t_guest_mem_heapold.log 2>&1 || status=$$?; \
	 if [ "$$status" -eq 0 ] || ! grep -q "FAIL SetHeapSize(1): InvalidSize" $(B)/t_guest_mem_heapold.log; then \
	   tail -5 $(B)/t_guest_mem_heapold.log; echo "t_guest_mem_heapold: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_guest_mem_heapold: the old SetHeapSize takes a 1-byte heap and the next growth faults, as expected"
	$(B)/t_hid_receipt 2>/dev/null
	@status=0; $(B)/t_hid_receipt_oneslot > $(B)/t_hid_receipt_oneslot.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "HID_RECEIPT FAIL cases=6 failures=1" $(B)/t_hid_receipt_oneslot.log; then \
	   cat $(B)/t_hid_receipt_oneslot.log; echo "t_hid_receipt_oneslot: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_hid_receipt_oneslot: one slot for all styles logs every poll of a game polling two, as expected"
	$(B)/t_hid_applet 2>/dev/null
	@status=0; $(B)/t_hid_applet_silent > $(B)/t_hid_applet_silent.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_hid_applet_silent.log)" -ne 2 ]; then \
	   cat $(B)/t_hid_applet_silent.log; echo "t_hid_applet_silent: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_hid_applet_silent: the applet's result not written, as expected"
	$(B)/t_haptics
	@status=0; $(B)/t_haptics_nogap > $(B)/t_haptics_nogap.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_haptics_nogap.log)" -ne 1 ]; then \
	   cat $(B)/t_haptics_nogap.log; echo "t_haptics_nogap: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_haptics_nogap: pulses as often as asked fail the gap check, as expected"
	@status=0; $(B)/t_haptics_anyfocus > $(B)/t_haptics_anyfocus.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_haptics_anyfocus.log)" -ne 1 ]; then \
	   cat $(B)/t_haptics_anyfocus.log; echo "t_haptics_anyfocus: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_haptics_anyfocus: the phone's engine made in the background fails, as expected"
	$(B)/t_motion 2>/dev/null
	$(B)/t_procon
	@status=0; $(B)/t_procon_raw > $(B)/t_procon_raw.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_procon_raw.log)" -ne 3 ]; then \
	   cat $(B)/t_procon_raw.log; echo "t_procon_raw: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_procon_raw: the raw axes as they come fail the three axis checks, as expected"
	$(B)/t_aspect 2>/dev/null
	@status=0; $(B)/t_aspect_nofollow > $(B)/t_aspect_nofollow.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_aspect_nofollow.log)" -ne 1 ]; then \
	   cat $(B)/t_aspect_nofollow.log; echo "t_aspect_nofollow: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_aspect_nofollow: \"window\" keeping its first shape fails the resize check, as expected"
	@for v in allstyles nobias nograv nopeak; do status=0; $(B)/t_motion_$$v > $(B)/t_motion_$$v.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_motion_$$v.log)" -ne 1 ]; then \
	   cat $(B)/t_motion_$$v.log; echo "t_motion_$$v: unexpected negative-control result ($$status)"; exit 1; \
	 fi; done; echo "t_motion: every style forwarded, the zero point never learned, no gravity, no peak hold each fail one, as expected"
	env -u DM_NVN_XLAT -u DM_NVN_TBDR $(B)/t_totk_defaults
	@status=0; env -u DM_NVN_XLAT -u DM_NVN_TBDR $(B)/t_totk_defaults_noxlat > $(B)/t_totk_defaults_noxlat.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_totk_defaults_noxlat.log)" -ne 1 ]; then \
	   cat $(B)/t_totk_defaults_noxlat.log; echo "t_totk_defaults_noxlat: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_totk_defaults_noxlat: the translation thread off by default, as expected"
	@status=0; env -u DM_NVN_XLAT -u DM_NVN_TBDR $(B)/t_totk_defaults_tbdr > $(B)/t_totk_defaults_tbdr.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_totk_defaults_tbdr.log)" -ne 1 ]; then \
	   cat $(B)/t_totk_defaults_tbdr.log; echo "t_totk_defaults_tbdr: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_totk_defaults_tbdr: the TBDR rewrites left on, as expected"
	$(B)/t_hid_script 2>/dev/null
	@status=0; $(B)/t_hid_script_nocircle > $(B)/t_hid_script_nocircle.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "FAIL LSL and LSU overlap" $(B)/t_hid_script_nocircle.log; then \
	   cat $(B)/t_hid_script_nocircle.log; echo "t_hid_script_nocircle: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_hid_script_nocircle: sticks clamped per axis leave the circle on diagonals, as expected"
	@status=0; $(B)/t_hid_script_norstick > $(B)/t_hid_script_norstick.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "FAIL RSR pushes the right stick right" $(B)/t_hid_script_norstick.log; then \
	   cat $(B)/t_hid_script_norstick.log; echo "t_hid_script_norstick: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_hid_script_norstick: right stick keys not applied fail, as expected"
	$(B)/t_hstack 2>/dev/null
	DM_HSTACK=0 $(B)/t_hstack 2>/dev/null
	@status=0; DM_HSTACK=0 $(B)/t_hstack deep > $(B)/t_hstack_deep.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "touched the page below the guest stack" $(B)/t_hstack_deep.log; then \
	   cat $(B)/t_hstack_deep.log; echo "t_hstack deep with DM_HSTACK=0: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_hstack deep with DM_HSTACK=0: host code on the guest stack faults on the page below it, as expected"
	@status=0; $(B)/t_hstack_old > $(B)/t_hstack_old.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '^FAIL 5' $(B)/t_hstack_old.log)" -lt 3 ] || grep -q '^FAIL [^5]' $(B)/t_hstack_old.log; then \
	   cat $(B)/t_hstack_old.log; echo "t_hstack_old: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_hstack_old: the context as before misses the guest's stack in all three pauses, as expected"
	@status=0; $(B)/t_hstack_inpatch > $(B)/t_hstack_inpatch.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '^FAIL ' $(B)/t_hstack_inpatch.log)" -ne 1 ] || ! grep -q '^FAIL 9:' $(B)/t_hstack_inpatch.log; then \
	   cat $(B)/t_hstack_inpatch.log; echo "t_hstack_inpatch: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_hstack_inpatch: stopped inside rewrite blocks as before, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_ktrace 2>/dev/null
	$(B)/t_loader_weak 2>/dev/null
	$(B)/t_logstamp
	$(B)/t_thermal
	@status=0; $(B)/t_thermal_weak > $(B)/t_thermal_weak.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗" $(B)/t_thermal_weak.log; then cat $(B)/t_thermal_weak.log; echo "t_thermal_weak: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_thermal_weak: with the weak default alone the state reads -1, as expected"
	@status=0; $(B)/t_logstamp_every > $(B)/t_logstamp_every.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ no stamp inside a line" $(B)/t_logstamp_every.log; then \
	   cat $(B)/t_logstamp_every.log; echo "t_logstamp_every: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_logstamp_every: a stamp before every write lands inside a line written in two goes, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_memsnap 2>/dev/null
	TMPDIR="$(abspath $(B))" $(B)/t_mods 2>/dev/null
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_mods_case > $(B)/t_mods_case.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ 01_one's layer" $(B)/t_mods_case.log; then \
	   cat $(B)/t_mods_case.log; echo "t_mods_case: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_mods_case: paths built with the program ID as asked break on a file system that tells cases apart, as expected"
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_mods_nopin > $(B)/t_mods_nopin.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_mods_nopin.log)" -ne 3 ]; then \
	   cat $(B)/t_mods_nopin.log; echo "t_mods_nopin: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_mods_nopin: with the mod's copy always used the overlay's change is lost, as expected"
	$(B)/t_mouse_route
	@status=0; $(B)/t_mouse_route_swallow > $(B)/t_mouse_route_swallow.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "press on the title bar .*✗" $(B)/t_mouse_route_swallow.log; then \
	   cat $(B)/t_mouse_route_swallow.log; echo "t_mouse_route_swallow: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_mouse_route_swallow: a monitor swallowing every press outside the picture takes the title bar from AppKit, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_movie 2>/dev/null
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_movie_evcopy > $(B)/t_movie_evcopy.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "31 checks, 3 failed" $(B)/t_movie_evcopy.log; then \
	   cat $(B)/t_movie_evcopy.log; echo "t_movie_evcopy: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_movie_evcopy: events copied at registration stay empty, as expected"
	$(B)/t_net
	@status=0; $(B)/t_net_bsd > $(B)/t_net_bsd.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "ENetDown 100 *✗" $(B)/t_net_bsd.log; then \
	   cat $(B)/t_net_bsd.log; echo "t_net_bsd: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_net_bsd: the FreeBSD errno numbers are not nn::socket's, as expected"
	$(B)/t_oe 2>/dev/null
	@status=0; $(B)/t_oe_noop > $(B)/t_oe_noop.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "FAIL inactivity time extended" $(B)/t_oe_noop.log; then \
	   cat $(B)/t_oe_noop.log; echo "t_oe_noop: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_oe_noop: the old stub never holds the frame rate, as expected"
	@status=0; $(B)/t_oe_unlocked > $(B)/t_oe_unlocked.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "FAIL event signalled with the message pushed" $(B)/t_oe_unlocked.log; then \
	   cat $(B)/t_oe_unlocked.log; echo "t_oe_unlocked: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_oe_unlocked: the event cleared after unlocking ends clear with a message queued, as expected"
	@status=0; $(B)/t_oe_noios > $(B)/t_oe_noios.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '^FAIL' $(B)/t_oe_noios.log)" -ne 1 ] || ! grep -q "FAIL iOS, a profile with ios_mode handheld" $(B)/t_oe_noios.log; then \
	   cat $(B)/t_oe_noios.log; echo "t_oe_noios: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_oe_noios: without the profile's iOS mode Tears of the Kingdom starts docked on iOS, as expected"
	$(B)/t_movie_fps
	@status=0; $(B)/t_movie_fps_any > $(B)/t_movie_fps_any.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "main+0x770f04 .*✗" $(B)/t_movie_fps_any.log; then \
	   cat $(B)/t_movie_fps_any.log; echo "t_movie_fps_any: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_movie_fps_any: holding every cutscene holds the prologue's real-time one too, as expected"
	$(B)/t_optref
	@status=0; $(B)/t_optref_one > $(B)/t_optref_one.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_optref_one.log)" -ne 1 ] || ! grep -q "the iPhone's.*✗" $(B)/t_optref_one.log; then \
	   cat $(B)/t_optref_one.log; echo "t_optref_one: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_optref_one: MacNativeGames' value alone lets the iPhone's crash through, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_survey
	$(B)/t_lang
	@status=0; $(B)/t_lang_region > $(B)/t_lang_region.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_lang_region.log)" -ne 6 ]; then \
	   cat $(B)/t_lang_region.log; echo "t_lang_region: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_lang_region: the language alone loses Traditional Chinese, British English, Canadian French and Latin American Spanish, as expected"
	@status=0; $(B)/t_lang_regfirst > $(B)/t_lang_regfirst.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_lang_regfirst.log)" -ne 1 ] || ! grep -q "zh-Hans-HK.*✗" $(B)/t_lang_regfirst.log; then \
	   cat $(B)/t_lang_regfirst.log; echo "t_lang_regfirst: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_lang_regfirst: the region before the script makes zh-Hans-HK Traditional Chinese, as expected"
	$(B)/t_guest_as
	@status=0; $(B)/t_guest_as_nolimit > $(B)/t_guest_as_nolimit.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_guest_as_nolimit.log)" -ne 2 ]; then \
	   cat $(B)/t_guest_as_nolimit.log; echo "t_guest_as_nolimit: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_guest_as_nolimit: without the 2^39 bound the gaps past it are taken, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_romfs_parts
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_romfs_parts_nosha > $(B)/t_romfs_parts_nosha.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_romfs_parts_nosha.log)" -ne 2 ] || ! grep -q "one byte changed.*✗" $(B)/t_romfs_parts_nosha.log; then \
	   cat $(B)/t_romfs_parts_nosha.log; echo "t_romfs_parts_nosha: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_romfs_parts_nosha: without the digest a damaged part is unpacked, as expected"
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_romfs_parts_anypath > $(B)/t_romfs_parts_anypath.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_romfs_parts_anypath.log)" -ne 1 ] || ! grep -q "evil.*✗" $(B)/t_romfs_parts_anypath.log; then \
	   cat $(B)/t_romfs_parts_anypath.log; echo "t_romfs_parts_anypath: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_romfs_parts_anypath: paths taken as they are write outside the tree, as expected"
	$(B)/t_pwrspec
	@status=0; $(B)/t_pwrspec_noduty > $(B)/t_pwrspec_noduty.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_pwrspec_noduty.log)" -ne 1 ]; then \
	   cat $(B)/t_pwrspec_noduty.log; echo "t_pwrspec_noduty: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_pwrspec_noduty: with the share ignored the GPU runs fully busy, as expected"
	$(B)/t_coolstart
	@status=0; $(B)/t_coolstart_onelook > $(B)/t_coolstart_onelook.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_coolstart_onelook.log)" -ne 3 ]; then \
	   cat $(B)/t_coolstart_onelook.log; echo "t_coolstart_onelook: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_coolstart_onelook: one look and on starts runs hot, as expected"
	$(B)/t_rtld 2>/dev/null
	@status=0; $(B)/t_rtld_order > $(B)/t_rtld_order.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_rtld_order.log)" -ne 3 ] || ! grep -q "subsdk0-9, sdk).*" $(B)/t_rtld_order.log; then \
	   cat $(B)/t_rtld_order.log; echo "t_rtld_order: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_rtld_order: the profile's module order binds main's foo to sdk's, as expected"
	@status=0; $(B)/t_rtld_ours > $(B)/t_rtld_ours.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_rtld_ours.log)" -ne 1 ] || ! grep -q "✗ DM_RTLD unset" $(B)/t_rtld_ours.log; then \
	   cat $(B)/t_rtld_ours.log; echo "t_rtld_ours: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_rtld_ours: ours as the default, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_rng_font 2>/dev/null
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_rng_font_rev > $(B)/t_rng_font_rev.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_rng_font_rev.log)" -ne 2 ]; then \
	   cat $(B)/t_rng_font_rev.log; echo "t_rng_font_rev: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_rng_font_rev: fonts in reverse order miss their types, as expected"
	$(B)/t_bfttf
	@status=0; $(B)/t_bfttf_one > $(B)/t_bfttf_one.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_bfttf_one.log)" -ne 1 ]; then \
	   cat $(B)/t_bfttf_one.log; echo "t_bfttf_one: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_bfttf_one: with one word order the byte-swapped fonts are refused, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_shadercache
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_shadercache_metal > $(B)/t_shadercache_metal.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_shadercache_metal.log)" -ne 2 ]; then \
	   cat $(B)/t_shadercache_metal.log; echo "t_shadercache_metal: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_shadercache_metal: with com.apple.metal alone the archiver and front-end caches stay, as expected"
	$(B)/t_schedstats
	@status=0; $(B)/t_schedstats_start > $(B)/t_schedstats_start.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_schedstats_start.log)" -ne 1 ]; then \
	   cat $(B)/t_schedstats_start.log; echo "t_schedstats_start: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_schedstats_start: the latency counted from the wait's start reads long, as expected"
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_survey_cols > $(B)/t_survey_cols.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_survey_cols.log)" -ne 1 ] || ! grep -q "the rotation's rows.*✗" $(B)/t_survey_cols.log; then \
	   cat $(B)/t_survey_cols.log; echo "t_survey_cols: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_survey_cols: a transposed rotation turns Link the wrong way, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_options 2>/dev/null
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_options_nocheck > $(B)/t_options_nocheck.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ c with a row" $(B)/t_options_nocheck.log; then \
	   cat $(B)/t_options_nocheck.log; echo "t_options_nocheck: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_options_nocheck: an option written without checking its originals is half applied, as expected"
	$(B)/t_cheat_vm
	$(B)/t_cheats
	@for v in t_cheat_vm_flat t_cheats_norw; do status=0; $(B)/$$v > $(B)/$$v.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/$$v.log)" -ne 1 ]; then cat $(B)/$$v.log; echo "$$v: unexpected negative-control result ($$status)"; exit 1; fi; done; \
	 echo "t_cheat_vm_flat / t_cheats_norw: the nesting ignored, the rewriter's words unchecked fail one each, as expected"
	$(B)/t_padmap
	$(B)/t_pchtxt 2>/dev/null
	@status=0; $(B)/t_pchtxt_nocheck > $(B)/t_pchtxt_nocheck.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_pchtxt_nocheck.log)" -ne 5 ]; then \
	   cat $(B)/t_pchtxt_nocheck.log; echo "t_pchtxt_nocheck: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_pchtxt_nocheck: patches over the rewritten code fail the five refusals, as expected"
	@status=0; $(B)/t_pchtxt_oldflags > $(B)/t_pchtxt_oldflags.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_pchtxt_oldflags.log)" -ne 5 ]; then \
	   cat $(B)/t_pchtxt_oldflags.log; echo "t_pchtxt_oldflags: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_pchtxt_oldflags: with every @flag skipped the five offset and flag checks fail, as expected"
	$(B)/t_ns_text
	@status=0; $(B)/t_ns_text_default > $(B)/t_ns_text_default.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_ns_text_default.log)" -ne 2 ]; then \
	   cat $(B)/t_ns_text_default.log; echo "t_ns_text_default: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_ns_text_default: with %s the middle dots and the titles come out garbled, as expected"
	@status=0; $(B)/t_padmap_posall > $(B)/t_padmap_posall.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_padmap_posall.log)" -ne 2 ]; then \
	   cat $(B)/t_padmap_posall.log; echo "t_padmap_posall: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_padmap_posall: Nintendo controllers by position fail their two checks, as expected"
	$(B)/t_quitwait
	@status=0; $(B)/t_quitwait_old > $(B)/t_quitwait_old.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "waits until the game leaves it *✗" $(B)/t_quitwait_old.log; then \
	   cat $(B)/t_quitwait_old.log; echo "t_quitwait_old: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_quitwait_old: a quit that waits for the save alone leaves inside the exit-request section, as expected"
	$(B)/t_relr
	@status=0; $(B)/t_relr_unchecked > $(B)/t_relr_unchecked.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ refused whole: a bitmap before any address" $(B)/t_relr_unchecked.log; then \
	   cat $(B)/t_relr_unchecked.log; echo "t_relr_unchecked: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_relr_unchecked: a RELR table applied unchecked writes wherever its words say, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_romfs_validate
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_romfs_validate_nokeys > $(B)/t_romfs_validate_nokeys.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "FAIL marker: a key file cut short" $(B)/t_romfs_validate_nokeys.log; then \
	   cat $(B)/t_romfs_validate_nokeys.log; echo "t_romfs_validate_nokeys: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_romfs_validate_nokeys: a marker trusted for the key files too lets a cut-short key file through, as expected"
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_romfs_validate_anyorig > $(B)/t_romfs_validate_anyorig.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "FAIL overlay: another version's original refused" $(B)/t_romfs_validate_anyorig.log; then \
	   cat $(B)/t_romfs_validate_anyorig.log; echo "t_romfs_validate_anyorig: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_romfs_validate_anyorig: an overlay served without comparing the RomFS's original takes another version's, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_rundiag
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_rundiag_nokeep > $(B)/t_rundiag_nokeep.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ its log kept as boot.interrupted.txt" $(B)/t_rundiag_nokeep.log; then \
	   cat $(B)/t_rundiag_nokeep.log; echo "t_rundiag_nokeep: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_rundiag_nokeep: an interrupted session whose log is not kept leaves nothing to report, as expected"
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_rundiag_head > $(B)/t_rundiag_head.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ ... and gives its last 8 MiB exactly" $(B)/t_rundiag_head.log; then \
	   cat $(B)/t_rundiag_head.log; echo "t_rundiag_head: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_rundiag_head: the start of a long log misses its last lines, as expected"
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_rundiag_noredact > $(B)/t_rundiag_noredact.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_rundiag_noredact.log)" -ne 3 ]; then \
	   cat $(B)/t_rundiag_noredact.log; echo "t_rundiag_noredact: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_rundiag_noredact: an unredacted report gives away paths, addresses and the user, as expected"
	$(B)/t_sead 2>/dev/null
	$(B)/t_codesig $(B)/t_codesig_true $(B)/t_codesig_false $(B)/t_codesig
	env -u DM_DISPLAY_SIZE $(B)/t_mano_display 1920 1080 2>/dev/null
	DM_DISPLAY_SIZE=2241x1261 $(B)/t_mano_display 2240 1260 2>/dev/null
	DM_DISPLAY_SIZE=wide $(B)/t_mano_display 1920 1080 2>/dev/null
	@status=0; DM_DISPLAY_SIZE=2241x1261 $(B)/t_mano_display_const 2240 1260 > $(B)/t_mano_display_const.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ DM_DISPLAY_SIZE=2241x1261" $(B)/t_mano_display_const.log; then \
	   cat $(B)/t_mano_display_const.log; echo "t_mano_display_const: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_mano_display_const: the constant reported whatever is asked, as expected"
	$(B)/t_mano_follow 2>/dev/null
	$(B)/t_mano_follow 720 2>/dev/null
	@status=0; $(B)/t_mano_follow_boot > $(B)/t_mano_follow_boot.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_mano_follow_boot.log)" -ne 1 ]; then \
	   cat $(B)/t_mano_follow_boot.log; echo "t_mano_follow_boot: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_mano_follow_boot: the game's own call at the boot's height fails 1, as expected"
	@status=0; $(B)/t_codesig_broken $(B)/t_codesig_true $(B)/t_codesig_false $(B)/t_codesig > $(B)/t_codesig_broken.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ signed with it false" $(B)/t_codesig_broken.log; then \
	   cat $(B)/t_codesig_broken.log; echo "t_codesig_broken: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_codesig_broken: a key present with <false/> read as true, as expected"
	@status=0; $(B)/t_sead_broken > $(B)/t_sead_broken.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ an address past A's end" $(B)/t_sead_broken.log; then \
	   cat $(B)/t_sead_broken.log; echo "t_sead_broken: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_sead_broken: a heap taken without checking it reaches the address is found past its end, as expected"
	$(B)/t_sleep
	$(B)/t_stickdir
	@status=0; $(B)/t_stickdir_none > $(B)/t_stickdir_none.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "left stick fully left alone: bit 16 .*✗" $(B)/t_stickdir_none.log; then \
	   cat $(B)/t_stickdir_none.log; echo "t_stickdir_none: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_stickdir_none: sticks without direction buttons set none, as expected"
	$(B)/t_swkbd
	$(B)/t_sysevent 2>/dev/null
	$(B)/t_threadcpu 2>/dev/null
	$(B)/t_threaddump
	@status=0; $(B)/t_threaddump_suspended > $(B)/t_threaddump_suspended.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ the dump ends" $(B)/t_threaddump_suspended.log; then \
	   cat $(B)/t_threaddump_suspended.log; echo "t_threaddump_suspended: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_threaddump_suspended: printing while the thread is stopped waits forever for the lock it holds, as expected"
	$(B)/t_thread_exit
	DM_HSTACK=0 $(B)/t_thread_exit 2>/dev/null
	@status=0; DM_HSTACK=0 $(B)/t_thread_exit_old > $(B)/t_thread_exit_old.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "touched the retired guest stack" $(B)/t_thread_exit_old.log; then \
	   cat $(B)/t_thread_exit_old.log; echo "t_thread_exit_old: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_thread_exit_old: the pre-fix exit faults on the retired guest stack, as expected"
	$(B)/t_thread_leak
	$(B)/t_time 2>/dev/null
	@status=0; $(B)/t_time_one > $(B)/t_time_one.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ New York 2026-11-01 01:30, twice" $(B)/t_time_one.log; then \
	   cat $(B)/t_time_one.log; echo "t_time_one: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_time_one: local time tried as standard time only misses the repeated hour, as expected"
	@status=0; $(B)/t_time_ctxold > $(B)/t_time_ctxold.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "t_time: 19 checks, 2 failed" $(B)/t_time_ctxold.log; then \
	   cat $(B)/t_time_ctxold.log; echo "t_time_ctxold: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_time_ctxold: the old user clock context has no time point, as expected"
	@status=0; $(B)/t_time_span > $(B)/t_time_span.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "t_time: 19 checks, 3 failed" $(B)/t_time_span.log; then \
	   cat $(B)/t_time_span.log; echo "t_time_span: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_time_span: CalculateSpanBetween answering a Result reads backwards, as expected"
	$(B)/t_tzrule 2>/dev/null
	@status=0; $(B)/t_tzrule_dev > $(B)/t_tzrule_dev.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_tzrule_dev.log)" -ne 7 ]; then \
	   cat $(B)/t_tzrule_dev.log; echo "t_tzrule_dev: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_tzrule_dev: the rule ignored for the device zone, as expected"
	$(B)/t_touch
	$(B)/t_touchpad 2>/dev/null
	@status=0; $(B)/t_touchpad_rehit > $(B)/t_touchpad_rehit.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ slid off A" $(B)/t_touchpad_rehit.log; then \
	   cat $(B)/t_touchpad_rehit.log; echo "t_touchpad_rehit: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_touchpad_rehit: a finger that takes the button it slides onto loses A, as expected"
	@status=0; $(B)/t_touchpad_sensafter > $(B)/t_touchpad_sensafter.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ sensitivity 0.5×" $(B)/t_touchpad_sensafter.log; then \
	   cat $(B)/t_touchpad_sensafter.log; echo "t_touchpad_sensafter: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_touchpad_sensafter: a sensitivity applied after saturation never lets 0.5× turn at full speed, as expected"
	@status=0; $(B)/t_touchpad_custom > $(B)/t_touchpad_custom.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "t_touchpad: 64 checks, 5 failed" $(B)/t_touchpad_custom.log; then \
	   cat $(B)/t_touchpad_custom.log; echo "t_touchpad_custom: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_touchpad_custom: the default layout whatever the customization fails 5 layout checks, as expected"
	@status=0; $(B)/t_touchpad_nowin > $(B)/t_touchpad_nowin.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "t_touchpad: 64 checks, 3 failed" $(B)/t_touchpad_nowin.log; then \
	   cat $(B)/t_touchpad_nowin.log; echo "t_touchpad_nowin: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_touchpad_nowin: the window controls ignored put ZL under them on the three iPads that have them, as expected"
	@status=0; $(B)/t_touchpad_ctlconnect > $(B)/t_touchpad_ctlconnect.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "t_touchpad: 64 checks, 1 failed" $(B)/t_touchpad_ctlconnect.log; then \
	   cat $(B)/t_touchpad_ctlconnect.log; echo "t_touchpad_ctlconnect: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_touchpad_ctlconnect: hidden as soon as a controller connects, as before, fails the controller check, as expected"
	$(B)/t_watchdog
	@status=0; $(B)/t_watchdog_nobg > $(B)/t_watchdog_nobg.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ away" $(B)/t_watchdog_nobg.log; then \
	   cat $(B)/t_watchdog_nobg.log; echo "t_watchdog_nobg: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_watchdog_nobg: a watchdog that counts the background kills the app away from the screen, as expected"

$(GEN): ../../games/$(GAME)/game.json ../../tools/gen_game.py   # (without these the tests kept a profile from before a new field)
	$(MAKE) -C .. GAME=$(GAME) profile

# Parsers of outside input under the undefined-behavior sanitizer (plan item 111; MacNativeGames runs its parsers so in CI; ASan hangs in dyld on
# macOS 26): env.txt, the command channel, DM_KEYS scripts, mods, code options, RomFS checks, controller maps, the software keyboard's text, the run
# marker and the log stamps, each built again with UBSan (fatal) into build/ubsan; t_ubsan_on first shows the sanitizer is on and stops a run
U = $(B)/ubsan
UBSAN = -fsanitize=undefined -fno-sanitize-recover=undefined -g
UBSAN_T = t_envfile t_cmdchan t_hid_script t_mods t_options t_romfs_validate t_padmap t_swkbd t_rundiag t_logstamp
run-ubsan: $(U)/t_ubsan_on $(U)/t_ubsan_on_ub $(addprefix $(U)/,$(UBSAN_T))
	$(U)/t_ubsan_on
	@status=0; $(U)/t_ubsan_on_ub > $(U)/t_ubsan_on_ub.log 2>&1 || status=$$?; \
	 if [ "$$status" -eq 0 ] || ! grep -q "runtime error" $(U)/t_ubsan_on_ub.log; then \
	   cat $(U)/t_ubsan_on_ub.log; echo "t_ubsan_on_ub: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_ubsan_on_ub: a signed overflow stops the run with a runtime error, as expected"
	TMPDIR="$(abspath $(U))" $(U)/t_envfile
	TMPDIR="$(abspath $(U))" $(U)/t_cmdchan 2>/dev/null
	$(U)/t_hid_script 2>/dev/null
	TMPDIR="$(abspath $(U))" $(U)/t_mods 2>/dev/null
	TMPDIR="$(abspath $(U))" $(U)/t_options 2>/dev/null
	TMPDIR="$(abspath $(U))" $(U)/t_romfs_validate
	$(U)/t_padmap
	$(U)/t_swkbd
	TMPDIR="$(abspath $(U))" $(U)/t_rundiag
	$(U)/t_logstamp
	@echo "run-ubsan: $(words $(UBSAN_T)) parser tests clean under the undefined-behavior sanitizer"
$(U):
	mkdir -p $@
$(U)/t_ubsan_on: t_ubsan_on.c | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -o $@ t_ubsan_on.c
$(U)/t_ubsan_on_ub: t_ubsan_on.c | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -DDM_TEST_UB -o $@ t_ubsan_on.c
$(U)/t_envfile: t_envfile.c ../envfile.h | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -I.. -o $@ t_envfile.c
$(U)/t_cmdchan: t_cmdchan.c ../front/cmdchan.c ../dm.h | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -I.. -o $@ t_cmdchan.c
$(U)/t_hid_script: t_hid_script.c ../hle/hle_hid.c ../front/touchpad.c ../touchpad.h ../hle.h ../dm.h ../dm_input.h ../front.h | $(U)
	clang -O1 -Wno-unused-function $(UBSAN) -I.. -o $@ t_hid_script.c ../front/touchpad.c
$(U)/t_mods: t_mods.c ../game/mods.c ../dm.h ../game.h | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -I.. -o $@ t_mods.c
$(U)/t_options: t_options.c ../runtime/options.c ../dm.h | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -I.. -o $@ t_options.c
$(U)/t_romfs_validate: t_romfs_validate.c ../runtime/romfs_validate.c | $(U)
	clang -O1 -Wall -Wno-unused-function $(UBSAN) -I.. -o $@ t_romfs_validate.c ../runtime/romfs_validate.c
$(U)/t_padmap: t_padmap.c ../gamepad_map.h ../dm_input.h | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -I.. -o $@ t_padmap.c
$(U)/t_swkbd: t_swkbd.c ../hle/hle_swkbd.c | $(U)
	clang -O1 -Wno-unused-function $(UBSAN) -I.. -o $@ t_swkbd.c ../hle/hle_swkbd.c
$(U)/t_rundiag: t_rundiag.c ../rundiag.h | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -I.. -o $@ t_rundiag.c
$(U)/t_logstamp: t_logstamp.c ../logstamp.h | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -I.. -o $@ t_logstamp.c

$(B)/t_altstack: t_altstack.c t_altstack_stub.c ../runtime/crash.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_altstack.c t_altstack_stub.c ../runtime/crash.c ../runtime/plat_apple.c
$(B)/t_audio: t_audio.c ../hle/hle_audio.c ../hle.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_audio.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation ../runtime/plat_apple.c
$(B)/t_audren_pace: t_audren_pace.c ../audren_pace.h | $(B)
	clang -O1 -Wall -I.. -o $@ t_audren_pace.c
$(B)/t_audren_pace_out: t_audren_pace.c ../audren_pace.h | $(B)
	clang -O1 -Wall -DDM_TEST_AUDREN_PACE_OUTPUT -I.. -o $@ t_audren_pace.c
$(B)/t_audren_pace_sink: t_audren_pace.c ../audren_pace.h | $(B)
	clang -O1 -Wall -DDM_TEST_SINK_NO_RESYNC -I.. -o $@ t_audren_pace.c
$(B)/t_audren: t_audren.c ../hle/hle_audren.c ../audren_pace.h ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_audren.c
$(B)/t_audren_unbounded: t_audren.c ../hle/hle_audren.c ../audren_pace.h ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_AUDREN_UNBOUNDED -I.. -o $@ t_audren.c
$(B)/t_audren_void: t_audren.c ../hle/hle_audren.c ../audren_pace.h ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_ADNOTIFY_VOID -I.. -o $@ t_audren.c
$(B)/t_audio_extras: t_audio_extras.c ../hle/hle_audio.c ../hle.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_audio_extras.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation ../runtime/plat_apple.c
$(B)/t_audio_extras_early: t_audio_extras.c ../hle/hle_audio.c ../hle.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_AUDIO_PUBLISH_EARLY -I.. -o $@ t_audio_extras.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation ../runtime/plat_apple.c
$(B)/t_audio_extras_vol: t_audio_extras.c ../hle/hle_audio.c ../hle.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -DDM_TEST_AUDIO_VOLUME_IGNORED -o $@ t_audio_extras.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation ../runtime/plat_apple.c
$(B)/t_audio_extras_kept: t_audio_extras.c ../hle/hle_audio.c ../hle.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -DDM_TEST_AUDIO_PLAYED_KEPT -o $@ t_audio_extras.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation ../runtime/plat_apple.c
$(B)/t_audio_extras_stopkeeps: t_audio_extras.c ../hle/hle_audio.c ../hle.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -DDM_TEST_AUDIO_STOP_KEEPS -o $@ t_audio_extras.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation ../runtime/plat_apple.c
$(B)/t_audio_spsc: t_audio_spsc.c ../hle/hle_audio.c ../hle.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_audio_spsc.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation ../runtime/plat_apple.c
$(B)/t_audio_spsc_tsan: t_audio_spsc.c ../hle/hle_audio.c ../hle.h ../dm.h | $(B)
	clang -O1 -g -fsanitize=thread -Wno-unused-function -I.. -o $@ t_audio_spsc.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation
$(B)/t_cloudlist: t_cloudlist.m ../cloud/cloud.m ../cloud/cloudsync.c ../cloud.h ../game/game.c $(GEN) ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -fobjc-arc -o $@ t_cloudlist.m ../cloud/cloudsync.c ../game/game.c $(GEN) -framework Foundation -framework Network
$(B)/t_cloudlist_old: t_cloudlist.m ../cloud/cloud.m ../cloud/cloudsync.c ../cloud.h ../game/game.c $(GEN) ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_CLOUD_ALWAYS_DISPATCH -I.. -fobjc-arc -o $@ t_cloudlist.m ../cloud/cloudsync.c ../game/game.c $(GEN) -framework Foundation -framework Network
$(B)/t_cloudsync: t_cloudsync.c ../cloud/cloudsync.c ../cloud.h ../dm.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_cloudsync.c ../cloud/cloudsync.c
$(B)/t_cmdchan: t_cmdchan.c ../front/cmdchan.c ../dm.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_cmdchan.c
$(B)/t_cmdchan_noreopen: t_cmdchan.c ../front/cmdchan.c ../dm.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_CMD_NO_REOPEN -I.. -o $@ t_cmdchan.c
$(B)/t_cmdchan_partial: t_cmdchan.c ../front/cmdchan.c ../dm.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_CMD_PARTIAL -I.. -o $@ t_cmdchan.c
$(B)/t_freeze: t_freeze.c t_altstack_stub.c ../runtime/crash.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_freeze.c t_altstack_stub.c ../runtime/crash.c ../runtime/plat_apple.c
$(B)/t_freeze_none: t_freeze.c t_altstack_stub.c ../runtime/crash.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_FREEZE_NONE -I.. -o $@ t_freeze.c t_altstack_stub.c ../runtime/crash.c ../runtime/plat_apple.c
$(B)/t_threaddump: t_threaddump.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_threaddump.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_threaddump_suspended: t_threaddump.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_HOOKS -DDM_TEST_BT_PRINT_SUSPENDED -I.. -o $@ t_threaddump.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_il2cpp_obj: t_il2cpp_obj.c t_altstack_stub.c ../runtime/crash.c ../il2cpp_obj.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_il2cpp_obj.c t_altstack_stub.c ../runtime/crash.c ../runtime/plat_apple.c
$(B)/t_il2cpp_obj_nocheck: t_il2cpp_obj.c t_altstack_stub.c ../runtime/crash.c ../il2cpp_obj.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_IL2CPP_NO_CHECK -I.. -o $@ t_il2cpp_obj.c t_altstack_stub.c ../runtime/crash.c ../runtime/plat_apple.c
$(B)/t_crash_bt: t_crash_bt.c t_altstack_stub.c ../runtime/crash.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_crash_bt.c t_altstack_stub.c ../runtime/crash.c ../runtime/plat_apple.c
$(B)/t_crash_bt_unguarded: t_crash_bt.c t_altstack_stub.c ../runtime/crash.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_BT_UNGUARDED -I.. -o $@ t_crash_bt.c t_altstack_stub.c ../runtime/crash.c ../runtime/plat_apple.c
$(B)/t_gate_svc: t_gate_svc.c ../runtime/loader.c ../hle/hle.c ../runtime/options.c ../dm.h ../hle.h | $(B)
	clang -O1 -Wno-deprecated-declarations -Wno-unused-function -I.. -Wl,-dead_strip -o $@ t_gate_svc.c ../hle/hle.c ../runtime/options.c
$(B)/t_gate_svc_rel: t_gate_svc.c ../runtime/loader.c ../hle/hle.c ../runtime/options.c ../dm.h ../hle.h | $(B)
	clang -O1 -Wno-deprecated-declarations -Wno-unused-function -DDM_TEST_GATE_ORIG_REL -I.. -Wl,-dead_strip -o $@ t_gate_svc.c ../hle/hle.c ../runtime/options.c
$(B)/t_mouse_route: t_mouse_route.c ../mouse_route.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_mouse_route.c
$(B)/t_mouse_route_swallow: t_mouse_route.c ../mouse_route.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_MOUSE_SWALLOW_TITLE -I.. -o $@ t_mouse_route.c
$(B)/t_relr: t_relr.c ../runtime/loader.c ../hle/hle.c ../runtime/options.c ../dm.h ../hle.h | $(B)
	clang -O1 -Wno-deprecated-declarations -Wno-unused-function -I.. -Wl,-dead_strip -o $@ t_relr.c
$(B)/t_relr_unchecked: t_relr.c ../runtime/loader.c ../hle/hle.c ../runtime/options.c ../dm.h ../hle.h | $(B)
	clang -O1 -Wno-deprecated-declarations -Wno-unused-function -DDM_TEST_RELR_UNCHECKED -I.. -Wl,-dead_strip -o $@ t_relr.c
$(B)/t_tzrule: t_tzrule.c ../hle/hle_time.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_tzrule.c -framework CoreFoundation
$(B)/t_tzrule_dev: t_tzrule.c ../hle/hle_time.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_TZRULE_DEVICE -I.. -o $@ t_tzrule.c -framework CoreFoundation
$(B)/t_time: t_time.c ../hle/hle_time.c ../hle.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_time.c -framework CoreFoundation ../runtime/plat_apple.c
$(B)/t_time_one: t_time.c ../hle/hle_time.c ../hle.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_TOPOSIX_ONE -I.. -o $@ t_time.c -framework CoreFoundation ../runtime/plat_apple.c
$(B)/t_time_ctxold: t_time.c ../hle/hle_time.c ../hle.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_CTX_OLD -I.. -o $@ t_time.c -framework CoreFoundation ../runtime/plat_apple.c
$(B)/t_time_span: t_time.c ../hle/hle_time.c ../hle.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_SPAN_RESULT -I.. -o $@ t_time.c -framework CoreFoundation ../runtime/plat_apple.c
$(B)/t_watchdog: t_watchdog.c t_altstack_stub.c ../runtime/crash.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_watchdog.c t_altstack_stub.c ../runtime/crash.c ../runtime/plat_apple.c
$(B)/t_watchdog_nobg: t_watchdog.c t_altstack_stub.c ../runtime/crash.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_WATCH_NO_BG -I.. -o $@ t_watchdog.c t_altstack_stub.c ../runtime/crash.c ../runtime/plat_apple.c
$(B)/t_crash_stderr: t_crash_stderr.c t_altstack_stub.c ../runtime/crash.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_crash_stderr.c t_altstack_stub.c ../runtime/crash.c ../runtime/plat_apple.c
$(B)/t_crash_stderr_nodeadman: t_crash_stderr.c t_altstack_stub.c ../runtime/crash.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_CRASH_NO_DEADMAN -I.. -o $@ t_crash_stderr.c t_altstack_stub.c ../runtime/crash.c ../runtime/plat_apple.c
$(B)/t_crash_stderr_alrm: t_crash_stderr.c t_altstack_stub.c ../runtime/crash.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_CRASH_ALRM_BLOCKED -I.. -o $@ t_crash_stderr.c t_altstack_stub.c ../runtime/crash.c ../runtime/plat_apple.c
$(B)/t_crash_stderr_counted: t_crash_stderr.c t_altstack_stub.c ../runtime/crash.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_CRASH_COUNTED_WAIT -I.. -o $@ t_crash_stderr.c t_altstack_stub.c ../runtime/crash.c ../runtime/plat_apple.c
$(B)/t_arbiter: t_arbiter.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_arbiter.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_padmap: t_padmap.c ../gamepad_map.h ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_padmap.c
$(B)/t_pchtxt: t_pchtxt.c ../game/pchtxt.c ../dm.h | $(B)
	clang -O1 -Wall -I.. -o $@ t_pchtxt.c
$(B)/t_pchtxt_nocheck: t_pchtxt.c ../game/pchtxt.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_PCHTXT_NO_CHECK -I.. -o $@ t_pchtxt.c
$(B)/t_pchtxt_oldflags: t_pchtxt.c ../game/pchtxt.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_PCHTXT_OLD_FLAGS -I.. -o $@ t_pchtxt.c
$(B)/t_ns_text: t_ns_text.m ../ns_text.h | $(B)
	clang -O1 -Wall -fobjc-arc -I.. -o $@ t_ns_text.m -framework Foundation
$(B)/t_ns_text_default: t_ns_text.m ../ns_text.h | $(B)
	clang -O1 -Wall -fobjc-arc -DDM_TEST_NS_TEXT_DEFAULT_ENCODING -I.. -o $@ t_ns_text.m -framework Foundation
$(B)/t_cheat_vm: t_cheat_vm.c ../game/cheat_vm.c ../cheat_vm.h | $(B)
	clang -O1 -Wall -I.. -o $@ t_cheat_vm.c
$(B)/t_cheat_vm_flat: t_cheat_vm.c ../game/cheat_vm.c ../cheat_vm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_CHEAT_FLAT_SKIP -I.. -o $@ t_cheat_vm.c
$(B)/t_cheats: t_cheats.c ../game/cheats.c ../game/cheat_vm.c ../cheat_vm.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_cheats.c ../game/cheat_vm.c ../runtime/plat_apple.c
$(B)/t_cheats_norw: t_cheats.c ../game/cheats.c ../game/cheat_vm.c ../cheat_vm.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_CHEATS_NO_REWRITE_CHECK -I.. -o $@ t_cheats.c ../game/cheat_vm.c ../runtime/plat_apple.c
$(B)/t_padmap_posall: t_padmap.c ../gamepad_map.h ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_FACE_POS_ALL -I.. -o $@ t_padmap.c
$(B)/t_codesig: t_codesig.c ../../ios/Sources/codesig.h | $(B)
	clang -O1 -Wall -Werror -o $@ t_codesig.c
	printf '<?xml version="1.0" encoding="UTF-8"?>\n<plist version="1.0"><dict><key>com.apple.developer.kernel.increased-memory-limit</key>\n\t<true/></dict></plist>\n' > $(B)/ent_true.plist
	printf '<?xml version="1.0" encoding="UTF-8"?>\n<plist version="1.0"><dict><key>com.apple.developer.kernel.increased-memory-limit</key><false/></dict></plist>\n' > $(B)/ent_false.plist
	cp $@ $(B)/t_codesig_true && codesign -s - -f --entitlements $(B)/ent_true.plist $(B)/t_codesig_true 2>/dev/null
	cp $@ $(B)/t_codesig_false && codesign -s - -f --entitlements $(B)/ent_false.plist $(B)/t_codesig_false 2>/dev/null
$(B)/t_mano_display: t_mano_display.c ../../games/manosaba/graphics.c ../dm.h ../game.h ../hle.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_mano_display.c
$(B)/t_mano_follow: t_mano_follow.c ../../games/manosaba/graphics.c ../native_size.h ../dm.h ../game.h ../hle.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_mano_follow.c
$(B)/t_mano_follow_boot: t_mano_follow.c ../../games/manosaba/graphics.c ../native_size.h ../dm.h ../game.h ../hle.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_FOLLOW_BOOT_HEIGHT -I.. -o $@ t_mano_follow.c
$(B)/t_mano_display_const: t_mano_display.c ../../games/manosaba/graphics.c ../dm.h ../game.h ../hle.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_DISPLAY_CONST -I.. -o $@ t_mano_display.c
$(B)/t_codesig_broken: t_codesig.c ../../ios/Sources/codesig.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_CODESIG_ANY_VALUE -o $@ t_codesig.c
$(B)/t_sead: t_sead.c ../diag/sead.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_sead.c ../runtime/plat_apple.c
$(B)/t_sead_broken: t_sead.c ../diag/sead.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_SEAD_BROKEN -I.. -o $@ t_sead.c ../runtime/plat_apple.c
$(B)/t_sleep: t_sleep.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_sleep.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_envfile: t_envfile.c ../envfile.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_envfile.c
$(B)/t_native_size: t_native_size.c ../native_size.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_native_size.c
$(B)/t_fit_rect: t_fit_rect.c ../fit_rect.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_fit_rect.c
$(B)/t_fit_rect_ignore: t_fit_rect.c ../fit_rect.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_FIT_IGNORE_AVOID -I.. -o $@ t_fit_rect.c
$(B)/t_native_size_long: t_native_size.c ../native_size.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_NATIVE_LONG_SIDE -I.. -o $@ t_native_size.c
$(B)/t_native_size_unclamped: t_native_size.c ../native_size.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_NATIVE_FIT_UNCLAMPED -I.. -o $@ t_native_size.c
$(B)/t_gameslot: t_gameslot.c ../game/game_slot.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-result -I.. -o $@ t_gameslot.c ../game/game_slot.c
$(B)/t_ktrace: t_ktrace.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_ktrace.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_loader_weak: t_loader_weak.c ../runtime/loader.c ../hle/hle.c ../runtime/options.c ../dm.h ../hle.h ../rodata_seal.h | $(B)
	clang -O1 -Wno-deprecated-declarations -I.. -Wl,-dead_strip -o $@ t_loader_weak.c ../hle/hle.c ../runtime/options.c
$(B)/t_rtld: t_rtld.c ../runtime/loader.c ../hle/hle.c ../runtime/options.c ../dm.h ../hle.h ../rodata_seal.h | $(B)
	clang -O1 -Wno-deprecated-declarations -I.. -Wl,-dead_strip -o $@ t_rtld.c ../hle/hle.c ../runtime/options.c
$(B)/t_rtld_ours: t_rtld.c ../runtime/loader.c ../hle/hle.c ../runtime/options.c ../dm.h ../hle.h ../rodata_seal.h | $(B)
	clang -O1 -Wno-deprecated-declarations -I.. -Wl,-dead_strip -DDM_TEST_RTLD_DEFAULT_OURS -o $@ t_rtld.c ../hle/hle.c ../runtime/options.c
$(B)/t_rtld_order: t_rtld.c ../runtime/loader.c ../hle/hle.c ../runtime/options.c ../dm.h ../hle.h ../rodata_seal.h | $(B)
	clang -O1 -Wno-deprecated-declarations -I.. -Wl,-dead_strip -DDM_TEST_RTLD_LOAD_ORDER -o $@ t_rtld.c ../hle/hle.c ../runtime/options.c
$(B)/t_mods: t_mods.c ../game/mods.c ../dm.h ../game.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_mods.c
$(B)/t_mods_case: t_mods.c ../game/mods.c ../dm.h ../game.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_MODS_CASE -I.. -o $@ t_mods.c
$(B)/t_mods_nopin: t_mods.c ../game/mods.c ../dm.h ../game.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_MODS_NO_PIN -I.. -o $@ t_mods.c
$(B)/t_thermal: t_thermal.m ../diag/thermal.m | $(B)
	clang -O1 -Wall -fobjc-arc -o $@ t_thermal.m ../diag/thermal.m -framework Foundation
$(B)/t_thermal_weak: t_thermal.m | $(B)
	clang -O1 -Wall -fobjc-arc -DDM_TEST_THERMAL_WEAK_ONLY -o $@ t_thermal.m -framework Foundation
$(B)/t_logstamp: t_logstamp.c ../logstamp.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_logstamp.c
$(B)/t_logstamp_every: t_logstamp.c ../logstamp.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_LOGSTAMP_EVERY_WRITE -I.. -o $@ t_logstamp.c
$(B)/t_memsnap: t_memsnap.c ../diag/memsnap.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-result -I.. -o $@ t_memsnap.c ../diag/memsnap.c ../runtime/plat_apple.c
$(B)/t_sysevent: t_sysevent.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -DT_OWN_GUEST_SYM -I.. -o $@ t_sysevent.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_threadcpu: t_threadcpu.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_threadcpu.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_waitsync: t_waitsync.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_waitsync.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_condvar: t_condvar.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_condvar.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_condvar_old: t_condvar.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -DDM_TEST_CV_OLD_ORDER -I.. -o $@ t_condvar.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_fs: t_fs.c t_fs_stub.c ../hle/hle_fs.c ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_fs.c t_fs_stub.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_fs_strict: t_fs.c t_fs_stub.c ../hle/hle_fs.c ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_PATH_STRICT=1 -I.. -o $@ t_fs.c t_fs_stub.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_fs_saveyes: t_fs.c t_fs_stub.c ../hle/hle_fs.c ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_SAVE_ALWAYS=1 -I.. -o $@ t_fs.c t_fs_stub.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_fs_nopin: t_fs.c t_fs_stub.c ../hle/hle_fs.c ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_ROM_NO_PIN=1 -I.. -o $@ t_fs.c t_fs_stub.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_fs_noreprep: t_fs.c t_fs_stub.c ../hle/hle_fs.c ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_FS_NO_REPREP=1 -I.. -o $@ t_fs.c t_fs_stub.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_fs_kill: t_fs_kill.c t_fs_stub.c ../hle/hle_fs.c ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_fs_kill.c t_fs_stub.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_fs_kill_inplace: t_fs_kill.c t_fs_stub.c ../hle/hle_fs.c ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_FS_COMMIT_INPLACE -I.. -o $@ t_fs_kill.c t_fs_stub.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_fs_index: t_fs_index.c ../hle/hle_fs.c ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_fs_index.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_fs_core: t_fs_core.c t_fs_core_stub.c ../hle/hle_fs.c ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_fs_core.c t_fs_core_stub.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_fs_core_kept: t_fs_core.c t_fs_core_stub.c ../hle/hle_fs.c ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_FS_KEEPS_CORE -I.. -o $@ t_fs_core.c t_fs_core_stub.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_fs_case: t_fs_case.c ../hle/hle_fs.c ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_fs_case.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_guest_mem: t_guest_mem.c ../hle/hle_svc.c ../rodata_seal.h ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_guest_mem.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_guest_mem_getinfoold: t_guest_mem.c ../hle/hle_svc.c ../rodata_seal.h ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_GETINFO_OLD -I.. -o $@ t_guest_mem.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_guest_mem_heapold: t_guest_mem.c ../hle/hle_svc.c ../rodata_seal.h ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HEAP_OLD -I.. -o $@ t_guest_mem.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_guest_mem_physold: t_guest_mem.c ../hle/hle_svc.c ../rodata_seal.h ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_PHYS_OLD -I.. -o $@ t_guest_mem.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_guest_mem_perpage: t_guest_mem.c ../hle/hle_svc.c ../rodata_seal.h ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_ALIAS_PER_PAGE -I.. -o $@ t_guest_mem.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_guest_mem_arold: t_guest_mem.c ../hle/hle_svc.c ../rodata_seal.h ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_AR_UNLOGGED -I.. -o $@ t_guest_mem.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_guest_mem_unnoted: t_guest_mem.c ../hle/hle_svc.c ../rodata_seal.h ../hle.h ../dm.h ../game/game.c $(GEN) ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_UNMAP_UNNOTED -I.. -o $@ t_guest_mem.c ../game/game.c $(GEN) ../runtime/plat_apple.c
$(B)/t_thrpub: t_thrpub.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_thrpub.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_thrpub_early: t_thrpub.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -DDM_TEST_THREAD_LINK_EARLY -I.. -o $@ t_thrpub.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_thrpub_prio: t_thrpub.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -DDM_TEST_PRIO_UNLOCKED -I.. -o $@ t_thrpub.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_coremask: t_coremask.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_coremask.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_coremask_noproc: t_coremask.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -DDM_TEST_CORE_NO_PROCMASK -I.. -o $@ t_coremask.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_mainqos: t_mainqos.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_mainqos.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_mainqos_ignored: t_mainqos.c t_condvar_stub.c ../hle/hle_thread.c ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -DDM_TEST_MAIN_QOS_IGNORED -I.. -o $@ t_mainqos.c t_condvar_stub.c ../hle/hle_thread.c ../runtime/plat_apple.c
$(B)/t_totk_defaults: t_totk_defaults.c ../../games/totk/defaults.c ../game.h | $(B)
	clang -O1 -Wall -I.. -o $@ t_totk_defaults.c
$(B)/t_totk_defaults_noxlat: t_totk_defaults.c ../../games/totk/defaults.c ../game.h | $(B)
	clang -O1 -Wall -DDM_TEST_TOTK_NO_XLAT -I.. -o $@ t_totk_defaults.c
$(B)/t_totk_defaults_tbdr: t_totk_defaults.c ../../games/totk/defaults.c ../game.h | $(B)
	clang -O1 -Wall -DDM_TEST_TOTK_TBDR_ON -I.. -o $@ t_totk_defaults.c
$(B)/t_haptics: t_haptics.c ../haptics_map.h | $(B)
	clang -O1 -Wall -I.. -o $@ t_haptics.c
$(B)/t_haptics_nogap: t_haptics.c ../haptics_map.h | $(B)
	clang -O1 -Wall -I.. -DDM_TEST_PULSE_NO_GAP -o $@ t_haptics.c
$(B)/t_haptics_anyfocus: t_haptics.c ../haptics_map.h | $(B)
	clang -O1 -Wall -I.. -DDM_TEST_PHONE_ENG_ANY_FOCUS -o $@ t_haptics.c
$(B)/t_procon: t_procon.c ../procon.h | $(B)
	clang -O1 -Wall -I.. -o $@ t_procon.c
$(B)/t_procon_raw: t_procon.c ../procon.h | $(B)
	clang -O1 -Wall -I.. -DDM_TEST_PROCON_RAW_AXES -o $@ t_procon.c
$(B)/t_aspect: t_aspect.c ../game/aspect.c ../aspect.h ../dm.h | $(B)
	clang -O1 -Wall -I.. -o $@ t_aspect.c
$(B)/t_aspect_nofollow: t_aspect.c ../game/aspect.c ../aspect.h ../dm.h | $(B)
	clang -O1 -Wall -I.. -DDM_TEST_ASPECT_NO_FOLLOW -o $@ t_aspect.c
MOTION_DEPS = t_motion.c ../hle/hle_hid.c ../hle/hle_motion.c ../hle.h ../dm.h ../dm_input.h ../front.h
$(B)/t_motion: $(MOTION_DEPS) | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_motion.c
$(B)/t_motion_allstyles: $(MOTION_DEPS) | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_VIB_ALL_STYLES -I.. -o $@ t_motion.c
$(B)/t_motion_nobias: $(MOTION_DEPS) | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_SIX_NO_BIAS -I.. -o $@ t_motion.c
$(B)/t_motion_nograv: $(MOTION_DEPS) | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_SIX_NO_GRAVITY -I.. -o $@ t_motion.c
$(B)/t_motion_nopeak: $(MOTION_DEPS) | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_VIB_NO_PEAK -I.. -o $@ t_motion.c
$(B)/t_hid_applet: t_hid_applet.c ../hle/hle_hid.c ../hle/hle_account.c ../hle.h ../dm.h ../dm_input.h ../front.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_hid_applet.c
$(B)/t_hid_applet_silent: t_hid_applet.c ../hle/hle_hid.c ../hle/hle_account.c ../hle.h ../dm.h ../dm_input.h ../front.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_CTLSUPPORT_SILENT -I.. -o $@ t_hid_applet.c
$(B)/t_hid_receipt: t_hid_receipt.c ../hle/hle_hid.c ../hle.h ../dm.h ../dm_input.h ../front.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_hid_receipt.c
$(B)/t_hid_receipt_oneslot: t_hid_receipt.c ../hle/hle_hid.c ../hle.h ../dm.h ../dm_input.h ../front.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_RECEIPT_ONE_SLOT -I.. -o $@ t_hid_receipt.c
$(B)/t_hid_script: t_hid_script.c ../hle/hle_hid.c ../front/touchpad.c ../touchpad.h ../hle.h ../dm.h ../dm_input.h ../front.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_hid_script.c ../front/touchpad.c
$(B)/t_hid_script_nocircle: t_hid_script.c ../hle/hle_hid.c ../front/touchpad.c ../touchpad.h ../hle.h ../dm.h ../dm_input.h ../front.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HID_NO_CIRCLE -I.. -o $@ t_hid_script.c ../front/touchpad.c
$(B)/t_hid_script_norstick: t_hid_script.c ../hle/hle_hid.c ../front/touchpad.c ../touchpad.h ../hle.h ../dm.h ../dm_input.h ../front.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HID_NO_RSTICK -I.. -o $@ t_hid_script.c ../front/touchpad.c
# VP9 movies need libvpx (brew install libvpx), as the host build does (host/Makefile VPX)
VPX ?= /opt/homebrew/opt/libvpx
$(B)/t_movie: t_movie.c ../hle/hle_movie.c ../runtime/hstack.S ../runtime/hstack.c ../media/movie_av.m ../movie_av.h ../media/movie_webm.c ../movie_webm.h ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -isystem $(VPX)/include -fobjc-arc -o $@ t_movie.c ../media/movie_av.m ../media/movie_webm.c ../runtime/hstack.S ../runtime/hstack.c $(VPX)/lib/libvpx.a \
		-framework Foundation -framework AVFoundation -framework CoreMedia -framework CoreVideo
$(B)/t_movie_evcopy: t_movie.c ../hle/hle_movie.c ../runtime/hstack.S ../runtime/hstack.c ../media/movie_av.m ../movie_av.h ../media/movie_webm.c ../movie_webm.h ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -isystem $(VPX)/include -fobjc-arc -DDM_TEST_EVENTS_COPIED -o $@ t_movie.c ../media/movie_av.m ../media/movie_webm.c ../runtime/hstack.S ../runtime/hstack.c $(VPX)/lib/libvpx.a \
		-framework Foundation -framework AVFoundation -framework CoreMedia -framework CoreVideo
$(B)/t_touchpad: t_touchpad.c ../front/touchpad.c ../touchpad.h ../dm.h ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_touchpad.c
$(B)/t_touchpad_rehit: t_touchpad.c ../front/touchpad.c ../touchpad.h ../dm.h ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_TP_REHIT -I.. -o $@ t_touchpad.c
$(B)/t_touchpad_sensafter: t_touchpad.c ../front/touchpad.c ../touchpad.h ../dm.h ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_TP_SENS_AFTER -I.. -o $@ t_touchpad.c
$(B)/t_touchpad_custom: t_touchpad.c ../front/touchpad.c ../touchpad.h ../dm.h ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_TP_CUSTOM_IGNORED -I.. -o $@ t_touchpad.c
$(B)/t_touchpad_nowin: t_touchpad.c ../front/touchpad.c ../touchpad.h ../dm.h ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_TP_NO_WINDOW -I.. -o $@ t_touchpad.c
$(B)/t_touchpad_ctlconnect: t_touchpad.c ../front/touchpad.c ../touchpad.h ../dm.h ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_TP_CTL_ON_CONNECT -I.. -o $@ t_touchpad.c
$(B)/t_options: t_options.c ../runtime/options.c ../dm.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_options.c
$(B)/t_options_nocheck: t_options.c ../runtime/options.c ../dm.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_OPTIONS_NO_CHECK -I.. -o $@ t_options.c
$(B)/t_net: t_net.c ../hle/hle_net.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_net.c
$(B)/t_net_bsd: t_net.c ../hle/hle_net.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_NET_BSD_ERRNO -I.. -o $@ t_net.c
$(B)/t_oe: t_oe.c ../hle/hle_oe.c ../hle.h ../dm.h ../game/game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_oe.c ../game/game.c $(GEN)
$(B)/t_oe_unlocked: t_oe.c ../hle/hle_oe.c ../hle.h ../dm.h ../game/game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -DDM_TEST_OE_EVENT_UNLOCKED -I.. -o $@ t_oe.c ../game/game.c $(GEN)
$(B)/t_oe_noios: t_oe.c ../hle/hle_oe.c ../hle.h ../dm.h ../game/game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -DDM_TEST_OPMODE_NO_IOS -I.. -o $@ t_oe.c ../game/game.c $(GEN)
$(B)/t_oe_noop: t_oe.c ../hle/hle_oe.c ../hle.h ../dm.h ../game/game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -DDM_TEST_INACTIVITY_NOOP -I.. -o $@ t_oe.c ../game/game.c $(GEN)
$(B)/t_rng_font: t_rng_font.c ../hle/hle_os.c ../hle/hle_pl.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_rng_font.c
$(B)/t_rng_font_rev: t_rng_font.c ../hle/hle_os.c ../hle/hle_pl.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -DDM_TEST_FONT_REVERSED -o $@ t_rng_font.c
$(B)/t_bfttf: t_bfttf.c ../hle/hle_os.c ../hle/hle_pl.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_bfttf.c
$(B)/t_bfttf_one: t_bfttf.c ../hle/hle_os.c ../hle/hle_pl.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -DDM_TEST_BFTTF_ONE_ORDER -o $@ t_bfttf.c
$(B)/t_shadercache: t_shadercache.c ../shadercache.h | $(B)
	clang -O1 -Wall -I.. -o $@ t_shadercache.c
$(B)/t_shadercache_metal: t_shadercache.c ../shadercache.h | $(B)
	clang -O1 -Wall -I.. -DDM_TEST_SHADERCACHE_METAL_ONLY -o $@ t_shadercache.c
$(B)/t_schedstats: t_schedstats.c ../schedstats.h | $(B)
	clang -O1 -Wall -I.. -o $@ t_schedstats.c
$(B)/t_schedstats_start: t_schedstats.c ../schedstats.h | $(B)
	clang -O1 -Wall -I.. -DDM_TEST_SCHED_WAKE_FROM_START -o $@ t_schedstats.c
$(B)/t_coolstart: t_coolstart.c ../coolstart.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_coolstart.c
$(B)/t_coolstart_onelook: t_coolstart.c ../coolstart.h | $(B)
	clang -O1 -Wall -Werror -I.. -DDM_TEST_COOL_ONE_LOOK -o $@ t_coolstart.c
$(B)/t_romfs_parts: t_romfs_parts.c ../runtime/romfs_parts.c ../romfs_parts.h ../../tools/romfs_parts.py | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_romfs_parts.c
$(B)/t_romfs_parts_nosha: t_romfs_parts.c ../runtime/romfs_parts.c ../romfs_parts.h | $(B)
	clang -O1 -Wall -Werror -I.. -DDM_TEST_PARTS_NO_SHA -o $@ t_romfs_parts.c
$(B)/t_romfs_parts_anypath: t_romfs_parts.c ../runtime/romfs_parts.c ../romfs_parts.h | $(B)
	clang -O1 -Wall -Werror -I.. -DDM_TEST_PARTS_ANY_PATH -o $@ t_romfs_parts.c
$(B)/t_guest_as: t_guest_as.c ../guest_as.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_guest_as.c ../runtime/plat_apple.c
$(B)/t_guest_as_nolimit: t_guest_as.c ../guest_as.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Werror -I.. -DDM_TEST_AS_NO_LIMIT -o $@ t_guest_as.c ../runtime/plat_apple.c
$(B)/t_lang: t_lang.c ../lang.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_lang.c
$(B)/t_lang_region: t_lang.c ../lang.h | $(B)
	clang -O1 -Wall -Werror -I.. -DDM_TEST_LANG_REGION_IGNORED -o $@ t_lang.c
$(B)/t_lang_regfirst: t_lang.c ../lang.h | $(B)
	clang -O1 -Wall -Werror -I.. -DDM_TEST_LANG_REGION_FIRST -o $@ t_lang.c
$(B)/t_survey: ../../games/totk/tests/t_survey.c ../../games/totk/survey.c ../dm.h ../game.h | $(B)
	clang -O1 -Wall -I.. -o $@ ../../games/totk/tests/t_survey.c
$(B)/t_survey_cols: ../../games/totk/tests/t_survey.c ../../games/totk/survey.c ../dm.h ../game.h | $(B)
	clang -O1 -Wall -I.. -DDM_TEST_SURVEY_COLUMNS -o $@ ../../games/totk/tests/t_survey.c
$(B)/t_movie_fps: ../../games/totk/tests/t_movie_fps.c ../../games/totk/movie_fps.c ../dm.h ../game.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ ../../games/totk/tests/t_movie_fps.c
$(B)/t_movie_fps_any: ../../games/totk/tests/t_movie_fps.c ../../games/totk/movie_fps.c ../dm.h ../game.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_MOVIE_ANY_CUTSCENE -I.. -o $@ ../../games/totk/tests/t_movie_fps.c
$(B)/t_optref: ../../games/totk/tests/t_optref.c ../../games/totk/optref.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ ../../games/totk/tests/t_optref.c
$(B)/t_optref_one: ../../games/totk/tests/t_optref.c ../../games/totk/optref.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_OPTREF_ONE_VALUE -I.. -o $@ ../../games/totk/tests/t_optref.c
$(B)/t_quitwait: t_quitwait.c ../hle/hle_lifecycle.c ../hle.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_quitwait.c ../runtime/plat_apple.c
$(B)/t_quitwait_old: t_quitwait.c ../hle/hle_lifecycle.c ../hle.h ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_QUIT_NO_SECTION -I.. -o $@ t_quitwait.c ../runtime/plat_apple.c
$(B)/t_romfs_validate: t_romfs_validate.c ../runtime/romfs_validate.c ../romfs_validate.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_romfs_validate.c ../runtime/romfs_validate.c
$(B)/t_romfs_validate_nokeys: t_romfs_validate.c ../runtime/romfs_validate.c ../romfs_validate.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_ROMFS_MARKER_NO_KEYS -I.. -o $@ t_romfs_validate.c ../runtime/romfs_validate.c
$(B)/t_romfs_validate_anyorig: t_romfs_validate.c ../runtime/romfs_validate.c ../romfs_validate.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_OVERLAY_ANY_ORIGINAL -I.. -o $@ t_romfs_validate.c ../runtime/romfs_validate.c
$(B)/t_rundiag: t_rundiag.c ../rundiag.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_rundiag.c
$(B)/t_rundiag_nokeep: t_rundiag.c ../rundiag.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_RUNDIAG_NO_KEEP -I.. -o $@ t_rundiag.c
$(B)/t_rundiag_head: t_rundiag.c ../rundiag.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_RUNDIAG_HEAD -I.. -o $@ t_rundiag.c
$(B)/t_rundiag_noredact: t_rundiag.c ../rundiag.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_RUNDIAG_NO_REDACT -I.. -o $@ t_rundiag.c
$(B)/t_stickdir: t_stickdir.c ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_stickdir.c
$(B)/t_stickdir_none: t_stickdir.c ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_STICK_NO_DIRS -I.. -o $@ t_stickdir.c
$(B)/t_swkbd: t_swkbd.c ../hle/hle_swkbd.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_swkbd.c ../hle/hle_swkbd.c
T_THREAD_EXIT_SRC = t_thread_exit.c t_thread_exit_guest.S ../hle/hle_thread.c ../runtime/thread.c ../runtime/entry.S ../runtime/hstack.S ../runtime/hstack.c
# The pre-fix exit only reaches the guest stack with host calls on it: t_thread_exit_old runs with DM_HSTACK=0
T_HSTACK_SRC = t_hstack.c t_hstack_guest.S ../hle/hle_thread.c ../runtime/thread.c ../runtime/entry.S ../runtime/hstack.S ../runtime/hstack.c ../hle/hle_table.c
$(B)/t_hstack: $(T_HSTACK_SRC) ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -g -Wall -Wno-unused-function -I.. -o $@ $(T_HSTACK_SRC) ../runtime/plat_apple.c
$(B)/t_hstack_old: $(T_HSTACK_SRC) ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -g -Wall -Wno-unused-function -DDM_TEST_HSTACK_OLD_CONTEXT -I.. -o $@ $(T_HSTACK_SRC) ../runtime/plat_apple.c
$(B)/t_hstack_inpatch: $(T_HSTACK_SRC) ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -g -Wall -Wno-unused-function -DDM_TEST_STOP_IN_PATCH -I.. -o $@ $(T_HSTACK_SRC) ../runtime/plat_apple.c
$(B)/t_thread_exit: $(T_THREAD_EXIT_SRC) ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -g -Wall -Wno-unused-function -I.. -o $@ $(T_THREAD_EXIT_SRC) ../runtime/plat_apple.c
$(B)/t_thread_exit_old: $(T_THREAD_EXIT_SRC) ../dm.h ../runtime/plat_apple.c | $(B)
	clang -O1 -g -Wall -Wno-unused-function -DDM_TEST_EXIT_ON_GUEST_STACK -I.. -o $@ $(T_THREAD_EXIT_SRC) ../runtime/plat_apple.c
$(B)/t_thread_leak: t_thread_leak.c ../runtime/thread.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_thread_leak.c
$(B)/t_touch: t_touch.c ../hle/hle_touch.c ../front.h ../dm.h ../game.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_touch.c ../hle/hle_touch.c

$(B)/t_pwrspec: t_pwrspec.c ../pwrspec.h | $(B)
	clang -O1 -Wall -I.. -o $@ t_pwrspec.c
$(B)/t_pwrspec_noduty: t_pwrspec.c ../pwrspec.h | $(B)
	clang -O1 -Wall -DDM_TEST_PWRSPEC_NO_DUTY -I.. -o $@ t_pwrspec.c

$(B):
	mkdir -p $@
tsan: $(B)/t_audio_spsc_tsan
	$(B)/t_audio_spsc_tsan
.PHONY: run tsan
