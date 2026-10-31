#!/usr/bin/env python3
"""Shrink the game's dynamic TextMeshPro font atlases from 4096x4096 to 2048x2048.

The two CJK fonts (SourceHanSerifSC, TsukushiMincho) are dynamic TMP font assets: glyphs are
rendered into an Alpha8 atlas at runtime. Every time new glyphs are added the whole atlas is
uploaded again, and Unity's Metal backend allocates a fresh texture for each full upload, so a
page of new Chinese text means hundreds of 16 MB allocations in a burst. On an A12Z iPad
that shows up as 250-300 ms hitches and GPU memory spikes of ~1 GB. The fonts have
multi-atlas enabled, so a 2048x2048 atlas only means more (4 MB) pages, not missing glyphs.

The TMP_FontAsset MonoBehaviours have no typetree; the live m_AtlasWidth/m_AtlasHeight are
found by their neighbours (m_AtlasPadding, m_AtlasRenderMode=SDFAA). The packing state that
follows them must shrink too: the (empty) used-rect list and the single free rect covering
the whole atlas, (0, 0, W-1, H-1) for SDF modes; left at 4095 TMP packs glyphs outside the
2048 atlas and text renders as garbage. The font's materials carry _TextureWidth/_TextureHeight
(used for underlay/shadow offsets) and are updated as well. m_CreationSettings, which holds
the same size for the editor, is left alone. Idempotent.
"""
from __future__ import annotations

import argparse
import logging
import struct
import sys
from pathlib import Path

import UnityPy

log = logging.getLogger("patch_font_atlases")

FONTS = {
    "general-fonts-sourcehanserifsc_assets_all.bundle": "SourceHanSerifSC",
    "general-fonts-tsukushimincho_assets_all.bundle": "TsukushiMincho",
}
SDFAA = 4165  # GlyphRenderMode.SDFAA
TARGET = 2048


def atlas_size_offset(raw: bytes) -> int:
    """Offset of the live (m_AtlasWidth, m_AtlasHeight) pair in a TMP_FontAsset's raw data."""
    hits = []
    for i in range(0, len(raw) - 15, 4):
        w, h, padding, mode = struct.unpack_from("<iiii", raw, i)
        if mode == SDFAA and 0 < padding < 64 and w in (512, 1024, 2048, 4096, 8192) and h == w:
            hits.append(i)
    if len(hits) != 1:
        raise ValueError(f"expected one atlas size field, found {len(hits)} at {hits}")
    return hits[0]


def patch_raw(raw: bytes, size: int = TARGET) -> tuple[bytes, int]:
    """Set the live atlas size and the matching initial free glyph rect; returns (data, old size)."""
    off = atlas_size_offset(raw)
    old = struct.unpack_from("<i", raw, off)[0]
    used = struct.unpack_from("<i", raw, off + 16)[0]
    free = struct.unpack_from("<i", raw, off + 20)[0]
    rect = struct.unpack_from("<4i", raw, off + 24)
    if (used, free, rect) != (0, 1, (0, 0, old - 1, old - 1)):
        raise ValueError(f"unexpected packing state used={used} free={free} rect={rect}: atlas not pristine")
    out = bytearray(raw)
    struct.pack_into("<ii", out, off, size, size)
    struct.pack_into("<4i", out, off + 24, 0, 0, size - 1, size - 1)
    return bytes(out), old


def patch_materials(env, size: int) -> int:
    """Point every font material's _TextureWidth/_TextureHeight at the new atlas size."""
    changed = 0
    for obj in env.objects:
        if obj.type.name != "Material":
            continue
        mat = obj.read()
        floats = mat.m_SavedProperties.m_Floats
        dirty = False
        for i, (key, value) in enumerate(floats):
            if key in ("_TextureWidth", "_TextureHeight") and value != size:
                floats[i] = (key, float(size))
                dirty = True
        if dirty:
            mat.save()
            changed += 1
    return changed


def font_name(raw: bytes) -> str:
    # MonoBehaviour: m_GameObject (12) + m_Enabled (4, aligned) + m_Script (12) + m_Name
    n = struct.unpack_from("<i", raw, 28)[0]
    return raw[32:32 + n].decode("utf-8", "replace") if 0 < n < 256 else ""


def patch_bundle(path: Path, name: str, size: int = TARGET) -> int | None:
    """Returns the previous atlas size, or None when the bundle already uses `size`."""
    env = UnityPy.load(str(path))
    for obj in env.objects:
        if obj.type.name != "MonoBehaviour":
            continue
        raw = obj.get_raw_data()
        if font_name(raw) != name:
            continue
        new, old = patch_raw(raw, size)
        materials = patch_materials(env, size)
        if old == size and not materials:
            return None
        obj.set_raw_data(new)
        log.info("%s: %d materials updated", name, materials)
        tmp = path.with_name(path.name + ".tmp")
        tmp.write_bytes(env.file.save(packer="lz4"))
        tmp.replace(path)
        return old
    raise ValueError(f"{path.name}: no TMP font asset named {name}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("data_dir", type=Path, help="staged Data/ directory (modified in place)")
    ap.add_argument("--size", type=int, default=TARGET)
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    root = args.data_dir / "Raw" / "aa" / "StandaloneOSX"
    for bundle, name in FONTS.items():
        old = patch_bundle(root / bundle, name, args.size)
        if old is None:
            log.info("%s: atlas already %dx%d", name, args.size, args.size)
        else:
            log.info("%s: atlas %dx%d -> %dx%d", name, old, old, args.size, args.size)
    return 0


if __name__ == "__main__":
    sys.exit(main())
