#!/usr/bin/env ruby
# Rewire Unity's generated iOS Xcode project to run the transplanted macOS game code:
#   * GameAssembly target: no il2cpp/bee step; compiles only the class + full icall registration.
#   * UnityFramework: links ManosabaGame.framework (the game's IL2CPP code) instead of il2cpp.a.
#   * Unity-iPhone: embeds ManosabaGame.framework and the steam_api stub.
# Usage: patch_xcode.rb <xcode-project-dir> <gen-dir>   (idempotent)
require "fileutils"
require "xcodeproj"

proj_dir, gen_dir = ARGV
abort "usage: patch_xcode.rb <xcode-dir> <gen-dir>" unless proj_dir && gen_dir

def log(msg) = puts("[patch_xcode] #{msg}")

dest = File.join(proj_dir, "Manosaba")
FileUtils.mkdir_p(dest)
# Device builds link the static player and need the class + full icall registration;
# the simulator player is a full dylib that registers everything itself.
sim = File.exist?(File.join(proj_dir, "Libraries", "libiPhone-lib.dylib"))
log "mode: #{sim ? "simulator" : "device"}"
FileUtils.cp(File.join(gen_dir, "steam_api.dylib"), dest)
if sim
  File.write(File.join(dest, "GameAssemblyPlaceholder.c"), "// GameAssembly lives in ManosabaGame.framework.\nint manosaba_game_assembly_placeholder;\n")
else
  FileUtils.cp(File.join(gen_dir, "FullICallRegistration.cpp"), dest)
end
FileUtils.rm_rf(File.join(dest, "ManosabaGame.framework"))
FileUtils.cp_r(File.join(gen_dir, "ManosabaGame.framework"), dest)

project = Xcodeproj::Project.open(File.join(proj_dir, "Unity-iPhone.xcodeproj"))
target = ->(name) { project.targets.find { |t| t.name == name } or abort "missing target #{name}" }
ga, uf, app = target["GameAssembly"], target["UnityFramework"], target["Unity-iPhone"]

group = project.main_group["Manosaba"] || project.main_group.new_group("Manosaba", "Manosaba")
ref = ->(path) { group.files.find { |f| f.path == path } || group.new_file(path) }

# 1. GameAssembly: replace the il2cpp build script with two plain sources.
ga.shell_script_build_phases.each { |ph| log "removing GameAssembly script phase"; ph.remove_from_project }
ga_sources =
  if sim
    [ref["GameAssemblyPlaceholder.c"]]
  else
    class_reg = project.files.find { |f| f.path&.end_with?("il2cppOutput/UnityClassRegistration.cpp") } ||
                project.main_group.new_file("Il2CppOutputProject/Source/il2cppOutput/UnityClassRegistration.cpp")
    [class_reg, ref["FullICallRegistration.cpp"]]
  end
ga_sources.each do |f|
  ga.source_build_phase.add_file_reference(f, true)
end
log "GameAssembly sources: #{ga.source_build_phase.files.map(&:display_name)}"

# 2. UnityFramework: drop il2cpp.a, link the game framework.
game_fw = ref["ManosabaGame.framework"]
uf.build_configurations.each do |c|
  # il2cpp.a and the il2cpp export list belonged to the shell's own GameAssembly.
  flags = Array(c.build_settings["OTHER_LDFLAGS"]).reject { |f| f.include?("il2cpp.a") || f.include?("il2cpp-symbols.txt") }
  c.build_settings["OTHER_LDFLAGS"] = flags
  fsp = Array(c.build_settings["FRAMEWORK_SEARCH_PATHS"] || ["$(inherited)"])
  fsp << "$(PROJECT_DIR)/Manosaba" unless fsp.include?("$(PROJECT_DIR)/Manosaba")
  c.build_settings["FRAMEWORK_SEARCH_PATHS"] = fsp
end
uf.frameworks_build_phase.add_file_reference(game_fw, true)

# 3. App: embed the framework and the steam stub next to UnityFramework.framework.
embed = app.copy_files_build_phases.find { |ph| ph.files.any? { |f| f.display_name == "UnityFramework.framework" } } or abort "no embed phase"
[game_fw, ref["steam_api.dylib"]].each do |f|
  next if embed.files.any? { |bf| bf.file_ref == f }
  bf = embed.add_file_reference(f)
  bf.settings = { "ATTRIBUTES" => %w[CodeSignOnCopy RemoveHeadersOnCopy] }
end
log "embedded: #{embed.files.map(&:display_name)}"

# 4. iOS 27 asserts at launch when an app linked against the iOS 27 SDK lacks the UIScene
#    life cycle (Unity 6000.0.48f1's trampoline predates it). The check keys off the main
#    executable's recorded SDK, so record 26.0 before Xcode signs the app.
#    Only rewrite a freshly linked executable: on an incremental build Xcode skips CodeSign,
#    so touching the already-signed binary would leave the app with an invalid signature.
SDK_PHASE = "Manosaba: record iOS 26 SDK"
ph = app.shell_script_build_phases.find { |p| p.name == SDK_PHASE } || app.new_shell_script_build_phase(SDK_PHASE)
ph.always_out_of_date = "1"
ph.shell_script = <<~SH
  set -euo pipefail
  exe="$TARGET_BUILD_DIR/$EXECUTABLE_PATH"
  build=$(vtool -show-build "$exe")
  if grep -qE '^ +sdk 26\\.0$' <<<"$build"; then
    echo "SDK 26.0 already recorded; leaving the signed executable alone"
    exit 0
  fi
  plat=ios; [ "$PLATFORM_NAME" = iphonesimulator ] && plat=iossim
  vtool -set-build-version "$plat" "$IPHONEOS_DEPLOYMENT_TARGET" 26.0 -replace -output "$exe.sdk26" "$exe"
  mv "$exe.sdk26" "$exe"
  vtool -show-build "$exe" | grep -E 'minos|sdk'
