"""Tests for the transplant tooling."""
from __future__ import annotations

import dataclasses
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import gen_icall_registration as icall  # noqa: E402
import gen_steam_stub as steam  # noqa: E402
import patch_player_lib as ppl  # noqa: E402
import transcode_audio as audio  # noqa: E402


@pytest.mark.unit
def test_icall_symbol_regex_checks_mangled_length() -> None:
    good = "__Z28Register_UnityEngine_Foo_Barv"
    m = icall.SYM_RE.match(good)
    assert m and int(m.group(1)) == len(m.group(2))
    assert icall.SYM_RE.match("__Z20Register_Something_Elsev") is None  # not an icall namespace


@pytest.mark.unit
def test_icall_render_declares_and_calls_each() -> None:
    out = icall.render(["Register_UnityEngine_A", "Register_Unity_B"])
    assert "void RegisterAllStrippedInternalCalls()" in out
    for n in ("Register_UnityEngine_A", "Register_Unity_B"):
        assert f"void {n}();" in out and f"\t{n}();" in out


@pytest.mark.unit
def test_steam_stub_special_cases() -> None:
    out = steam.render(["SteamAPI_RunCallbacks", "SteamAPI_RestartAppIfNecessary", "SteamInternal_SteamAPI_Init"])
    assert "void *SteamAPI_RunCallbacks(void) { return 0; }" in out
    assert "return false;" in out  # RestartAppIfNecessary must not ask the app to quit
    assert "return 1;" in out  # SteamAPI init reports FailedGeneric


@pytest.mark.integration
def test_steam_stub_compiles_for_ios(tmp_path: Path) -> None:
    c = tmp_path / "stub.c"
    c.write_text(steam.render(["SteamAPI_RunCallbacks", "SteamAPI_RestartAppIfNecessary", "SteamInternal_SteamAPI_Init"]))
    subprocess.run(["xcrun", "--sdk", "iphoneos", "clang", "-target", "arm64-apple-ios15.0", "-dynamiclib",
                    "-o", str(tmp_path / "stub.dylib"), str(c)], check=True)
    syms = subprocess.run(["nm", "-gU", str(tmp_path / "stub.dylib")], capture_output=True, text=True, check=True).stdout
    assert "_SteamInternal_SteamAPI_Init" in syms


def _synthetic_player_lib(tmp_path: Path) -> Path:
    """An archive whose Runtime_Serialize member has the platform check at the real offset."""
    member, _, toff, _, _ = ppl.PATCHES[0]
    asm = tmp_path / "s.s"
    asm.write_text(
        ".text\n"
        f".space {toff - 4}\n"
        "cmp w10, #0x9\n"
        "b.ne Lerr\n"
        f".space {0xBE10 - toff - 4}\n"
        "Lerr: ret\n"
    )
    obj = tmp_path / member
    subprocess.run(["xcrun", "clang", "-c", "-target", "arm64-apple-ios15.0", "-o", str(obj), str(asm)], check=True)
    lib = tmp_path / "libfake.a"
    subprocess.run(["xcrun", "libtool", "-static", "-o", str(lib), str(obj)], check=True, capture_output=True)
    return lib


@pytest.mark.integration
def test_patch_player_lib_nops_check_and_is_idempotent(tmp_path: Path) -> None:
    lib = _synthetic_player_lib(tmp_path)
    buf = bytearray(lib.read_bytes())
    assert ppl.apply_patches(buf) == 1
    assert ppl.apply_patches(buf) == 0  # second run: already patched
    # exactly one 4-byte word changed, and it is now a NOP
    orig = lib.read_bytes()
    diff = [i for i, (a, b) in enumerate(zip(orig, buf)) if a != b]
    assert len(diff) <= 4 and diff
    assert struct.unpack_from("<I", buf, diff[0] - diff[0] % 4)[0] == ppl.NOP


def _synthetic_stub_member(tmp_path: Path) -> Path:
    """An object defining the stubbed proxies with their real prologues."""
    member = ppl.STUBS[0][0]
    lines = [".text"]
    for _, sym, (w0, w1) in ppl.STUBS:
        lines += [f".globl {sym}", ".p2align 2", f"{sym}:", f".long 0x{w0:08x}", f".long 0x{w1:08x}", "ret"]
    asm = tmp_path / "stubs.s"
    asm.write_text("\n".join(lines) + "\n")
    obj = tmp_path / member
    subprocess.run(["xcrun", "clang", "-c", "-target", "arm64-apple-ios15.0", "-o", str(obj), str(asm)], check=True)
    return obj


@pytest.mark.integration
def test_patch_player_lib_stubs_mobile_audio_proxies(tmp_path: Path) -> None:
    _synthetic_player_lib(tmp_path)  # creates the Runtime_Serialize member next to it
    serialize = tmp_path / ppl.PATCHES[0][0]
    stubs = _synthetic_stub_member(tmp_path)
    lib = tmp_path / "libboth.a"
    subprocess.run(["xcrun", "libtool", "-static", "-o", str(lib), str(serialize), str(stubs)],
                   check=True, capture_output=True)
    buf = bytearray(lib.read_bytes())
    assert ppl.apply_patches(buf) == 1 + len(ppl.STUBS)
    assert ppl.apply_patches(buf) == 0
    members = {n: (o, s) for n, o, s in ppl.iter_ar_members(buf)}
    moff = members[ppl.STUBS[0][0]][0]
    addr, _, foff = ppl.text_section(buf, moff)
    for _, sym, _ in ppl.STUBS:
        at = foff + ppl.symbol_address(buf, moff, sym) - addr
        assert struct.unpack_from("<II", buf, at) == ppl.RETURN_ZERO


@pytest.mark.integration
def test_patch_player_lib_refuses_unexpected_code(tmp_path: Path) -> None:
    lib = _synthetic_player_lib(tmp_path)
    buf = bytearray(lib.read_bytes())
    member_off = next(off for name, off, _ in ppl.iter_ar_members(buf) if name == ppl.PATCHES[0][0])
    addr, _, foff = ppl.text_section(buf, member_off)
    struct.pack_into("<I", buf, foff + ppl.PATCHES[0][2] - addr - 4, 0xD503201F)  # clobber the cmp
    with pytest.raises(ValueError):
        ppl.apply_patches(buf)


import patch_game_config as pgc  # noqa: E402


def _camera_config_bytes(coeffs: list[float]) -> bytes:
    name = pgc.NAME
    head = b"\0" * 12 + struct.pack("<i", 1) + struct.pack("<iq", 1, 1009)[:12] + struct.pack("<i", len(name))
    head += name + b"\0" * (((len(name) + 3) & ~3) - len(name))
    pcs = [(2560, 1440), (1920, 1080), (1280, 720)]
    body = struct.pack("<i", len(pcs)) + b"".join(struct.pack("<ii", *p) for p in pcs)
    body += struct.pack("<i", len(coeffs)) + struct.pack(f"<{len(coeffs)}f", *coeffs)
    body += struct.pack("<i", 2) + struct.pack("<ii", 60, 30) + struct.pack("<4i", 1, 0, 0, 1)
    return head + body


@pytest.mark.unit
def test_game_config_fills_empty_mobile_table() -> None:
    raw = _camera_config_bytes([])
    new = pgc.patched(raw)
    assert new is not None and len(new) == len(raw) + 12
    off = 32 + ((len(pgc.NAME) + 3) & ~3) + 4 + 3 * 8
    n = struct.unpack_from("<i", new, off)[0]
    assert n == 3
    assert [round(v, 3) for v in struct.unpack_from("<3f", new, off + 4)] == [1.0, 0.75, 0.5]
    assert struct.unpack_from("<iii", new, off + 16) == (2, 60, 30)  # TargetFpsValues intact


@pytest.mark.unit
def test_game_config_leaves_filled_table_alone() -> None:
    assert pgc.patched(_camera_config_bytes([1.0, 0.75, 0.5])) is None


