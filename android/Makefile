# The host for Android (plan item 301): the shared host sources (../host) with this directory's own, built with the NDK for arm64 Android.
#   make -C android GAME=manosaba      → build/<GAME>/dmhost, run from adb shell with an android product (DM_TARGET=android tools/build_product.sh)
#   make -C android GAME=manosaba apk  → build/<GAME>/<GAME>.apk: the app (NativeActivity, libdmhost.so) with the product inside (android/run.sh)
#   make -C android test               → the unit tests of this directory, built for the phone (run them with run-tests, the phone connected)
# Both hosts are linked from the same objects (build/<GAME>/obj): main.c's main is dm_main, called by front_headless.c's main or the app's thread.
# Graphics run on the null backend (gfx/nvn/null: nothing drawn) until the Vulkan one (301e).
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
# -Wno-format: uint64_t is unsigned long here and the shared sources print it with %ll (the same 64 bits; the Apple build keeps -Wformat).
# -fPIC (not the default PIE): the objects go into the app's library too, whose thread-local variables need the dynamic TLS model
CFLAGS = -O1 -g -fPIC -Wall -Wno-unused-function -Wno-format -Wno-deprecated-declarations -fno-omit-frame-pointer -D_GNU_SOURCE \
	-DDM_REWRITER_STAMP=0x$(REWRITER_STAMP)ull -I$(H) -I../gfx/nvn/include -I$(GAMEDIR) -Icompat -include compat/darwin.h