SH
log "app phases: #{app.build_phases.map { |p| p.display_name }}"

# 5. Orientation: the game is 16:9 landscape-only, but the desktop PlayerSettings it ships
#    with allow every orientation. Pin the trampoline's view controller to landscape.
vc = File.join(proj_dir, "Classes/UI/UnityViewControllerBase+iOS.mm")
src = File.read(vc)
MARK = "// Manosaba: landscape only"
unless src.include?(MARK)
  src = src.sub("- (void)readOrientationFromUnity\n{\n",
                "- (void)readOrientationFromUnity\n{\n    #{MARK}\n    _fixedOrientation = UIInterfaceOrientationUnknown;\n    _supportedOrientations = EnabledAutorotationInterfaceOrientations();\n    return;\n")
  src = src.sub("NSUInteger EnabledAutorotationInterfaceOrientations()\n{\n    NSUInteger ret = 0;\n",
                "NSUInteger EnabledAutorotationInterfaceOrientations()\n{\n    #{MARK}\n    return (1 << UIInterfaceOrientationLandscapeLeft) | (1 << UIInterfaceOrientationLandscapeRight);\n    NSUInteger ret = 0;\n")
  abort "orientation patch did not apply" unless src.scan(MARK).size == 2
  File.write(vc, src)
  log "pinned orientation to landscape"
end

# 6. Memory: log footprint/headroom every 2 s (UnityFramework) and, on device, ask for the
#    increased memory limit (the game's PC-sized assets run close to the default jetsam limit).
extras = File.expand_path("shell-extras", __dir__)
sources = %w[ManosabaSettings.mm MemoryLogger.mm MetalAllocLogger.mm AudioSession.mm ThermalFrameCap.mm
             ICloudSaveSyncCore.mm ICloudSaveSync.mm]
(sources + %w[ManosabaSettings.h ICloudSaveSyncCore.h manosaba.entitlements]).each { |f| FileUtils.cp(File.join(extras, f), dest) }
# Files that earlier versions of this script added and that no longer exist.
%w[AudioRouteLogger.mm].each do |gone|
  uf.source_build_phase.files.select { |bf| bf.file_ref&.path == gone }.each { |bf| bf.remove_from_project }
  group.files.select { |f| f.path == gone }.each(&:remove_from_project)
  FileUtils.rm_f(File.join(dest, gone))
end
sources.each { |f| uf.source_build_phase.add_file_reference(ref[f], true) }
%w[ManosabaSettings.h ICloudSaveSyncCore.h manosaba.entitlements].each { |f| ref[f] }
# Settings app page (Settings.bundle in the app bundle root), read by ManosabaSettings.mm.
FileUtils.rm_rf(File.join(dest, "Settings.bundle"))
FileUtils.cp_r(File.join(extras, "Settings.bundle"), dest)
settings = group.files.find { |f| f.path == "Settings.bundle" } || group.new_reference("Settings.bundle")
settings.last_known_file_type = "wrapper.plug-in"
app.resources_build_phase.add_file_reference(settings, true)
# 7. iCloud save sync (ICloudSaveSync.mm) needs an iCloud Documents container that is registered
#    to the App ID in the developer account, so it is opt-in: ICLOUD_CONTAINER=iCloud.<bundle id>.
container = ENV["ICLOUD_CONTAINER"].to_s
unless sim || container.empty?
  ent_path = File.join(dest, "manosaba.entitlements")
  ent = Xcodeproj::Plist.read_from_path(ent_path)
  ent["com.apple.developer.icloud-container-identifiers"] = [container]
  ent["com.apple.developer.ubiquity-container-identifiers"] = [container]
  ent["com.apple.developer.icloud-services"] = ["CloudDocuments"]
  Xcodeproj::Plist.write_to_path(ent, ent_path)
end
unless sim
  app.build_configurations.each { |c| c.build_settings["CODE_SIGN_ENTITLEMENTS"] = "Manosaba/manosaba.entitlements" }
end
log "memory logger + iCloud save sync added#{sim ? "" : ", increased-memory-limit entitlement set"}" \
    "#{container.empty? || sim ? "" : ", iCloud container #{container}"}"

# 8. Thermal frame-rate cap (ThermalFrameCap.mm): route Unity's target frame rate through it.
rendering = File.join(proj_dir, "Classes/UnityAppController+Rendering.mm")
rsrc = File.read(rendering)
CAP_MARK = "// Manosaba: thermal frame-rate cap"
unless rsrc.include?(CAP_MARK)
  anchor = "    if (targetFPS <= 0)\n        targetFPS = UnityGetTargetFPS();\n"
  abort "frame-rate anchor not found in #{rendering}" unless rsrc.include?(anchor)
  rsrc = rsrc.sub(anchor, anchor + "    targetFPS = ManosabaCapTargetFPS(targetFPS); #{CAP_MARK}\n")
  rsrc = rsrc.sub("- (void)callbackFramerateChange:(int)targetFPS\n",
                  "extern \"C\" int ManosabaCapTargetFPS(int targetFPS); #{CAP_MARK}\n- (void)callbackFramerateChange:(int)targetFPS\n")
  abort "frame-rate cap patch did not apply" unless rsrc.scan(CAP_MARK).size == 2
  File.write(rendering, rsrc)
  log "thermal frame-rate cap wired into callbackFramerateChange:"
end

project.save
log "saved"
