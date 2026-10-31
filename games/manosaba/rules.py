# Rewriter rules for manosaba (tools/patch_nso.py --rules games/manosaba/rules.py), keyed by the NSO build ID of each module.
# Format: see the "Game rules" section at the top of patch_nso.py.

CUTS = {
    # main 2.0.4b (Unity 6000.0.72f1). Unity compiles the Switch player settings into a row of constant functions at the start of main (from 0x10,
    # each `mov w0, #constant; ret`). Input init 0x9d6240 builds the Input System backend 0x9d5920 with (0x50() controller styles, 0x60() controller
    # count, 0x20() touchscreen, 0x70()), and only when the touchscreen argument is 1 (0x9d5a2c) does it create the touchscreen device 0x9d5d40, whose
    # update 0x9d6070 reads nn::hid::GetTouchScreenStates<16> every frame. The game ships with 0x20 returning 0; returning 1 gives native touch
    #
    # Presenting evidence in a trial: the witch book page shows its "present" button (WitchTrialsButton) only off console. The shared
    # WitchBookPageBase<,>.ControlPresentEvidenceUi 0x200c6e0 calls Show() 0x18ad1c0 when the mode is on and !Platform.IsConsole (0x21a8b00),
    # otherwise Hide() 0x18ad210; on Switch the player presses A (Naninovel/Submit = <NPad>/buttonEast) instead, which a touch screen does not have.
    # ProfilePage overrides it (0x191d520) to also turn the button's TrueShadow on under the same !IsConsole test. Both IsConsole calls answer 0
    # here; the platform itself stays NintendoSwitch (CurrentPlatform also names the Addressables catalog path and selects the save configuration).
    'd81467ff90dcee10ab82759d16731cc1': [
        (0x20, 'e0031f2a', '20008052', 'player setting "touchscreen" 0x20: mov w0, wzr → mov w0, #1, so the Input System backend creates its touchscreen device'),
        (0x191d5b0, '542d2294', '00008052', 'ProfilePage.ControlPresentEvidenceUi: bl Platform.IsConsole → mov w0, #0 (present button shadow as off console)'),
        (0x200c734, 'f3700694', '00008052', 'WitchBookPageBase.ControlPresentEvidenceUi: bl Platform.IsConsole → mov w0, #0 (show the touchable present button)'),
        # Render resolution (graphics.c): the game keeps its PC version's CameraManagerExtended settings. SetResolutionLevel 0x21c5c60 picks
        # (width, height) from its configuration's table by level and tail-calls Screen.SetResolution(int, int, bool) 0x3327d00, its only caller on
        # Switch (the PC settings dropdowns call the four-argument overload 0x328c360 directly); LoadServiceState 0x21c6660 calls it at startup
        (0x3327d00, '5f00007268008052', '011000d4c0035fd6', 'Screen.SetResolution(int, int, bool): tst w2, #1; mov w8, #3 → svc #0x80; ret (host call: the size scaled, then the four-argument overload)'),
        # The display's size (graphics.c): the Switch player's screen manager (the object main+0x44d760 returns, made at 0x44d6c0 with the
        # constructor 0x90c860) answers its size with the constant 1920x1080 at vtable +0x180, 0x90c990; the window textures take it (0x9b01f8 →
        # SetSize2D at 0x9b0258 / 0x9b02f4) and Screen.SetResolution is held to it; the graphics device has the same constant answer at 0x99bcc0
        # (with it 1920x1080, a 2560x1440 picture reached only the window's top-left 1920x1080). The host call answers DM_DISPLAY_SIZE instead
        # for both (plan item 281)
        (0x90c990, '08f08052280000b9', '211000d4c0035fd6', 'screen manager display size: mov w8, #0x780; str w8, [x1] → svc #0x81; ret (host call: the size reported)'),
        (0x99bcc0, '08f08052280000b9', '211000d4c0035fd6', 'graphics device display size: mov w8, #0x780; str w8, [x1] → svc #0x81; ret (host call: the size reported)'),
    ],    # sdk (plan item 289, after nx2android 8056043, MIT, the words checked here): the same node area cut as Tears of the Kingdom's (games/totk/rules.py).
    # Unity 6 takes all its memory through AllocateAddressRegion, and the first trial reached +4904 MB of the 5376 MB alias region on the iPhone Air
    # (472 MB left). The constructor is at sdk+0x14fbe0 (+0x14fbf0 the limit, +0x14fc5c the area registered as occupied); VammManager::AllocateAddressRegion
    # (sdk+0x14fd00) aligns regions to 64 KB or 2 MB only
    'ab3835b2a37b1916e296f27e82dd4e36': [
        (0x14fbf0, '08fca752', '087ca052', 'AddressRegionManager constructor: node area limit mov w8, #0x3fe00000 → mov w8, #0x3e00000'),
        (0x14fc5c, '09fca752', '097ca052', 'AddressRegionManager constructor: the node area registered as occupied, mov w9, #0x3fe00000 → mov w9, #0x3e00000'),
    ],
}
# The main loop (main+0x41367c, the player's endless loop) calls its steps through a pointer; main+0x71bcc0 is the one that does a frame's work: on
# the main thread, once a frame (gate counted against the frames presented, plan item 302). graphics.c's step_gate sets the resolution there for
# the screen the window moved to (the setting "Native" on the iPhone Duo); the entry's sub sp, sp, #0xa0 runs after the gate
OBS = {
    'd81467ff90dcee10ab82759d16731cc1': [(0x71bcc0, 1, 'd10283ff', 'main loop step that does a frame\'s work: the resolution for the screen the window moved to (graphics.c step_gate)')],
}
PARAMS = {}
