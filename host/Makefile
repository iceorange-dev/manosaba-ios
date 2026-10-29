# Host build. GAME selects the game directory games/<GAME>/ (default manosaba): its game.json becomes gen/<GAME>/game_profile.c (tools/gen_game.py)
# and its C / Objective-C sources, if any, are built in. ios/tools/gen_build.py takes the iOS app's sources, include paths and frameworks from the
# compile command below (make -n), so the iOS app builds the same host.
GAME ?= manosaba
DM_ROOT ?= $(HOME)/dm-work
GAMEDIR := ../games/$(GAME)
PROFILE := gen/$(GAME)/game_profile.c
ifeq ($(wildcard $(GAMEDIR)/game.json),)
$(error no game profile $(GAMEDIR)/game.json)
endif
# Minimum system version: the graphics backend (gfx/nvn/nvn_mtl.m) is built on Metal 4, which exists from macOS 26. tools/mac_app.sh reads the same
# number through print-macos-min for Info.plist.
MACOS_MIN ?= 26.0
CFLAGS = -O1 -g -Wall -Wno-unused-function -mmacosx-version-min=$(MACOS_MIN)
# The rewriter stamp this host expects of a product (tools/rewriter_stamp.py: patch_nso.py + the game's rules.py; loader.c compares)
REWRITER_STAMP := $(shell python3 ../tools/rewriter_stamp.py $(wildcard $(GAMEDIR)/rules.py))
CFLAGS += -DDM_REWRITER_STAMP=0x$(REWRITER_STAMP)ull
# Host sources, one per line in alphabetical order of file name, in directories by role (hle/ the services, runtime/ loading and threads, front/ the Mac
# front end and input, media/, diag/, game/ what changes the game, cloud/); the hle/hle_*.c service files are taken by wildcard; the headers stay here
SRC = \
	front/app_mac.m \
	game/aspect.c \
	cloud/cloud.m \
	cloud/cloudsync.c \
	game/cheat_vm.c \
	game/cheats.c \
	front/cmdchan.c \
	runtime/crash.c \
	diag/diag.c \
	runtime/entry.S \
	front/front_mac.m \
	game/game.c \
	game/game_slot.c \
	front/gamepad.m \
	$(PROFILE) \
	hle/hle.c \
	runtime/hstack.S \
	runtime/hstack.c \
	runtime/loader.c \
	runtime/main.c \
	diag/memsnap.c \
	diag/memstat.m \
	game/mods.c \
	media/movie_av.m \
	media/movie_webm.c \
	runtime/options.c \
	game/pchtxt.c \
	runtime/plat_apple.c \
	front/procon_mac.m \
	runtime/romfs_parts.c \
	runtime/romfs_validate.c \
	diag/sdktest.c \
	diag/sead.c \
	diag/thermal.m \
	runtime/thread.c \
	front/touchpad.c \
	$(sort $(wildcard hle/hle_*.c)) \
	$(sort $(wildcard $(GAMEDIR)/*.c $(GAMEDIR)/*.m))
# NVN → Metal and nn::vi (gfx/): sources under gfx, gfx/nvn and gfx/tex by wildcard (tests/ and metal/ are not part of the host)
GFX = $(sort $(wildcard ../gfx/*.c ../gfx/nvn/*.c ../gfx/nvn/*.m ../gfx/nvn/*.S ../gfx/nvn/mtl/*.m ../gfx/nvn/core/*.c ../gfx/tex/*.c ../gfx/tex/*.m))   # nvn/core: the replay's decisions in plain C; nvn/mtl/*.m: Metal backend translation units of their own (plan item 264)
GFX_H = $(sort $(wildcard ../gfx/nvn/*.h ../gfx/nvn/mtl/*.inc ../gfx/nvn/mtl/*.h ../gfx/nvn/hle/*.inc ../gfx/nvn/core/*.h ../gfx/nvn/include/*.h ../gfx/tex/*.h))   # mtl/*.inc: the parts nvn_mtl.m includes; mtl/mtl.h: the backend's private header; hle/*.inc: the parts hle_nvn.c includes
HDR = $(wildcard *.h) $(wildcard runtime/*.h) $(GFX_H) $(wildcard $(GAMEDIR)/*.h)
FRAMEWORKS = -framework Cocoa -framework QuartzCore -framework Metal -framework MetalFX -framework AudioToolbox -framework CoreAudio -framework ImageIO \
	-framework UniformTypeIdentifiers -framework GameController -framework CoreHaptics -framework IOKit -framework Network -framework AVFoundation -framework CoreMedia -framework CoreVideo
INCLUDES = -I. -I../gfx/nvn/include -I$(GAMEDIR)
# VP8 / VP9 movies (hle_movie.c: Tears of the Kingdom's cutscenes) decode with libvpx when it is installed (brew install libvpx; the static library is
# linked in, BSD-3-Clause). -isystem and the archive are not taken over by the iOS app (ios/tools/gen_build.py): it links its own, built by
# ios/tools/build_libvpx.sh (ios/run.sh, plan item 62b)
VPX ?= /opt/homebrew/opt/libvpx
VPX_LIB := $(wildcard $(VPX)/lib/libvpx.a)
VPX_FLAGS := $(if $(VPX_LIB),-isystem $(VPX)/include $(VPX_LIB))
.DEFAULT_GOAL := dmhost

$(PROFILE): $(GAMEDIR)/game.json ../tools/gen_game.py
	@mkdir -p $(dir $@)
	python3 ../tools/gen_game.py $< $@.tmp && mv $@.tmp $@
profile: $(PROFILE)
# The game the last host was built for, rewritten only when GAME changes: switching games relinks even when no source changed
gen/last_game: FORCE
	@mkdir -p gen; [ "$$(cat $@ 2>/dev/null)" = "$(GAME)" ] || echo "$(GAME)" > $@
FORCE:
.PHONY: FORCE profile

dmhost: $(SRC) $(GFX) $(HDR) gen/last_game ../tools/patch_nso.py $(wildcard $(GAMEDIR)/rules.py)
	clang $(CFLAGS) $(INCLUDES) -fobjc-arc -o $@ $(SRC) $(GFX) $(FRAMEWORKS) $(VPX_FLAGS)

# Older-API host (graphics backend without Metal 4, macOS 15; see DM_NVN_M3 in gfx/nvn/nvn_mtl.m): same sources, one more switch. make dmhost_m3
MACOS_MIN_M3 ?= 15.0
CFLAGS_M3 = $(subst -mmacosx-version-min=$(MACOS_MIN),-mmacosx-version-min=$(MACOS_MIN_M3),$(CFLAGS)) -DDM_NVN_M3=1
dmhost_m3: $(SRC) $(GFX) $(HDR) gen/last_game ../tools/patch_nso.py $(wildcard $(GAMEDIR)/rules.py)
	clang $(CFLAGS_M3) $(INCLUDES) -fobjc-arc -o $@ $(SRC) $(GFX) $(FRAMEWORKS) $(VPX_FLAGS)

print-macos-min:
	@echo $(MACOS_MIN)
print-macos-min-m3:
	@echo $(MACOS_MIN_M3)
print-game:
	@echo $(GAME)

.PHONY: print-macos-min print-macos-min-m3 print-game
