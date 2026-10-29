# The host for Android (plan item 301): the shared host sources (../host) with this directory's own, built with the NDK for arm64 Android.
#   make -C android GAME=manosaba      → build/<GAME>/dmhost, run from adb shell with an android product (DM_TARGET=android tools/build_product.sh)
#   make -C android test               → the unit tests of this directory, built for the phone (run them with run-tests, the phone connected)
# Graphics run on the null backend (gfx/nvn/null: nothing drawn) until the Vulkan one (301e); sound plays to the silent sink until AAudio (301d).
NDK ?= /opt/homebrew/share/android-ndk
API ?= 35
CC := $(NDK)/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android$(API)-clang
GAME ?= manosaba
H := ../host
GAMEDIR := ../games/$(GAME)
B := build/$(GAME)
ifeq ($(wildcard $(GAMEDIR)/game.json),)
$(error no game profile $(GAMEDIR)/game.json)
endif
REWRITER_STAMP := $(shell python3 ../tools/rewriter_stamp.py $(wildcard $(GAMEDIR)/rules.py))
# -Wno-format: uint64_t is unsigned long here and the shared sources print it with %ll (the same 64 bits; the Apple build keeps -Wformat)
CFLAGS = -O1 -g -Wall -Wno-unused-function -Wno-format -Wno-deprecated-declarations -fno-omit-frame-pointer -D_GNU_SOURCE \
	-DDM_REWRITER_STAMP=0x$(REWRITER_STAMP)ull -I$(H) -I../gfx/nvn/include -I$(GAMEDIR) -Icompat -include compat/darwin.h
# The shared host sources Android takes: every service, the runtime, what changes the game, the portable diagnostics and front-end helpers
HOST_SRC = $(sort $(wildcard $(H)/hle/hle_*.c)) $(H)/hle/hle.c \
	$(addprefix $(H)/runtime/,crash.c hstack.c loader.c main.c options.c romfs_parts.c romfs_validate.c thread.c entry.S hstack.S) \
	$(sort $(wildcard $(H)/game/*.c)) $(addprefix $(H)/diag/,diag.c memsnap.c sdktest.c sead.c) $(addprefix $(H)/front/,cmdchan.c touchpad.c) \
	$(H)/media/movie_webm.c $(H)/cloud/cloudsync.c
# The graphics layer without its Metal backend: the display and GPU driver glue takeovers (nn::vi, nv::), NVN's takeover and the replay core
# (gfx/nvn/hle_nvn.c, core/), the pool write tracking, texture layouts, the format table and the trace, on the null backend (gfx/nvn/null) until
# the Vulkan one (301e)
GFX_SRC = ../gfx/hle_vi.c ../gfx/hle_nv.c $(addprefix ../gfx/nvn/,hle_nvn.c nvn_dirty.c nvn_tex.c nvn_trace.c nvn_entry.S nvn_thunks.S) \
	$(sort $(wildcard ../gfx/nvn/core/*.c)) ../gfx/tex/nvn_fmt_mtl.c ../gfx/nvn/null/null.c
ANDROID_SRC = diag_android.c front_headless.c movie_none.c plat_linux.c slots.c compat/sha256.c
GAME_SRC = $(sort $(wildcard $(GAMEDIR)/*.c))
HDR = $(wildcard $(H)/*.h) $(wildcard $(H)/runtime/*.h) $(wildcard compat/*.h compat/*/*.h) $(wildcard $(GAMEDIR)/*.h) \
	$(wildcard ../gfx/nvn/*.h ../gfx/nvn/hle/*.inc ../gfx/nvn/core/*.h ../gfx/nvn/include/*.h ../gfx/tex/*.h)
.DEFAULT_GOAL := $(B)/dmhost

$(B)/game_profile.c: $(GAMEDIR)/game.json ../tools/gen_game.py
	@mkdir -p $(B)
	python3 ../tools/gen_game.py $< $@.tmp && mv $@.tmp $@
$(B)/dmhost: $(HOST_SRC) $(GFX_SRC) $(ANDROID_SRC) $(GAME_SRC) $(B)/game_profile.c $(HDR) ../tools/patch_nso.py $(wildcard $(GAMEDIR)/rules.py)
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -o $@ $(HOST_SRC) $(GFX_SRC) $(ANDROID_SRC) $(GAME_SRC) $(B)/game_profile.c -lm -ldl

# Unit tests of the Android parts, for the phone: build/tests/<name> (run-tests pushes them to /data/local/tmp/dm_tests and runs each)
TESTS = t_sha256 t_sha256_63 t_hthread t_hthread_nov t_slots
test: $(addprefix build/tests/,$(TESTS))
build/tests:
	mkdir -p $@
build/tests/t_sha256: tests/t_sha256.c compat/sha256.c | build/tests
	$(CC) -O1 -Wall -Icompat -o $@ $^
build/tests/t_sha256_63: tests/t_sha256.c compat/sha256.c | build/tests
	$(CC) -O1 -Wall -Icompat -DDM_TEST_SHA256_ROUND63 -o $@ $^
build/tests/t_hthread: tests/t_hthread.c plat_linux.c | build/tests
	$(CC) -O1 -Wall -Wno-format -D_GNU_SOURCE -I$(H) -Icompat -include compat/darwin.h -o $@ $^
build/tests/t_hthread_nov: tests/t_hthread.c plat_linux.c | build/tests
	$(CC) -O1 -Wall -Wno-format -D_GNU_SOURCE -I$(H) -Icompat -include compat/darwin.h -DDM_TEST_STOP_NO_V -o $@ $^
build/tests/t_slots: tests/t_slots.c slots.c | build/tests
	$(CC) -O1 -Wall -Wno-format -D_GNU_SOURCE -I$(H) -Icompat -include compat/darwin.h -o $@ $^
SERIAL ?= $(shell adb devices | awk 'NR > 1 && $$2 == "device" { print $$1; exit }')
run-tests: test
	sh tests/run_on_phone.sh "$(SERIAL)" $(TESTS)
.PHONY: test run-tests