def _ota_server(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    import threading
    from http.server import ThreadingHTTPServer

    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "ota"))
    import server as ota

    (tmp_path / "logs").mkdir()
    (tmp_path / "ota").mkdir()
    (tmp_path / "ota" / "manosaba.ipa").write_bytes(bytes(range(16)))  # your own build; no placeholder yet
    monkeypatch.setattr(ota, "ROOT", tmp_path)
    monkeypatch.setattr(ota, "OTA_DIR", tmp_path / "ota")
    monkeypatch.setattr(ota, "UDID_FILE", tmp_path / "logs/ipad-udid.json")
    srv = ThreadingHTTPServer(("127.0.0.1", 0), ota.Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return ota, srv, f"http://127.0.0.1:{srv.server_address[1]}"


@pytest.mark.unit
def test_ota_profile_and_manifests_point_at_base() -> None:
    import plistlib

    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "ota"))
    import server as ota

    cfg = plistlib.loads(ota.mobileconfig())
    assert cfg["PayloadType"] == "Profile Service"
    assert cfg["PayloadContent"]["URL"] == f"{ota.BASE}/udid"
    game = plistlib.loads(ota.manifest("game"))["items"][0]
    unlock = plistlib.loads(ota.manifest("unlock"))["items"][0]
    assert game["assets"][0]["url"] == f"{ota.BASE}/manosaba.ipa"
    assert unlock["assets"][0]["url"] == f"{ota.BASE}/placeholderunlockdev.ipa"
    # Same bundle id: installing your game build replaces the placeholder in place.
    assert game["metadata"]["bundle-identifier"] == unlock["metadata"]["bundle-identifier"] == ota.BUNDLE_ID