# The shared host sources Android takes: every service, the runtime, what changes the game, the portable diagnostics and front-end helpers
HOST_SRC = $(sort $(wildcard $(H)/hle/hle_*.c)) $(H)/hle/hle.c \
	$(addprefix $(H)/runtime/,crash.c hstack.c loader.c main.c options.c romfs_parts.c romfs_validate.c thread.c entry.S hstack.S) \
	$(sort $(wildcard $(H)/game/*.c)) $(addprefix $(H)/diag/,diag.c memsnap.c sdktest.c sead.c) $(addprefix $(H)/front/,cmdchan.c touchpad.c) \
	$(H)/media/movie_webm.c $(H)/cloud/cloudsync.c
# The graphics layer without its Metal backend: the display and GPU driver glue takeovers (nn::vi, nv::), NVN's takeover and the replay core
# (gfx/nvn/hle_nvn.c, core/), the pool write tracking, texture layouts and deswizzling, the format table and the trace. The backend: the null one
# (gfx/nvn/null: nothing drawn) under adb, the Vulkan one (gfx/nvn/vk, plan item 301e) in the app
GFX_SRC = ../gfx/hle_vi.c ../gfx/hle_nv.c $(addprefix ../gfx/nvn/,hle_nvn.c nvn_dirty.c nvn_tex.c nvn_trace.c nvn_entry.S nvn_thunks.S) \
	$(sort $(wildcard ../gfx/nvn/core/*.c)) ../gfx/tex/nvn_fmt_mtl.c ../gfx/tex/nvn_deswizzle.c ../gfx/nvn/null/readings.c
ANDROID_SRC = $(addprefix ../android/,diag_android.c movie_android.c plat_linux.c settings.c slots.c compat/sha256.c)
GAME_SRC = $(sort $(wildcard $(GAMEDIR)/*.c))
VK_SRC = $(sort $(wildcard ../gfx/nvn/vk/*.c))
OBJ = $(patsubst ../%,$(B)/obj/%.o,$(HOST_SRC) $(GFX_SRC) $(ANDROID_SRC) $(GAME_SRC)) $(B)/obj/game_profile.c.o
NULL_OBJ = $(B)/obj/gfx/nvn/null/null.c.o
VK_OBJ = $(patsubst ../%,$(B)/obj/%.o,$(VK_SRC))
.DEFAULT_GOAL := $(B)/dmhost

# Every object depends on the flags it was built with (the rewriter stamp among them) and on the headers it read (-MMD)
$(B)/flags.txt: FORCE
	@mkdir -p $(B); echo '$(CC) $(CFLAGS)' | cmp -s - $@ || echo '$(CC) $(CFLAGS)' > $@
$(B)/obj/%.c.o: ../%.c $(B)/flags.txt
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(if $(filter %/runtime/main.c,$<),-Dmain=dm_main) -MMD -MP -c -o $@ $<
$(B)/obj/%.S.o: ../%.S $(B)/flags.txt
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<
$(B)/game_profile.c: $(GAMEDIR)/game.json ../tools/gen_game.py
	@mkdir -p $(B)
	python3 ../tools/gen_game.py $< $@.tmp && mv $@.tmp $@
$(B)/obj/game_profile.c.o: $(B)/game_profile.c $(B)/flags.txt
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<
-include $(OBJ:.o=.d) $(NULL_OBJ:.o=.d) $(VK_OBJ:.o=.d) $(B)/obj/android/front_headless.c.d $(B)/obj/android/app_android.c.d $(B)/obj/android/front_android.c.d
$(B)/dmhost: $(OBJ) $(NULL_OBJ) $(B)/obj/android/front_headless.c.o
	$(CC) -o $@ $^ -lm -ldl -landroid -laaudio -lmediandk

# The app's library: NativeActivity loads it and calls ANativeActivity_onCreate (app_android.c). -Bsymbolic: the assembly's branches and adrp to
# the library's own global symbols (dm_hcall, nvn_slot ...) bind inside it, with no PLT stub (one would take x16 / x17, which the thunks pass) and
# no text relocation; its symbols stay in the dynamic table for dladdr and dlsym (the crash report, the thread dumps)
APP_OBJ = $(B)/obj/android/app_android.c.o $(B)/obj/android/front_android.c.o
$(B)/libdmhost.so: $(OBJ) $(VK_OBJ) $(APP_OBJ)
	$(CC) -shared -o $@ $^ -Wl,-soname,libdmhost.so -Wl,-Bsymbolic -Wl,--no-undefined -landroid -llog -laaudio -lmediandk -lvulkan -lz -lm -ldl

# The APK (android/tools/apk.py): the library, the product's files the loader reads (the modules' images and what ios/tools/product_files.py lists), the icon.
# PRODUCT: the android product (default $DM_ROOT/build/android/<game>.b301, DM_ROOT the workspace, ../work when unset); SHADERS: the Vulkan shader
# bundle (gfx/tools/dmsh_vk.py; default $DM_ROOT/build/android/<game>.dmvk), carried as the product's shaders.dmvk; PKG: the package name;
# RELEASE=1: not debuggable (debuggable lets adb's run-as read the app's own files: the save, the copied product); the version code is the commit
# count, so a newer build installs over an older one. Written each time (about 6 s; the same inputs give the same bytes): the workspace's paths
# may hold spaces, which make's prerequisites cannot, so they appear only quoted in the recipe
WS = $(if $(DM_ROOT),$(DM_ROOT),../work)
PRODUCT ?= $(WS)/build/android/$(GAME).b301
SHADERS ?= $(WS)/build/android/$(GAME).dmvk
PKG ?= io.github.iceorange_dev.$(GAME)
MODS := $(shell python3 -c "import json,sys; print(' '.join(json.load(open(sys.argv[1]))['modules']))" $(GAMEDIR)/game.json)
LABEL := $(shell python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['title'])" $(GAMEDIR)/game.json)
ICON := $(shell python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['workspace'].get('icon', ''))" $(GAMEDIR)/game.json)
# The app's Java classes (android/java: the settings panel, plan item 301g) as classes.dex: javac against the platform's android.jar, d8. JDK and the
# Android SDK's build-tools and platform (Homebrew's openjdk@21 and android-commandlinetools, sdkmanager "build-tools;35.0.1" "platforms;android-35")
ANDROID_SDK ?= /opt/homebrew/share/android-commandlinetools
JAVA_HOME ?= /opt/homebrew/opt/openjdk@21
ANDROID_JAR ?= $(ANDROID_SDK)/platforms/android-$(API)/android.jar
D8 ?= $(ANDROID_SDK)/build-tools/35.0.1/d8
JAVA_SRC = $(sort $(shell find java -name '*.java'))
$(B)/classes.dex: $(JAVA_SRC)
	@test -x "$(JAVA_HOME)/bin/javac" -a -f "$(ANDROID_JAR)" -a -x "$(D8)" || { echo "[red] no JDK / android.jar / d8: JAVA_HOME=$(JAVA_HOME) ANDROID_SDK=$(ANDROID_SDK)"; exit 1; }
	rm -rf $(B)/java && mkdir -p $(B)/java
	$(JAVA_HOME)/bin/javac --release 11 -Xlint:-options -classpath $(ANDROID_JAR) -d $(B)/java $(JAVA_SRC)
	JAVA_HOME=$(JAVA_HOME) $(D8) --min-api 34 --lib $(ANDROID_JAR) --output $(B) $$(find $(B)/java -name '*.class')   # (this d8 knows API levels up to 34)
$(B)/$(GAME).apk: $(B)/libdmhost.so $(B)/classes.dex tools/apk.py FORCE
	@test -f "$(SHADERS)" || { echo "[red] no shader bundle at $(SHADERS): DM_GAME=$(GAME) gfx/tools/dmsh_vk.py <captures> <it>"; exit 1; }
	sips -Z 432 -s format png "$(WS)/$(ICON)" --out $(B)/icon.png > /dev/null
	python3 tools/apk.py --lib $(B)/libdmhost.so --dex $(B)/classes.dex --out $@ --package $(PKG) --label "$(LABEL)" --icon $(B)/icon.png $(if $(filter 1,$(RELEASE)),--release) \
		--version-code $$(git rev-list --count HEAD) --version-name $$(git rev-parse --short HEAD) --product "$(PRODUCT)" --modules "$(MODS)" \
		--asset "product/shaders.dmvk=$(SHADERS)"
apk: $(B)/$(GAME).apk
lib: $(B)/libdmhost.so

# Unit tests of the Android parts, for the phone: build/tests/<name> (run-tests pushes them to /data/local/tmp/dm_tests and runs each)
TESTS = t_sha256 t_sha256_63 t_hthread t_hthread_nov t_slots t_input t_input_ydown t_input_three t_movie t_movie_uvswap t_vk_be t_vk_be_early t_vk_be_wind t_vk_be_grid t_vk_be_stale t_vk_be_cont t_vk_be_ubsh t_vk_be_shot t_vk_be_ts t_vk_be_pe t_vk_be_bar t_vk_be_ptime t_vk_be_arena t_vk_be_pend t_vk_be_pgall t_thermal t_thermal_raw t_settings t_settings_over t_settings_always
TEST_DATA = build/tests/movie.mp4 build/tests/movie.webm build/tests/movie1080.mp4 build/tests/t_vk.dmvk
test: $(addprefix build/tests/,$(TESTS)) $(TEST_DATA)
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
build/tests/t_settings: tests/t_settings.c settings.c settings.h $(H)/envfile.h $(H)/native_size.h | build/tests
	$(CC) -O1 -Wall -Werror -I$(H) -o $@ tests/t_settings.c settings.c
build/tests/t_settings_over: tests/t_settings.c settings.c settings.h $(H)/envfile.h $(H)/native_size.h | build/tests
	$(CC) -O1 -Wall -Werror -DDM_TEST_SETTINGS_OVER_ENV -I$(H) -o $@ tests/t_settings.c settings.c
build/tests/t_settings_always: tests/t_settings.c settings.c settings.h $(H)/envfile.h $(H)/native_size.h | build/tests
	$(CC) -O1 -Wall -Werror -DDM_TEST_SETTINGS_ALWAYS -I$(H) -o $@ tests/t_settings.c settings.c
build/tests/t_thermal: tests/t_thermal.c diag_android.c | build/tests
	$(CC) -O1 -Wall -Wno-format -D_GNU_SOURCE -I$(H) -Icompat -include compat/darwin.h -o $@ $^ -landroid
build/tests/t_thermal_raw: tests/t_thermal.c diag_android.c | build/tests
	$(CC) -O1 -Wall -Wno-format -D_GNU_SOURCE -DDM_TEST_THERMAL_RAW -I$(H) -Icompat -include compat/darwin.h -o $@ $^ -landroid
build/tests/t_input: tests/t_input.c input_map.h | build/tests
	$(CC) -O1 -Wall -I. -I$(H) -o $@ $< -lm
build/tests/t_input_ydown: tests/t_input.c input_map.h | build/tests
	$(CC) -O1 -Wall -I. -I$(H) -DDM_TEST_INPUT_Y_DOWN -o $@ $< -lm
build/tests/t_input_three: tests/t_input.c input_map.h | build/tests
	$(CC) -O1 -Wall -I. -I$(H) -DDM_TEST_THREE_TAP_REPEAT -o $@ $< -lm
MOVIE_SRC = tests/t_movie.c movie_android.c ../host/media/movie_webm.c
build/tests/t_movie: $(MOVIE_SRC) | build/tests
	$(CC) -O1 -Wall -Wno-format -D_GNU_SOURCE -I. -I$(H) -Icompat -include compat/darwin.h -o $@ $^ -lmediandk -lm
build/tests/t_movie_uvswap: $(MOVIE_SRC) | build/tests
	$(CC) -O1 -Wall -Wno-format -D_GNU_SOURCE -I. -I$(H) -Icompat -include compat/darwin.h -DDM_TEST_MOVIE_UV_SWAPPED -o $@ $^ -lmediandk -lm
# t_movie's clips (ffmpeg on the Mac): 1 s of red, H.264 with a key frame every 10 and a half-scale 440 Hz sine in AAC; the same red in VP9; 0.2 s of blue at 1080p
build/tests/movie.mp4: | build/tests
	ffmpeg -loglevel error -y -f lavfi -i color=c=red:s=320x240:r=30:d=1 -f lavfi -i sine=frequency=440:sample_rate=48000:duration=1 -t 1 \
		-af volume=4 -ac 2 -c:v libx264 -pix_fmt yuv420p -g 10 -bf 0 -c:a aac -b:a 128k $@
build/tests/movie1080.mp4: | build/tests
	ffmpeg -loglevel error -y -f lavfi -i color=c=blue:s=1920x1080:r=30:d=0.2 -c:v libx264 -pix_fmt yuv420p -bf 0 $@
build/tests/movie.webm: | build/tests
	ffmpeg -loglevel error -y -f lavfi -i color=c=red:s=320x240:r=30:d=1 -c:v libvpx-vp9 -g 10 -pix_fmt yuv420p $@
# the Vulkan backend without a window, with the core modules it calls (plan item 301e1), and its draws' shaders (301e2)
VKT_SRC = tests/t_vk_be.c $(VK_SRC) $(addprefix ../gfx/nvn/core/,core.c state.c pend.c usnap.c tbdr.c store_index.c prog.c upd.c frame.c) ../gfx/nvn/nvn_tex.c ../gfx/nvn/nvn_dirty.c \
	../gfx/tex/nvn_deswizzle.c
VKT_FLAGS = -O1 -g -Wall -Wno-format -D_GNU_SOURCE -I$(H) -I../gfx/nvn/include -I../gfx/nvn/vk -Icompat -include compat/darwin.h
build/tests/t_vk_be: $(VKT_SRC) | build/tests
	$(CC) $(VKT_FLAGS) -o $@ $(VKT_SRC) -lvulkan -landroid -lz -lm
build/tests/t_vk_be_early: $(VKT_SRC) | build/tests
	$(CC) $(VKT_FLAGS) -DDM_TEST_VK_EVENT_EARLY -o $@ $(VKT_SRC) -lvulkan -landroid -lz -lm
build/tests/t_vk_be_wind: $(VKT_SRC) | build/tests
	$(CC) $(VKT_FLAGS) -DDM_TEST_VK_WINDING_SWAPPED -o $@ $(VKT_SRC) -lvulkan -landroid -lz -lm
build/tests/t_vk_be_grid: $(VKT_SRC) | build/tests
	$(CC) $(VKT_FLAGS) -DDM_TEST_VK_GRID_ZERO -o $@ $(VKT_SRC) -lvulkan -landroid -lz -lm
build/tests/t_vk_be_stale: $(VKT_SRC) | build/tests
	$(CC) $(VKT_FLAGS) -DDM_TEST_VK_SET_STALE -o $@ $(VKT_SRC) -lvulkan -landroid -lz -lm
build/tests/t_vk_be_cont: $(VKT_SRC) | build/tests
	$(CC) $(VKT_FLAGS) -DDM_TEST_VK_CONT_NO_SPLIT -o $@ $(VKT_SRC) -lvulkan -landroid -lz -lm
build/tests/t_vk_be_ubsh: $(VKT_SRC) | build/tests
	$(CC) $(VKT_FLAGS) -DDM_TEST_VK_UB_NO_SHADOW -o $@ $(VKT_SRC) -lvulkan -landroid -lz -lm
build/tests/t_vk_be_shot: $(VKT_SRC) | build/tests
	$(CC) $(VKT_FLAGS) -DDM_TEST_VK_SHOT_FLIPPED -o $@ $(VKT_SRC) -lvulkan -landroid -lz -lm
build/tests/t_vk_be_ts: $(VKT_SRC) | build/tests
	$(CC) $(VKT_FLAGS) -DDM_TEST_VK_TS_NONE -o $@ $(VKT_SRC) -lvulkan -landroid -lz -lm
build/tests/t_vk_be_pe: $(VKT_SRC) | build/tests
	$(CC) $(VKT_FLAGS) -DDM_TEST_VK_PE_OP_ONLY -o $@ $(VKT_SRC) -lvulkan -landroid -lz -lm
build/tests/t_vk_be_bar: $(VKT_SRC) | build/tests
	$(CC) $(VKT_FLAGS) -DDM_TEST_VK_BAR_NO_SPLIT -o $@ $(VKT_SRC) -lvulkan -landroid -lz -lm
build/tests/t_vk_be_ptime: $(VKT_SRC) | build/tests
	$(CC) $(VKT_FLAGS) -DDM_TEST_VK_PTIME_FIRST_ONLY -o $@ $(VKT_SRC) -lvulkan -landroid -lz -lm
build/tests/t_vk_be_arena: $(VKT_SRC) | build/tests
	$(CC) $(VKT_FLAGS) -DDM_TEST_VK_ARENA_WAIT_OPEN -o $@ $(VKT_SRC) -lvulkan -landroid -lz -lm
build/tests/t_vk_be_pend: $(VKT_SRC) | build/tests
	$(CC) $(VKT_FLAGS) -DDM_TEST_VK_PEND_KEPT -o $@ $(VKT_SRC) -lvulkan -landroid -lz -lm
build/tests/t_vk_be_pgall: $(VKT_SRC) | build/tests
	$(CC) $(VKT_FLAGS) -DDM_TEST_VK_POOL_ALL_UNWRITTEN -o $@ $(VKT_SRC) -lvulkan -landroid -lz -lm
build/tests/t_vk.dmvk: tests/mk_vk_bundle.py ../gfx/tools/dmsh_vk.py ../games/manosaba/game.json | build/tests
	python3 tests/mk_vk_bundle.py $@
SERIAL ?= $(shell adb devices | awk 'NR > 1 && $$2 == "device" { print $$1; exit }')
run-tests: test
	sh tests/run_on_phone.sh "$(SERIAL)" "$(TEST_DATA)" $(TESTS)
.PHONY: test run-tests apk lib FORCE
