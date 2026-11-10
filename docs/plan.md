# Plan: what is left

The open plan items, by what each needs (2026-11-11). An entry holds only what is left; what was done for an item is in
[plan-done.md](plan-done.md) under the same number, the state of the ports in [DEVELOPMENT.md](../DEVELOPMENT.md#where-the-work-stands-2026-11-11).
A new item takes the next number (327). How these two files are kept: [DEVELOPMENT.md § Testing](../DEVELOPMENT.md#testing).

## The iPhone Air (the test phone)

- 280 · manosaba's trial peak on the phone with the depth targets without memory (`work/scripts/mano_trial_dev.sh`; 3240 MB of RSS before).
- 71b · the fps line's on-screen count after the app has been in the background; the Metal 4 screen (`DM_TEST_NO_METAL4=1` in env.txt); the
  backup exclusion in Settings.
- 219 · the store decisions and the same-target continuation against the default, in turn, three runs each (one run each so far).
- 285 · Tears of the Kingdom on Great Sky Island off USB power (every run so far was on it).
- 310 · sound mixed with other apps' (acnh-mobile 8dcd00c: crackles; the user plays music while manosaba runs); memory touched before loading
  after other apps have filled the phone's memory (nxnative-mkw b8f81e4).
- 153 · the latency default (`DM_PRESENT_LATENCY=auto`) on a device. 148 · the settings panel on a device.
- 317a · whether a free-account install receives MetricKit's payloads.
- 281 · Tears of the Kingdom above 1080 on iOS: the heap overlay of `games/totk/heapdef.py`, a larger application pool than `ios_pool_add_mb` 0
  and a heap region above `guest_heap_mb` 2176 (none tried on a device).
- 321 · once its cost is the critical path (below): the frame step's rules measured again, for `DM_FRAME_STEP=vsync` as the iOS default.

## The Mac alone

- 264 · the next parts of the Metal backend as translation units of their own: the presentation layer (present, present_thread, shot) as one;
  then live, uniform_rename, chunks, u8_layers. The parts the draw reads at every call stay in the main unit.
- 321 · the frame step's cost as the critical path (submits' start to the GPU's end; S2Arm: 27 against 35 ms at a hold of 3).
- 271 · CPU per draw: what the profile still shows is small (pool lookups 6.4 % of the translation thread, the snapshots' lookup 3.7 %). Not
  taken so far: 4ppp44 cb21bec / d0d3cb1 (argument blocks reused across a submission's passes, the driver segment filled only where read).
- 220 · MetalFX frame interpolation: with no motion target the camera's motion would come from the depth and two frames' matrices, and moving
  things would ghost.

## The user at the Mac

- 97 · a present with a deadline against the GPU clock drop: a window and `powermetrics` (the password).
- 127 · the window checks by hand: the title bar's drag, the bars.

## The user's choices

- 192 · touch controls after Touchable (its 8-way D-pad, Liquid Glass, face-button slides and layout; the stick, the camera drag and opacity
  are open).
- 221 · fewer triangles through the game's own LOD and draw distance, picture against frame rate (MacNativeGames, facts: LOD by 12 view-depth
  sites, main+0x1ee8424 and copies; far plane main+0x628c30; FOV load main+0x628c3c; sun shadow map size main+0x683604).
- 133 · `DM_FRAME_STEP=vsync` as iOS's default (35 % less CPU than the mean step on the Mac).
- 280 · manosaba's characters at fewer pixels per unit in the RomFS's `resources.assets` (PR #2's `patch_character_ppu.py`: 1500x3000 →
  1125x2250): an asset change.

## A save, play or the console

- 94a · console footage of Link close to the camera and of the island's grass, and what the RG8 256x256 texture the CPU rewrites is; then
  `DM_NVN_WB_IDLE=1` by default or a narrower rule.
- 132 · a save where the game draws the wave program's wide lines (totk-mobile expands them by editing its shaders).
- 71c · product60's 30 fps systems in play: stamina, arrow speed, shield surfing, rune aiming, flurry rush (BotWRecomp's BotW list as the guide).
- 305 · the camera-rate getter confirmed at run time (nxport b588cf2; 1.4.3's only getter of that shape is main 0x1bd3528). If a shrine
  flashes in play at the game's resolution steps: BennettStaley 38a1348's carry (MIT).
- Checks of finished items, for the user: grass that stands up and can be cut (161), hair and cloth in wind at 60 and 120 fps (163),
  `DM_NVN_FBX=split` with a turning camera over paved ground (165), an overworld save for towns (167), the game's text in the app's language
  (170), stick diagonals against a console (124), a shrine entered at 60 fps (91).

## Other hardware

- 190 · a phone with headroom (the iPhone 17 Pro's 56 fps case; none at hand): `DM_SCENE_AUTO=1` measured there, then a setting in the app.
  On the Air it changes nothing by design.
- 320g · the iPhone Duo's inner screen (the user's main phone: ask first): the widened present's fps, GPU time, the HUD's proportions.
- 66, 67, 299 · a controller: the printed-letter face buttons of a Switch Pro controller (66); gyro and rumble with a controller and the phone
  (67); Home and Capture kept from the system (nxnative 062cd06), motion (659c69d, cca3333), rumble through `GCHapticsLocalityHandles`
  (2e07a88) (299).
- 71d · a display above 120 Hz (macOS rounds a 60 Hz request to 72 on a 144 Hz panel).
- 62, 62c2, 62g, 318 · other iPhones and iPads: the cold-texture census (56) under a device's memory limit; BC2 / BC3 / BC7 → ASTC and BC4 /
  BC5 → EAC on GPUs without BC (62c2); one varying interface as a fallback library if a log shows "not written by vertex shader" (62g); a
  device before Apple10 to see the shader's LOD bias on (318; the user, 2026-11-10: none for now).

The Android phone (a Redmi K100 Pro Max), for item 301, manosaba on Android:
- 301h · Tears of the Kingdom's data imported by the app (a folder the player picks: the system's document picker).
- 301d2 · the on-screen controls (`host/front/touchpad.c`; Tears of the Kingdom's), the software keyboard (nx2android draws its own), the
  motion sensors (ASensorManager → `dm_motion_set`), vibration (`dm_vibration_take` → the Vibrator through JNI).
- 301e2c · what manosaba never needed: texel buffers, line loops, clears with a channel mask, storage buffers off 64-byte alignment (a chain
  change) or addressed from a uniform slot; the Adreno findings to check for (a varying not written, more than about 700 draws in a command
  buffer, LRZ across passes, subgroups) and 284's items.
- 301e3 · occlusion samples and the counter reports' GPU timestamps, layered passes, the other TBDR rewrites, the write-back of GPU-written
  CPU-visible memory, the 5-bit formats, memory and bandwidth readings; the trial's pass of 3-4 draws that takes 3.3 of 7.4-7.7 ms a frame;
  the NVN fence on the game's last own submission (LASRecompAndroid 54686f6).
- 301f · the interactive class and deadline declaration (performance hint sessions), the energy and per-core readings; the dips left: the
  wait for an idle GPU before a pool's memory is freed (0.5-0.66 s in all), `vkAllocateMemory` (0.44-0.55 s), the game's own main thread.
- 313 · for Tears of the Kingdom there, from other ports: 1/256-texel vertex lookups if the terrain cracks (300), cuts only between passes
  (PKShield f4f9f1c), the spin before short waits (mleo2003 77f6586), same-pass feedback (triasbrata 41beb51), padded vertex outputs
  (triasbrata 8bb3212), masked and region clears and GPU timestamps (AurelioB/totk-android `VULKAN.md`), scaled depth copies (nx2android
  e154eec), `vm.max_map_count` (diablo3-port 5bb14e8), pools backed on first GPU use (mleo2003 3f5467d).
- 313 · from S2Arm (written anew): the BC4 3D table's slices through an RGBA16UI view (a K100 probe of `vkCmdCopyImage`), the 1x1-viewport
  draw with no target, the vector-indexing workaround (a probe on the 840 and a count first), sub-allocated image memory, occlusion queries,
  the SampledCubeArray capability.
- 314c · the Vulkan vertex formats checked for VERTEX_BUFFER support (32-bit UI2F / I2F fall to R32_UINT / SINT); the swapchain acquire in
  `vk_present.c` bounded (100 ms; S2Arm 4c0bec5).
- 284 · nxport's frame capture replayed pass by pass on both backends; Adreno 740-799 without the quad / warp32 rewrites, subgroup helpers
  inlined, BC4 / BC5 as 8-bit where R16G16 does not filter (upstream 2461649, 758f5d4, 17b4baa); nx2apk's Darwin shim headers.

## When a game or a frame needs it

- 322 · three shaders changed when a run draws with one (`DM_NVN_KEY_WATCH` names the frame and pass): vertex shader 588e242a52f5f7aa's
  stores with no storage buffer; the sample index (PIXLD.MY_INDEX) of manosaba's pixel shaders a1fe71290083482a and 45eb69b3b99c6600.
- 320e · compute writing a widened image and a copy's destination widened after its source (S2Arm's rules); the `[nvn] widening's census`
  line watches.
- 281 · for other games, render targets of the screen's shape made at the screen's size while the game keeps its own (LASRecomp's "Match
  display", AnySwitch's `DM_INTERNAL_RES`), with a headroom guard (AnySwitch: none below 768 MB).
- 64 · with the next game: ClearBuffer, the indirect and IndirectCount draws and dispatches, Downsample, the deferred uniform / storage binds
  (64a2); tessellation (64f); a SystemEvent kept while its detached read end waits (64h2, UE4); AVC / AAC decoded from the game's bytes
  (64j2); LDN (64n2). Also [sources.md § Port when a game needs it](sources.md#port-when-a-game-needs-it).