@pytest.mark.unit
def test_ota_packages_range_and_udid_callback(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    import json
    import plistlib
    import urllib.error
    import urllib.request

    ota, srv, url = _ota_server(tmp_path, monkeypatch)
    try:
        with urllib.request.urlopen(urllib.request.Request(f"{url}/manosaba.ipa", headers={"Range": "bytes=2-5"})) as r:
            assert r.status == 206 and r.headers["Content-Range"] == "bytes 2-5/16" and r.read() == bytes([2, 3, 4, 5])
        with urllib.request.urlopen(f"{url}/manosaba.ipa") as r:
            assert r.status == 200 and r.read() == bytes(range(16))

        # The device posts a CMS envelope; the server only needs the plist inside it.
        attrs = {"UDID": "00008027-TEST", "PRODUCT": "iPad8,10", "VERSION": "24A446"}
        body = b"\x30\x80junk" + plistlib.dumps(attrs) + b"\x00trailer"
        req = urllib.request.Request(f"{url}/udid", data=body, method="POST")
        try:
            urllib.request.build_opener(type("NoRedirect", (urllib.request.HTTPRedirectHandler,),
                                             {"redirect_request": lambda *a: None})).open(req)
        except urllib.error.HTTPError as e:
            assert e.code == 301 and e.headers["Location"] == f"{ota.BASE}/"
        assert json.loads(ota.UDID_FILE.read_text()) == attrs
        with urllib.request.urlopen(f"{url}/") as r:
            page = r.read().decode()
        assert "00008027-TEST" in page and "安装游戏（1 MB）" in page
        assert 'class="btn off"' in page  # the placeholder is not built yet
        # ... but the game is never gated on it: Developer Mode may already be on.
        assert f'<a class="btn " href="itms-services://?action=download-manifest&amp;url={ota.BASE}/manifest.plist">' in page

        try:
            urllib.request.urlopen(f"{url}/placeholderunlockdev.ipa")
            raise AssertionError("a missing package must 404")
        except urllib.error.HTTPError as e:
            assert e.code == 404
        (ota.OTA_DIR / "placeholderunlockdev.ipa").write_bytes(b"PK-placeholder")
        with urllib.request.urlopen(f"{url}/unlock.plist") as r:
            assert plistlib.loads(r.read())["items"][0]["assets"][0]["url"].endswith("/placeholderunlockdev.ipa")
        with urllib.request.urlopen(f"{url}/placeholderunlockdev.ipa") as r:
            assert r.read() == b"PK-placeholder"
    finally:
        srv.shutdown()


import io  # noqa: E402
import json  # noqa: E402
import plistlib  # noqa: E402

import make_app_icon as mai  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

RED, BLUE = (200, 30, 40), (30, 60, 200)


def _mac_tile(size: int = 1024) -> Image.Image:
    """The macOS icon template: a squircle inset 10% on a transparent canvas, with a soft shadow."""
    inset = size // 10
    box = (inset, inset, size - inset - 1, size - inset - 1)
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(img).rounded_rectangle(tuple(v + size // 50 for v in box), radius=size // 5, fill=(0, 0, 0, 90))
    art = Image.new("RGBA", (size, size), RED)
    ImageDraw.Draw(art).rectangle((size // 2, 0, size, size), fill=BLUE)  # left half red, right half blue
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle(box, radius=size // 5, fill=255)
    img.paste(art, (0, 0), mask)
    return img


def _icns(reps: dict[bytes, Image.Image]) -> bytes:
    body = b""
    for tag, im in reps.items():
        buf = io.BytesIO()
        im.save(buf, "PNG")
        body += tag + (8 + buf.tell()).to_bytes(4, "big") + buf.getvalue()
    return b"icns" + (8 + len(body)).to_bytes(4, "big") + body


@pytest.mark.unit
def test_app_icon_mac_tile_becomes_opaque_fullbleed_square() -> None:
    icns = _icns({b"ic09": _mac_tile(512), b"ic10": _mac_tile(1024)})
    master = mai.make_master(icns, (255, 255, 255))
    assert master.mode == "RGB" and master.size == (1024, 1024)
    # the 10% template margin and shadow are gone: the artwork reaches the edges...
    assert master.getpixel((4, 512)) == RED and master.getpixel((1019, 512)) == BLUE
    assert master.getpixel((256, 4)) == RED and master.getpixel((768, 1019)) == BLUE
    # ...and the rounded-off corners carry the neighbouring artwork colours, not black or white
    for xy, want in (((0, 0), RED), ((0, 1023), RED), ((1023, 0), BLUE), ((1023, 1023), BLUE)):
        got = master.getpixel(xy)
        assert max(abs(a - b) for a, b in zip(got, want)) <= 4, (xy, got)


@pytest.mark.unit
def test_app_icon_glyph_is_flattened_onto_background() -> None:
    glyph = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
    ImageDraw.Draw(glyph).ellipse((412, 412, 612, 612), fill=(255, 255, 0, 255))
    master = mai.make_master(_icns({b"ic10": glyph}), (10, 20, 30))
    assert master.getpixel((3, 3)) == (10, 20, 30)
    assert master.getpixel((512, 512)) == (255, 255, 0)


@pytest.mark.unit
def test_app_icon_opaque_source_passes_through() -> None:
    flat = Image.new("RGB", (1024, 1024), (5, 6, 7))
    master = mai.make_master(_icns({b"ic10": flat}), (255, 255, 255))
    assert master.getpixel((0, 0)) == (5, 6, 7) and master.size == (1024, 1024)


@pytest.mark.unit
def test_app_icon_rejects_small_or_broken_icns() -> None:
    with pytest.raises(ValueError, match="at least"):
        mai.make_master(_icns({b"ic07": _mac_tile(128)}), (255, 255, 255))
    with pytest.raises(ValueError, match="no PNG"):
        mai.make_master(b"icns\x00\x00\x00\x08", (255, 255, 255))
    with pytest.raises(ValueError, match="not an icns"):
        mai.make_master(b"nope", (255, 255, 255))


@pytest.mark.unit
def test_app_icon_path_follows_info_plist(tmp_path: Path) -> None:
    res = tmp_path / "g.app" / "Contents" / "Resources"
    res.mkdir(parents=True)
    (res / "Custom.icns").write_bytes(b"x")
    (tmp_path / "g.app" / "Contents" / "Info.plist").write_bytes(plistlib.dumps({"CFBundleIconFile": "Custom"}))
    assert mai.icon_path(tmp_path / "g.app") == res / "Custom.icns"
    (tmp_path / "g.app" / "Contents" / "Info.plist").unlink()
    with pytest.raises(ValueError, match="PlayerIcon.icns"):
        mai.icon_path(tmp_path / "g.app")


@pytest.mark.unit
def test_app_icon_catalog_is_complete_idempotent_and_drops_stale_files(tmp_path: Path) -> None:
    master = mai.make_master(_icns({b"ic10": _mac_tile(1024)}), (255, 255, 255))
    iconset = tmp_path / "AppIcon.appiconset"
    iconset.mkdir()
    (iconset / "Icon-iPhone-180.png").write_bytes(b"unity default")
    assert mai.write_catalog(master, iconset) > 0
    assert not (iconset / "Icon-iPhone-180.png").exists()

    contents = json.loads((iconset / "Contents.json").read_text())
    pairs = {(e["idiom"], e["size"], e["scale"]) for e in contents["images"]}
    for need in [("iphone", "60x60", "2x"), ("iphone", "60x60", "3x"), ("ipad", "76x76", "2x"),
                 ("ipad", "83.5x83.5", "2x"), ("ios-marketing", "1024x1024", "1x")]:
        assert need in pairs
    assert sorted(p.name for p in iconset.glob("*.png")) == sorted(e["filename"] for e in contents["images"])
    for e in contents["images"]:
        im = Image.open(iconset / e["filename"])
        pt, scale = float(e["size"].split("x")[0]), int(e["scale"][0])
        assert im.size == (round(pt * scale),) * 2 and im.mode == "RGB"  # no alpha channel

    before = {p.name: p.read_bytes() for p in iconset.iterdir()}
    assert mai.write_catalog(master, iconset) == 0
    assert {p.name: p.read_bytes() for p in iconset.iterdir()} == before


@pytest.mark.integration
def test_app_icon_script_output_compiles_with_actool(tmp_path: Path) -> None:
    # A real icns from iconutil, in a fake .app, through the CLI, then Apple's asset compiler.
    iconset = tmp_path / "x.iconset"
    iconset.mkdir()
    for name, px in (("icon_512x512@2x", 1024), ("icon_512x512", 512), ("icon_256x256@2x", 512), ("icon_256x256", 256),
                     ("icon_128x128@2x", 256), ("icon_128x128", 128)):
        _mac_tile(px).save(iconset / f"{name}.png")
    app = tmp_path / "g.app" / "Contents"
    (app / "Resources").mkdir(parents=True)
    subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(app / "Resources" / "PlayerIcon.icns")], check=True)
    (app / "Info.plist").write_bytes(plistlib.dumps({"CFBundleIconFile": "PlayerIcon.icns"}))
    xcode = tmp_path / "xcode"
    (xcode / "Unity-iPhone" / "Images.xcassets").mkdir(parents=True)

    script = Path(__file__).resolve().parents[1] / "tools" / "make_app_icon.py"
    for _ in range(2):  # the second run must be a no-op
        run = subprocess.run([sys.executable, str(script), str(tmp_path / "g.app"), str(xcode)],
                             capture_output=True, text=True)
        assert run.returncode == 0, run.stderr
    assert "already up to date" in run.stderr

    out = tmp_path / "out"
    out.mkdir()
    partial = tmp_path / "partial.plist"
    res = subprocess.run(["xcrun", "actool", "--compile", str(out), "--platform", "iphoneos",
                          "--minimum-deployment-target", "15.0", "--app-icon", "AppIcon",
                          "--target-device", "iphone", "--target-device", "ipad",
                          "--output-partial-info-plist", str(partial), str(xcode / "Unity-iPhone" / "Images.xcassets")],
                         capture_output=True, text=True, check=True)
    assert "warning" not in res.stdout.lower() and "error" not in res.stdout.lower(), res.stdout
    info = plistlib.loads(partial.read_bytes())
    assert info["CFBundleIcons"]["CFBundlePrimaryIcon"]["CFBundleIconName"] == "AppIcon"
    assert "AppIcon76x76" in info["CFBundleIcons~ipad"]["CFBundlePrimaryIcon"]["CFBundleIconFiles"]
    car = json.loads(subprocess.run(["xcrun", "assetutil", "--info", str(out / "Assets.car")],
                                    capture_output=True, text=True, check=True).stdout)
    widths = {e["PixelWidth"] for e in car if e.get("Name") == "AppIcon" and "PixelWidth" in e}
    assert {20, 29, 40, 58, 60, 76, 80, 87, 120, 152, 167, 180, 1024} <= widths  # every size, not just the 1024


@pytest.mark.integration
def test_icloud_save_sync_scenarios(tmp_path: Path) -> None:
    root = Path(__file__).resolve().parents[1]
    extras = root / "tools" / "shell-extras"
    exe = tmp_path / "icloud_sync_harness"
    subprocess.run(["xcrun", "clang++", "-std=c++17", "-fobjc-arc", "-Wall", "-Werror", "-I", str(extras),
                    str(extras / "ICloudSaveSyncCore.mm"), str(root / "tests" / "icloud_sync_harness.mm"),
                    "-framework", "Foundation", "-o", str(exe)], check=True)
    run = subprocess.run([str(exe), str(tmp_path / "work")], capture_output=True, text=True)
    print(run.stdout)
    assert run.returncode == 0, run.stdout
    assert run.stdout.count("\nok ") + run.stdout.startswith("ok ") == 14


import transcode_textures as tt  # noqa: E402

STEAM_AA = Path.home() / "Library/Application Support/Steam/steamapps/common/manosaba_game/manosaba.app/Contents/Resources/Data/StreamingAssets/aa/StandaloneOSX"


@pytest.mark.unit
def test_texture_target_prefers_6x6_for_large_character_and_ui_atlases() -> None:
    char = "Raw/aa/StandaloneOSX/naninovel-characters_assets_naninovel/characters/coco.bundle"
    ui = "Raw/aa/StandaloneOSX/general-sprites_assets_all.bundle"
    bg = "Raw/aa/StandaloneOSX/naninovel-backgrounds_assets_naninovel/stills/1_1.bundle"
    TF = tt.TF
    assert tt.target_format(char, TF.BC7, 4096, 8192) == TF.ASTC_RGB_6x6
    assert tt.target_format(ui, TF.DXT5, 4096, 4096) == TF.ASTC_RGB_6x6
    assert tt.target_format(ui, TF.DXT5, 1024, 512) == TF.ASTC_RGB_4x4  # small UI sprites keep 4x4
    assert tt.target_format(bg, TF.DXT5, 4096, 2048) == TF.ASTC_RGB_4x4
    assert tt.target_format(char, TF.BC4, 4096, 4096) == TF.R8  # non-ASTC targets are untouched


@pytest.mark.integration
@pytest.mark.skipif(not (STEAM_AA / "naninovel-characters_assets_naninovel/characters/coco.bundle").exists(),
                    reason="needs the Steam install")
def test_transcode_rebuilds_ress_without_the_replaced_data(tmp_path: Path) -> None:
    import UnityPy

    rel = "naninovel-characters_assets_naninovel/characters/coco.bundle"
    dst = tmp_path / "Raw/aa/StandaloneOSX" / rel
    dst.parent.mkdir(parents=True)
    dst.write_bytes((STEAM_AA / rel).read_bytes())
    done = tt.process_file(dst, f"Raw/aa/StandaloneOSX/{rel}")
    assert [c.dst for c in done] == ["ASTC_RGB_6x6"] and done[0].psnr >= tt.MIN_PSNR

    env = UnityPy.load(str(dst))
    ress = [v for k, v in env.file.files.items() if k.endswith(".resS")]
    tex = next(o.read() for o in env.objects if o.type.name == "Texture2D")
    expected = -(-tex.m_Width // 6) * -(-tex.m_Height // 6) * 16  # one 16-byte block per 6x6
    assert len(ress) == 1 and len(ress[0].bytes) == expected == tex.m_StreamData.size
    assert not tex.image_data and tex.image.size == (tex.m_Width, tex.m_Height)
    assert tt.process_file(dst, f"Raw/aa/StandaloneOSX/{rel}") == []  # idempotent


import patch_font_atlases as pfa  # noqa: E402


def _fake_font_asset(name: bytes, live: int, creation: int = 4096) -> bytes:
    head = b"\0" * 28 + struct.pack("<i", len(name)) + name + b"\0" * (-len(name) % 4)
    creation_settings = struct.pack("<9i", 90, 9, 2, 0, creation, creation, 7, 0, 0)
    packing = struct.pack("<2i4i", 0, 1, 0, 0, live - 1, live - 1)  # no used rects, one free rect
    tail = struct.pack("<4i", 0, 1, 1, 1) + struct.pack("<4i", live, live, 9, pfa.SDFAA) + packing + b"\0" * 32
    return head + creation_settings + tail


@pytest.mark.unit
def test_font_atlas_patch_touches_only_the_live_size() -> None:
    raw = _fake_font_asset(b"SourceHanSerifSC", 4096)
    assert pfa.font_name(raw) == "SourceHanSerifSC"
    new, old = pfa.patch_raw(raw)
    assert old == 4096 and len(new) == len(raw)
    off = pfa.atlas_size_offset(new)
    assert struct.unpack_from("<ii", new, off) == (2048, 2048)
    assert new.count(struct.pack("<ii", 4096, 4096)) == 1  # m_CreationSettings is untouched
    assert struct.unpack_from("<2i4i", new, off + 16) == (0, 1, 0, 0, 2047, 2047)  # free rect fits the atlas
    assert pfa.patch_raw(new)[1] == 2048  # idempotent: reports the size already there


@pytest.mark.unit
def test_font_atlas_patch_refuses_an_atlas_with_glyphs() -> None:
    raw = bytearray(_fake_font_asset(b"X", 4096))
    off = pfa.atlas_size_offset(bytes(raw))
    struct.pack_into("<i", raw, off + 16, 3)  # used rects: the atlas already holds glyphs
    with pytest.raises(ValueError):
        pfa.patch_raw(bytes(raw))


@pytest.mark.unit
def test_font_atlas_patch_refuses_ambiguous_layouts() -> None:
    raw = _fake_font_asset(b"X", 4096) + struct.pack("<4i", 1024, 1024, 5, pfa.SDFAA)
    with pytest.raises(ValueError):
        pfa.atlas_size_offset(raw)


@pytest.mark.integration
@pytest.mark.skipif(not (STEAM_AA / "general-fonts-tsukushimincho_assets_all.bundle").exists(), reason="needs the Steam install")
def test_font_atlas_patch_round_trips_a_real_bundle(tmp_path: Path) -> None:
    import UnityPy

    name = "general-fonts-tsukushimincho_assets_all.bundle"
    dst = tmp_path / name
    dst.write_bytes((STEAM_AA / name).read_bytes())
    assert pfa.patch_bundle(dst, "TsukushiMincho") == 4096
    assert pfa.patch_bundle(dst, "TsukushiMincho") is None
    env = UnityPy.load(str(dst))
    raw = next(o.get_raw_data() for o in env.objects if o.type.name == "MonoBehaviour" and pfa.font_name(o.get_raw_data()) == "TsukushiMincho")
    off = pfa.atlas_size_offset(raw)
    assert struct.unpack_from("<10i", raw, off) == (2048, 2048, 9, pfa.SDFAA, 0, 1, 0, 0, 2047, 2047)
    for mat in (o.read() for o in env.objects if o.type.name == "Material"):
        sizes = {k: v for k, v in mat.m_SavedProperties.m_Floats if k in ("_TextureWidth", "_TextureHeight")}
        assert set(sizes.values()) <= {2048.0}


# --------------------------------------------------------------------------------------------------
# tools/transcode_audio.py (synthetic audio only; the integration tests need ffmpeg with libvorbis)
# --------------------------------------------------------------------------------------------------
def _bit_pack(fields: list[tuple[int, int]]) -> bytes:
    """Vorbis bit packing: (value, width) fields, least significant bit first."""
    bits = [(value >> i) & 1 for value, width in fields for i in range(width)]
    bits += [0] * (-len(bits) % 8)
    return bytes(sum(bit << i for i, bit in enumerate(bits[n : n + 8])) for n in range(0, len(bits), 8))


def _fake_setup(blockflags: list[int]) -> bytes:
    """A setup header that is junk except for the tail the mode parser reads: count, modes, framing bit."""
    body = [(0xA5A5A5, 24), (0x3, 2)]  # whatever precedes the modes
    body += [(len(blockflags) - 1, 6)]
    for flag in blockflags:
        body += [(flag, 1), (0, 16), (0, 16), (0, 8)]  # blockflag, window type, transform type, mapping
    body += [(1, 1)]  # framing
    return b"\x05vorbis" + _bit_pack(body)


def _ogg_page(packets: list[bytes], granule: int, *, last_continues: bool = False) -> bytes:
    """One Ogg page; with last_continues the final packet is an unfinished multiple of 255 bytes."""
    lacing = b""
    for n, p in enumerate(packets):
        unfinished = last_continues and n == len(packets) - 1
        lacing += b"\xff" * (len(p) // 255) + (b"" if unfinished else bytes([len(p) % 255]))
    return (b"OggS" + bytes([0, 0]) + struct.pack("<qIII", granule, 1, 0, 0) + bytes([len(lacing)]) + lacing
            + b"".join(packets))


@pytest.mark.unit
def test_audio_fsb5_writer_layout_and_roundtrip() -> None:
    packets = [bytes([0x00 | (i % 2) << 1]) + bytes([i]) * (40 + i) for i in range(6)]
    granules = [0, 400, 800, 1200, 1600, 2000]
    crc = next(iter(audio.FMOD_VORBIS_HEADERS))
    template = struct.pack("<Q", 1) + bytes(range(100, 116)) + b"fillerXX"
    data = audio.build_fsb5_vorbis(packets, granules, 2300, 8000, 1, crc, template)
    version, count, header_size, names, data_size, mode = struct.unpack_from("<6I", data, 4)
    assert (data[:4], version, count, names, mode) == (b"FSB5", 1, 1, 0, 15)
    assert (60 + header_size) % 32 == 0 and data_size % 32 == 0 and len(data) == 60 + header_size + data_size
    assert data[28:36] == struct.pack("<Q", 1) and data[52:60] == b"fillerXX"  # flags FMOD insists on, filler
    assert data[36:52] != template[8:24]  # the hash describes the new stream, not the template's
    assert audio.build_fsb5_vorbis(packets, granules, 2300, 8000, 1, crc)[28:36] == struct.pack("<Q", 1)
    clip = audio.parse_fsb5_vorbis(data)
    assert (clip.channels, clip.rate, clip.samples, clip.crc) == (1, 8000, 2300, crc)
    assert list(clip.packets) == packets
    import fsb5  # python-fsb5 reads our container the same way

    sample = fsb5.load(data).samples[0]
    assert (sample.channels, sample.frequency, sample.samples) == (1, 8000, 2300)
    assert sample.metadata[fsb5.MetadataChunkType.VORBISDATA].crc32 == crc


@pytest.mark.unit
def test_audio_seek_table_has_one_entry_per_whole_second() -> None:
    granules = [0, 400, 800, 1200, 1600, 2000, 2400]
    offsets = [0, 10, 20, 30, 40, 50, 60]
    # second 1 falls between the packets ending at 800 and 1200; second 2 ends exactly on 2000 -> the one before it
    assert audio.seek_pairs(granules, offsets, 1000, 2300) == [(800, 20), (1600, 40)]
    assert audio.seek_pairs(granules, offsets, 1000, 1999) == [(800, 20)]  # no entry past the clip's end
    assert audio.seek_pairs(granules, offsets, 1000, 900) == []  # clips under a second have no table


@pytest.mark.unit
@pytest.mark.parametrize("flags", [[0, 1], [1, 0], [0], [0, 1, 1], [0, 0, 1, 1, 0]])
def test_audio_vorbis_mode_flags_come_from_the_setup_tail(flags: list[int]) -> None:
    assert audio.vorbis_mode_flags(_fake_setup(flags)) == flags


@pytest.mark.unit
def test_audio_granule_positions_follow_the_vorbis_window_rule() -> None:
    short, long_ = bytes([0b00]), bytes([0b10])  # packet type bit 0 = audio, mode number in bit 1
    packets = [long_, long_, short, long_, short]
    assert audio.granule_positions(packets, [0, 1], (256, 2048)) == [0, 1024, 1024 + 576, 1600 + 576, 2176 + 576]
    with pytest.raises(audio.AudioError):
        audio.granule_positions([bytes([0b01])], [0, 1], (256, 2048))  # header packet inside the audio stream


@pytest.mark.unit
def test_audio_parse_ogg_packets_span_pages() -> None:
    long_packet = bytes(range(256)) * 2 + bytes(range(88))  # 600 bytes: needs lacing values 255, 255, 90
    stream = (_ogg_page([b"id", b"comment", b"setup"], 0)
              + _ogg_page([b"a" * 10, long_packet[:510]], -1, last_continues=True)  # page ends inside a packet
              + _ogg_page([long_packet[510:], b"z"], 4096))
    parsed = audio.parse_ogg(stream)
    assert parsed.packets == [b"id", b"comment", b"setup", b"a" * 10, long_packet, b"z"]
    assert parsed.page_ends == [(2, 0), (3, -1), (5, 4096)]
    with pytest.raises(audio.AudioError):
        audio.parse_ogg(b"RIFF....")


@pytest.mark.unit
def test_audio_quality_scale_and_fmod_table_cover_what_we_emit() -> None:
    assert audio.vorbis_quality(100) == pytest.approx(1.0) and audio.vorbis_quality(1) == pytest.approx(-0.1)
    table = set(audio.FMOD_VORBIS_HEADERS.values())
    for combo in [(100, 2, 48000), (45, 1, 48000), (45, 2, 48000), (63, 1, 48000), (63, 2, 48000)]:
        assert combo in table  # voice mono/stereo, music stereo, SFX mono; q100 stereo is what the game ships


@pytest.mark.unit
def test_audio_classification_and_streaming_rules() -> None:
    assert audio.classify("general-voice-act01_chapter01_trial_assets_all.bundle", "0101Adv01_Ema001") == "voice"
    assert audio.classify("naninovel-audio_assets_audio-bgm_015naninovel.bundle", "Bgm_015_001_Loop") == "music"
    assert audio.classify("naninovel-audio_assets_audio-song_004naninovel.bundle", "Song_004_001") == "music"
    assert audio.classify("naninovel-audio_assets_audio-sfxnaninovel.bundle", "Sfx_System_Cancel_001") == "sfx"
    assert audio.classify("naninovel-audio_assets_audio-ambient_102naninovel.bundle", "Ambient_102_001 Fire") == "sfx"
    assert audio.wants_streaming("Song_004_001", audio.LOAD_COMPRESSED_IN_MEMORY, 3.0)  # named
    assert audio.wants_streaming("Whatever", audio.LOAD_DECOMPRESS_ON_LOAD, 120.5)  # long and resident as PCM
    assert not audio.wants_streaming("Whatever", audio.LOAD_DECOMPRESS_ON_LOAD, 119.0)
    assert not audio.wants_streaming("Whatever", audio.LOAD_COMPRESSED_IN_MEMORY, 300.0)  # not asked for
    assert not audio.wants_streaming("Song_004_001", audio.LOAD_STREAMING, 300.0)


@pytest.mark.unit
def test_audio_channel_identity_check() -> None:
    rng = np.random.default_rng(1)
    voice = (rng.standard_normal(48000) * 3000).astype(np.int16)
    both = np.stack([voice, voice], axis=1)
    assert audio.channels_identical(both)
    hiss = np.stack([voice, voice + rng.integers(-3, 4, voice.size).astype(np.int16)], axis=1)  # ~ -70 dB difference
    assert audio.channels_identical(hiss)
    other = np.stack([voice, (rng.standard_normal(48000) * 3000).astype(np.int16)], axis=1)
    assert not audio.channels_identical(other)
    pan = np.stack([voice, (voice * 0.5).astype(np.int16)], axis=1)  # hard-panned level difference is not identical
    assert not audio.channels_identical(pan)
    assert not audio.channels_identical(np.stack([voice, -voice], axis=1))  # out of phase
    assert audio.channels_identical(np.zeros((100, 2), dtype=np.int16))


@pytest.mark.unit
def test_audio_lag_and_correlation_detect_misalignment() -> None:
    noise = np.random.default_rng(2).standard_normal(20000).astype(np.float32)
    assert audio.best_lag(noise, noise) == 0
    assert audio.best_lag(np.roll(noise, 5), noise) == 5
    assert audio.best_lag(np.roll(noise, -7), noise) == -7
    assert audio.correlation(noise, noise) == pytest.approx(1.0)
    assert abs(audio.correlation(noise, np.roll(noise, 100))) < 0.1


# ---- synthetic Unity bundle -----------------------------------------------------------------------
def _audioclip_bytes(tree: dict) -> bytes:
    from UnityPy.enums import ClassIDType
    from UnityPy.helpers import TypeTreeHelper
    from UnityPy.helpers.Tpk import get_typetree_node
    from UnityPy.helpers.UnityVersion import UnityVersion
    from UnityPy.streams import EndianBinaryWriter

    writer = EndianBinaryWriter(endian="<")
    node = get_typetree_node(ClassIDType.AudioClip.value, UnityVersion.from_str("6000.0.48f1"))
    TypeTreeHelper.write_typetree(tree, node, writer, None)
    return writer.bytes


def _serialized_file(objects: list[tuple[int, bytes]]) -> bytes:
    """Format-22 SerializedFile with AudioClip objects (class 83) and no embedded type tree."""
    meta = bytearray(b"6000.0.48f1\0" + struct.pack("<i", 2) + b"\0" + struct.pack("<i", 1))
    meta += struct.pack("<ibh", 83, 0, -1) + bytes(16) + struct.pack("<i", len(objects))
    data = bytearray()
    for path_id, payload in objects:
        meta += bytes(-(48 + len(meta)) % 4) + struct.pack("<qqIi", path_id, len(data), len(payload), 0)
        data += payload + bytes(-len(payload) % 8)
    meta += struct.pack("<iii", 0, 0, 0) + b"\0"
    data_offset = 48 + len(meta)
    data_offset += -data_offset % 16
    head = struct.pack(">IIII", 0, 0, 22, 0) + bytes(4) + struct.pack(">IqqQ", len(meta), data_offset + len(data), data_offset, 0)
    return head + bytes(meta) + bytes(data_offset - 48 - len(meta)) + bytes(data)


def _unityfs(nodes: list[tuple[str, int, bytes]]) -> bytes:
    payload = b"".join(b for _, _, b in nodes)
    info = bytearray(16) + struct.pack(">iIIH", 1, len(payload), len(payload), 0) + struct.pack(">i", len(nodes))
    offset = 0
    for name, flags, blob in nodes:
        info += struct.pack(">qqI", offset, len(blob), flags) + name.encode() + b"\0"
        offset += len(blob)
    head = b"UnityFS\0" + struct.pack(">I", 8) + b"5.x.x\0" + b"6000.0.48f1\0"
    pad = -(len(head) + 20) % 16
    return (head + struct.pack(">qIII", len(head) + 20 + pad + len(info) + len(payload), len(info), len(info), 0x40)
            + bytes(pad) + bytes(info) + payload)


def _write_audio_bundle(path: Path, clips: list[dict]) -> None:
    """clips: name, data (resource bytes), length, channels, load (default streaming)."""
    stream, objects, at = b"", [], 0
    for i, clip in enumerate(clips, 1):
        tree = {"m_Name": clip["name"], "m_LoadType": clip.get("load", audio.LOAD_STREAMING), "m_Channels": clip["channels"],
                "m_Frequency": 48000, "m_BitsPerSample": 16, "m_Length": clip["length"], "m_IsTrackerFormat": False,
                "m_Ambisonic": False, "m_SubsoundIndex": 0, "m_PreloadAudioData": False, "m_LoadInBackground": False,
                "m_Legacy3D": True, "m_Resource": {"m_Source": "archive:/CAB-t/CAB-t.resource", "m_Offset": at,
                                                   "m_Size": len(clip["data"])}, "m_CompressionFormat": 1}
        objects.append((i, _audioclip_bytes(tree)))
        stream += clip["data"]
        at += len(clip["data"])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(_unityfs([("CAB-t", 4, _serialized_file(objects)), ("CAB-t.resource", 0, stream)]))


def _tone(seconds: float, freq: float = 440.0, rate: int = 48000, amp: float = 0.4) -> np.ndarray:
    """Harmonics plus a little noise, so the encoder has something to spend bits on like real voice."""
    t = np.arange(int(seconds * rate)) / rate
    noise = np.random.default_rng(int(freq)).standard_normal(t.size) * 0.03
    return (amp * np.sin(2 * np.pi * freq * t) + 0.1 * np.sin(2 * np.pi * 3.3 * freq * t) + noise).astype(np.float32)


def _source_clip(left: np.ndarray, right: np.ndarray | None = None, quality: int = 100,
                 rate: int = 48000) -> tuple[bytes, float, int]:
    """A game-style clip: FSB5/Vorbis at FMOD quality 100 (stereo, `right` = `left` for the dual-mono voice case)."""
    channels = 2
    pcm = np.stack([left, left if right is None else right], axis=1)
    ogg = audio.encode_vorbis(pcm, rate, channels, quality)
    stream = audio.parse_ogg(ogg)
    setup, packets = stream.packets[2], stream.packets[3:]
    granules = audio.granule_positions(packets, audio.vorbis_mode_flags(setup), audio.vorbis_blocksizes(stream.packets[0]))
    crc = audio.zlib.crc32(setup) & 0xFFFFFFFF
    return audio.build_fsb5_vorbis(packets, granules, len(left), rate, channels, crc), len(left) / rate, channels


@pytest.mark.integration
@pytest.mark.parametrize("combo", [(45, 1), (45, 2), (63, 1), (63, 2), (100, 2)])
def test_audio_libvorbis_setup_headers_match_fmods_crc_table(combo: tuple[int, int]) -> None:
    quality, channels = combo
    pcm = np.tile(_tone(0.5)[:, None], (1, channels))
    stream = audio.parse_ogg(audio.encode_vorbis(pcm, 48000, channels, quality))
    crc = audio.zlib.crc32(stream.packets[2]) & 0xFFFFFFFF
    assert audio.FMOD_VORBIS_HEADERS[crc] == (quality, channels, 48000)
    assert audio.vorbis_blocksizes(stream.packets[0]) == (256, 2048)
    # our granule bookkeeping reproduces the ones libvorbis stamped on the Ogg pages
    granules = audio.granule_positions(stream.packets[3:], audio.vorbis_mode_flags(stream.packets[2]), (256, 2048))
    for index, granule in stream.page_ends[:-1]:
        if index >= 3 and granule >= 0:
            assert granules[index - 3] == granule


@pytest.mark.integration
def test_audio_rebuilt_game_style_clip_is_decoded_by_fmod() -> None:
    fsb, length, _ = _source_clip(_tone(1.5))
    decoded = audio.fmod_decode(fsb)
    assert decoded.rate == 48000 and decoded.pcm.shape == (72000, 2)
    assert abs(decoded.pcm.shape[0] / 48000 - length) < 1e-6
    assert audio.channels_identical(decoded.pcm)


@pytest.mark.integration
def test_audio_convert_dual_mono_voice_to_mono_q45() -> None:
    fsb, length, _ = _source_clip(_tone(2.0))
    res = audio.convert_clip(audio.ClipTask(("n", 0, len(fsb)), "v", "voice", fsb, length, 45))
    assert res.status == "converted", res.reason
    assert (res.channels, res.rate, res.samples) == (1, 48000, 96000)
    assert res.stats["peak_dbfs"] <= 0 and res.stats["duration_error_ms"] <= 1 and res.stats["lag_samples"] == 0
    assert res.stats["correlation"] > 0.99 and len(res.data) < len(fsb) * 0.45
    clip = audio.parse_fsb5_vorbis(res.data)
    assert audio.FMOD_VORBIS_HEADERS[clip.crc] == (45, 1, 48000)
    assert audio.fmod_decode(res.data).pcm.shape == (96000, 1)
    # the next run recognises the result and leaves it alone
    again = audio.convert_clip(audio.ClipTask(("n", 0, len(res.data)), "v", "voice", res.data, length, 45))
    assert (again.status, again.code) == ("skipped", "already-converted")


@pytest.mark.integration
def test_audio_stereo_stays_stereo_where_it_must() -> None:
    left, right = _tone(1.5, 330.0), _tone(1.5, 523.0)
    fsb, length, _ = _source_clip(left, right)
    voice = audio.convert_clip(audio.ClipTask(("n", 0, len(fsb)), "v", "voice", fsb, length, 45))
    assert voice.status == "converted" and voice.channels == 2  # different channels: no downmix
    assert audio.FMOD_VORBIS_HEADERS[audio.parse_fsb5_vorbis(voice.data).crc] == (45, 2, 48000)
    dual, length, _ = _source_clip(_tone(1.5))
    music = audio.convert_clip(audio.ClipTask(("n", 0, len(dual)), "Bgm_001_001_Loop", "music", dual, length, 63))
    assert music.status == "converted" and music.channels == 2  # music is never downmixed
    sfx = audio.convert_clip(audio.ClipTask(("n", 0, len(dual)), "Sfx_001", "sfx", dual, length, 63))
    assert sfx.status == "converted" and sfx.channels == 1
    assert audio.FMOD_VORBIS_HEADERS[audio.parse_fsb5_vorbis(sfx.data).crc] == (63, 1, 48000)


def _played_rms_db(fsb: bytes, wav_path: Path) -> float:
    """RMS (dB) of what FMOD sends to a stereo output for a centred 2D channel, the way Unity's AudioSource plays it."""
    import wave

    from fmod_toolkit.importer import import_pyfmodex

    fmod = import_pyfmodex()
    system = fmod.System()
    system.output = fmod.enums.OUTPUTTYPE.WAVWRITER_NRT
    system.init(8, fmod.flags.INIT_FLAGS.NORMAL, str(wav_path).encode())
    sound = system.create_sound(fsb, fmod.flags.MODE.OPENMEMORY,
                                exinfo=fmod.structure_declarations.CREATESOUNDEXINFO(length=len(fsb)))
    channel = system.play_sound(sound.get_subsound(0), paused=False)
    channel.pan = 0.0  # SoundChannelInstance::UpdateStereoPan calls FMOD::Channel::setPan
    for _ in range(120):
        system.update()
    system.release()
    with wave.open(str(wav_path)) as w:
        raw = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2" if w.getsampwidth() == 2 else "<f4")
    out = raw.astype(np.float64).reshape(-1, w.getnchannels())
    out = out[np.abs(out).max(axis=1) > (1 if raw.dtype == np.int16 else 1e-4)]
    return float(10 * np.log10(np.mean(out**2)))


@pytest.mark.integration
def test_audio_downmixed_voice_plays_as_loud_as_the_stereo_original(tmp_path: Path) -> None:
    fsb, length, _ = _source_clip(_tone(2.0))
    task = audio.ClipTask(("n", 0, len(fsb)), "v", "voice", fsb, length, 45)
    stereo = _played_rms_db(fsb, tmp_path / "stereo.wav")
    compensated = audio.convert_clip(task)
    plain = audio.convert_clip(dataclasses.replace(task, mono_gain_db=0.0))
    assert compensated.status == plain.status == "converted"
    assert abs(_played_rms_db(compensated.data, tmp_path / "comp.wav") - stereo) < 0.5
    # FMOD's constant-power pan puts a centred mono source 3 dB down on each speaker: why the default adds PAN_LAW_DB
    assert abs((_played_rms_db(plain.data, tmp_path / "plain.wav") - stereo) + audio.PAN_LAW_DB) < 0.5


@pytest.mark.integration
def test_audio_cli_wiring(tmp_path: Path) -> None:
    import json

    fsb, length, _ = _source_clip(_tone(1.0))
    data_dir = tmp_path / "Data"
    bundle = data_dir / "Raw" / "aa" / "StandaloneOSX" / "naninovel-audio_assets_audio-sfxnaninovel.bundle"
    _write_audio_bundle(bundle, [{"name": "Sfx_Test", "data": fsb, "length": length, "channels": 2}])
    report = tmp_path / "r.json"
    argv = [str(data_dir), "--music-quality", "63", "--mono-gain-db", "0", "--jobs", "1", "--report", str(report)]
    assert audio.main(argv) == 0
    info = json.loads(report.read_text())
    assert info["mono_gain_db"] == 0.0 and info["quality"]["sfx"] == 63
    clip = info["bundles"][0]["details"][0]
    assert clip["channels_after"] == 1 and clip["gain_db"] == 0.0 and abs(clip["level_error_db"]) < 1.5


@pytest.mark.integration
def test_audio_loud_clip_gets_headroom_instead_of_clipping() -> None:
    square = np.where(np.sin(2 * np.pi * 220 * np.arange(48000) / 48000) > 0, 1.0, -1.0).astype(np.float32)
    fsb, length, _ = _source_clip(square * 0.999)
    res = audio.convert_clip(audio.ClipTask(("n", 0, len(fsb)), "Bgm_x", "music", fsb, length, 63))
    assert res.status == "converted", res.reason
    assert res.stats["peak_dbfs"] <= 0
    assert res.stats["attempts"] == 2 and res.stats["gain_db"] < 0  # first encode overshoots, the retry has headroom


@pytest.mark.integration
def test_audio_unknown_setup_crc_keeps_the_clip(tmp_path: Path) -> None:
    fsb, length, _ = _source_clip(_tone(1.0))
    res = audio.convert_clip(audio.ClipTask(("n", 0, len(fsb)), "v", "voice", fsb, length, 47))  # q47 mono is not in FMOD's table
    assert (res.status, res.code, res.data) == ("skipped", "unknown-crc", None)
    junk = audio.convert_clip(audio.ClipTask(("n", 0, 64), "v", "voice", bytes(64), 1.0, 45))
    assert (junk.status, junk.code) == ("skipped", "unsupported")


@pytest.mark.integration
def test_audio_bundle_rewrite_is_verified_idempotent_and_updates_fields(tmp_path: Path) -> None:
    import json

    import UnityPy

    voice, voice_len, _ = _source_clip(_tone(2.0))
    stereo, stereo_len, _ = _source_clip(_tone(1.0, 330.0), _tone(1.0, 523.0))
    clips = [
        {"name": "0101Adv01_Ema001", "data": voice, "length": voice_len, "channels": 2},
        {"name": "0101Adv02_Ema002", "data": stereo, "length": stereo_len, "channels": 2},
        {"name": "Song_004_001", "data": bytes(96), "length": 3.0, "channels": 2, "load": audio.LOAD_DECOMPRESS_ON_LOAD},
        {"name": "Long", "data": bytes(32), "length": 130.0, "channels": 2, "load": audio.LOAD_DECOMPRESS_ON_LOAD},
    ]
    data_dir = tmp_path / "Data"
    bundle = data_dir / "Raw" / "aa" / "StandaloneOSX" / "general-voice-act01_chapter01_trial_assets_all.bundle"
    _write_audio_bundle(bundle, clips)
    before = bundle.stat().st_size
    report_path = tmp_path / "report.json"
    assert audio.run(data_dir, 45, 63, jobs=1, report_path=report_path) == 0
    assert not list(bundle.parent.glob("*.tmp"))
    assert bundle.stat().st_size < before

    env = UnityPy.load(str(bundle))
    trees = {o.read_typetree()["m_Name"]: o.read_typetree() for o in env.objects}
    node = memoryview(env.file.files["CAB-t.resource"].bytes)
    assert trees["0101Adv01_Ema001"]["m_Channels"] == 1 and trees["0101Adv02_Ema002"]["m_Channels"] == 2
    assert trees["Song_004_001"]["m_LoadType"] == trees["Long"]["m_LoadType"] == audio.LOAD_STREAMING
    assert trees["0101Adv01_Ema001"]["m_LoadType"] == audio.LOAD_STREAMING
    spans = sorted((t["m_Resource"]["m_Offset"], t["m_Resource"]["m_Size"]) for t in trees.values())
    assert spans[0][0] == 0 and all(a[0] + a[1] == b[0] for a, b in zip(spans, spans[1:], strict=False))
    assert spans[-1][0] + spans[-1][1] == len(node)  # tightly packed, nothing left over
    for name in ("0101Adv01_Ema001", "0101Adv02_Ema002"):
        res = trees[name]["m_Resource"]
        pcm = audio.fmod_decode(bytes(node[res["m_Offset"] : res["m_Offset"] + res["m_Size"]]))
        assert pcm.pcm.shape[1] == trees[name]["m_Channels"] and pcm.rate == 48000
    assert bytes(node[trees["Long"]["m_Resource"]["m_Offset"] :][:32]) == bytes(32)  # untouched clip bytes survive
    info = json.loads(report_path.read_text())["bundles"][0]
    assert info["status"] == "converted" and info["clips"] == {"converted": 2, "skipped": 2}
    assert info["skipped"] == {"unsupported": 2} and sorted(info["load_type_changes"]) == ["Long", "Song_004_001"]

    first = bundle.read_bytes()
    assert audio.run(data_dir, 45, 63, jobs=1) == 0  # converted clips are recognised, nothing is rewritten
    assert bundle.read_bytes() == first


def _voice_bundle(tmp_path: Path, clips: list[dict]) -> tuple[Path, Path]:
    data_dir = tmp_path / "Data"
    bundle = data_dir / "Raw" / "aa" / "StandaloneOSX" / "general-voice-act01_chapter02_trial_assets_all.bundle"
    _write_audio_bundle(bundle, clips)
    return data_dir, bundle


def _resource_slices(bundle: Path) -> dict[str, tuple[dict, bytes]]:
    import UnityPy

    env = UnityPy.load(str(bundle))
    node = memoryview(env.file.files["CAB-t.resource"].bytes)
    trees = {o.read_typetree()["m_Name"]: o.read_typetree() for o in env.objects}
    return {n: (t, bytes(node[t["m_Resource"]["m_Offset"] :][: t["m_Resource"]["m_Size"]])) for n, t in trees.items()}


@pytest.mark.unit
def test_audio_failed_clips_keep_their_bytes_and_the_rest_of_the_bundle_is_saved(
        tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    import json

    crc = next(c for c, v in audio.FMOD_VORBIS_HEADERS.items() if v == (45, 1, 48000))
    fake_fsb = audio.build_fsb5_vorbis([bytes([0]) + b"x" * 30] * 3, [0, 300, 600], 48000, 48000, 1, crc)
    bad_bytes = bytes(range(64))

    def fake_convert(task: audio.ClipTask) -> audio.ClipResult:
        if task.name == "bad":
            return audio.ClipResult(task.key, task.name, task.kind, "kept", reason="correlation 0.313")
        return audio.ClipResult(task.key, task.name, task.kind, "converted", data=fake_fsb, channels=1, rate=48000,
                                samples=48000, stats={"bytes_before": len(task.data), "bytes_after": len(fake_fsb)})

    monkeypatch.setattr(audio, "convert_clip", fake_convert)
    clips = [{"name": "a", "data": bytes(300), "length": 1.0, "channels": 2},
             {"name": "bad", "data": bad_bytes, "length": 1.0, "channels": 2},
             {"name": "c", "data": bytes(200), "length": 1.0, "channels": 2}]
    data_dir, bundle = _voice_bundle(tmp_path, clips)
    report_path = tmp_path / "report.json"
    assert audio.run(data_dir, 45, 63, jobs=1, report_path=report_path) == 0  # a kept clip is not an error
    slices = _resource_slices(bundle)
    assert slices["bad"][1] == bad_bytes and slices["bad"][0]["m_Channels"] == 2  # untouched
    for good in ("a", "c"):
        assert slices[good][1] == fake_fsb and slices[good][0]["m_Channels"] == 1
    spans = sorted((t["m_Resource"]["m_Offset"], t["m_Resource"]["m_Size"]) for t, _ in slices.values())
    assert spans[0][0] == 0 and all(a[0] + a[1] == b[0] for a, b in zip(spans, spans[1:], strict=False))
    info = json.loads(report_path.read_text())["bundles"][0]
    assert info["status"] == "converted" and info["clips"] == {"converted": 2, "kept": 1}
    assert info["kept"] == [{"name": "bad", "kind": "voice", "reason": "correlation 0.313"}]


@pytest.mark.unit
def test_audio_bundle_where_every_clip_is_kept_is_left_byte_identical(
        tmp_path: Path, monkeypatch: pytest.MonkeyPatch, caplog: pytest.LogCaptureFixture) -> None:
    monkeypatch.setattr(audio, "convert_clip", lambda t: audio.ClipResult(t.key, t.name, t.kind, "kept", reason="boom"))
    data_dir, bundle = _voice_bundle(tmp_path, [{"name": "x", "data": bytes(100), "length": 1.0, "channels": 2}])
    before = bundle.read_bytes()
    with caplog.at_level("WARNING", logger="transcode_audio"):
        assert audio.run(data_dir, 45, 63, jobs=1) == 0
    assert bundle.read_bytes() == before and not list(bundle.parent.glob("*.tmp"))
    assert "x (boom)" in caplog.text or "kept x unchanged: boom" in caplog.text  # the clip is named in the log


@pytest.mark.unit
def test_audio_failed_read_back_refuses_the_bundle(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    crc = next(c for c, v in audio.FMOD_VORBIS_HEADERS.items() if v == (45, 1, 48000))
    fake_fsb = audio.build_fsb5_vorbis([bytes([0]) + b"x" * 30] * 3, [0, 300, 600], 48000, 48000, 1, crc)
    monkeypatch.setattr(audio, "convert_clip", lambda t: audio.ClipResult(
        t.key, t.name, t.kind, "converted", data=fake_fsb, channels=1, rate=48000, samples=48000))

    def broken(*_args: object) -> None:
        raise audio.AudioError("offsets do not match")

    monkeypatch.setattr(audio, "_read_back", broken)
    data_dir, bundle = _voice_bundle(tmp_path, [{"name": "x", "data": bytes(100), "length": 1.0, "channels": 2}])
    before = bundle.read_bytes()
    assert audio.run(data_dir, 45, 63, jobs=1) == 1  # structural failure: nothing is written
    assert bundle.read_bytes() == before and not list(bundle.parent.glob("*.tmp"))


@pytest.mark.integration
def test_audio_clip_that_fails_verification_does_not_block_its_neighbours(
        tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    good, good_len, _ = _source_clip(_tone(1.0))
    bad, bad_len, _ = _source_clip(_tone(1.0, 330.0))
    real = audio._convert_clip

    def flaky(task: audio.ClipTask, result: audio.ClipResult) -> audio.ClipResult:
        if task.name == "bad":
            raise audio.AudioError("synthetic verification failure")
        return real(task, result)

    monkeypatch.setattr(audio, "_convert_clip", flaky)
    data_dir, bundle = _voice_bundle(tmp_path, [{"name": "good", "data": good, "length": good_len, "channels": 2},
                                                 {"name": "bad", "data": bad, "length": bad_len, "channels": 2}])
    assert audio.run(data_dir, 45, 63, jobs=1, report_path=tmp_path / "r.json") == 0
    slices = _resource_slices(bundle)
    assert slices["bad"][1] == bad and slices["bad"][0]["m_Channels"] == 2
    assert slices["good"][0]["m_Channels"] == 1 and len(slices["good"][1]) < len(good)
    assert audio.fmod_decode(slices["good"][1]).pcm.shape[1] == 1
    first = bundle.read_bytes()
    assert audio.run(data_dir, 45, 63, jobs=1) == 0  # the converted clip is recognised; the kept one is retried and kept
    assert bundle.read_bytes() == first


@pytest.mark.integration
def test_audio_near_silent_placeholder_clip_converts_instead_of_failing(tmp_path: Path) -> None:
    # Placeholder lines are +-3 LSB dither with independent channels: a codec cannot (and need not) reproduce the noise.
    noise = np.random.default_rng(3).integers(-3, 4, (72000, 2)).astype(np.float32) / 32768.0
    fsb, length, _ = _source_clip(noise[:, 0], noise[:, 1])
    res = audio.convert_clip(audio.ClipTask(("n", 0, len(fsb)), "silent", "voice", fsb, length, 45))
    assert res.status == "converted", res.reason
    assert res.channels == 2 and res.stats["peak_dbfs"] < -60  # stereo kept, and the result is still silent


@pytest.mark.integration
def test_audio_only_filter_and_bundles_without_audio(tmp_path: Path) -> None:
    voice, voice_len, _ = _source_clip(_tone(1.0))
    root = tmp_path / "Data" / "Raw" / "aa" / "StandaloneOSX"
    for name in ("general-voice-a.bundle", "general-voice-b.bundle"):
        _write_audio_bundle(root / name, [{"name": name, "data": voice, "length": voice_len, "channels": 2}])
    (root / "textures.bundle").write_bytes(audio.bundle_node_names.__doc__.encode())  # not a UnityFS file: ignored
    sizes = {p.name: p.stat().st_size for p in root.iterdir()}
    assert audio.run(tmp_path / "Data", 45, 63, only="general-voice-b*", jobs=1) == 0
    after = {p.name: p.stat().st_size for p in root.iterdir()}
    assert after["general-voice-a.bundle"] == sizes["general-voice-a.bundle"]
    assert after["general-voice-b.bundle"] < sizes["general-voice-b.bundle"]
    assert after["textures.bundle"] == sizes["textures.bundle"]
    assert audio.bundle_node_names(root / "general-voice-a.bundle") == ["CAB-t", "CAB-t.resource"]
    assert audio.bundle_node_names(root / "textures.bundle") == []
    with pytest.raises(SystemExit):
        audio.find_bundles(tmp_path / "missing", None)


import patch_character_ppu as pcp  # noqa: E402


def _fake_characters_config(n: int, ppu: float = 100.0) -> bytes:
    entry = struct.pack("<3fIf", 0.5, 0.0, ppu, 0, 0.5) + b"\0" * 20  # Pivot, PPU, EnableDepthPass, DepthAlphaCutoff, ...
    return b"\0" * 28 + struct.pack("<i", 23) + b"CharactersConfiguration\0" + entry * n


@pytest.mark.unit
def test_character_ppu_patch_changes_every_actor_entry() -> None:
    raw = _fake_characters_config(5)
    new, changed = pcp.patch_raw(raw)
    assert changed == 5 and len(new) == len(raw)
    assert len(pcp.ppu_offsets(new, (75.0,))) == 5 and not pcp.ppu_offsets(new, (100.0,))
    assert pcp.patch_raw(new) == (new, 0)  # idempotent


@pytest.mark.unit
def test_character_ppu_patch_ignores_unrelated_floats() -> None:
    raw = _fake_characters_config(1) + struct.pack("<3fIf", 3.0, 7.0, 100.0, 0, 0.5)  # pivot out of range
    assert pcp.patch_raw(raw)[1] == 1


@pytest.mark.unit
def test_settings_bundle_defaults_match_the_registered_defaults() -> None:
    import plistlib
    import re

    extras = Path(__file__).resolve().parents[1] / "tools" / "shell-extras"
    root = plistlib.loads((extras / "Settings.bundle" / "Root.plist").read_bytes())
    specs = [s for s in root["PreferenceSpecifiers"] if "Key" in s]
    plist_defaults = {s["Key"]: s["DefaultValue"] for s in specs if s["Type"] != "PSTitleValueSpecifier"}
    for s in specs:
        if s["Type"] == "PSMultiValueSpecifier":
            assert len(s["Titles"]) == len(s["Values"]) and s["DefaultValue"] in s["Values"]
    code = (extras / "ManosabaSettings.mm").read_text()
    registered = {}
    for key, value in re.findall(r'@"(manosaba_\w+)":\s*@(YES|NO|-?\d+)', code):
        registered[key] = {"YES": True, "NO": False}.get(value, int(value) if value.lstrip("-").isdigit() else value)
    assert registered == plist_defaults
    # every key the shell reads is declared in the Settings page
    used = set()
    for f in extras.glob("*.mm"):
        used |= set(re.findall(r'ManosabaSetting(?:Bool|Int)\(@"(manosaba_\w+)"\)', f.read_text()))
    assert used <= set(plist_defaults)


@pytest.mark.integration
def test_drive_refuses_a_tag_that_leaves_its_log_folder(tmp_path: Path) -> None:
    import os

    root = Path(__file__).resolve().parents[1]
    script = root / "tools" / "drive.sh"
    victim = tmp_path / "victim"
    victim.mkdir()
    (victim / "precious.txt").write_text("keep me")
    escape = os.path.relpath(victim, root / "logs" / "drive")  # ../../../..../victim: no trailing "..", rm takes it
    for tag in (escape, "../..", "..", "a/b", ""):
        run = subprocess.run([str(script), tag, "shot:x"], capture_output=True, text=True,
                             env={"PATH": "/usr/bin:/bin", "MANOSABA_SIM": "none"}, timeout=30)
        assert run.returncode != 0, tag
        assert (victim / "precious.txt").read_text() == "keep me", tag


@pytest.mark.integration
def test_stage_data_refuses_a_folder_that_is_not_the_unity_project(tmp_path: Path) -> None:
    script = Path(__file__).resolve().parents[1] / "tools" / "stage_data.sh"
    app = tmp_path / "fake.app"
    (app / "Contents/Resources/Data").mkdir(parents=True)
    (app / "Contents/Resources/Data/globalgamemanagers").write_bytes(b"x")
    # A folder that looks enough like staged data that the copies before the delete succeed.
    victim = tmp_path / "home"
    (victim / "Data/Resources").mkdir(parents=True)
    (victim / "Data/Managed/mono").mkdir(parents=True)
    (victim / "Data/Resources/unity default resources").write_bytes(b"x")
    (victim / "Data" / "precious.txt").write_text("keep me")
    run = subprocess.run([str(script), str(app), str(victim)], capture_output=True, text=True, timeout=30)
    assert run.returncode != 0
    assert (victim / "Data" / "precious.txt").read_text() == "keep me"
    for bad in ("", "/"):
        run = subprocess.run([str(script), str(app), bad], capture_output=True, text=True, timeout=30)
        assert run.returncode != 0 and "Unity" in run.stderr, bad
