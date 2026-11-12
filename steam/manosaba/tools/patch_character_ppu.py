#!/usr/bin/env python3
"""Render the game's layered characters at a lower pixels-per-unit on iOS.

Each Naninovel layered character is composited into its own render texture of
RenderCanvas.Size (15 x 30 units) x OrthoActorMetadata.PixelsPerUnit (100) = 1500 x 3000 px
(colour + depth + an HDR target), every frame it is on screen. Scenes with many characters
(trials) need several of those per frame, which saturates the A12Z: 16-24 fps and a device
that heats up to "serious". The on-screen size of a character is texture size / PPU, i.e.
RenderCanvas.Size, so lowering PPU only lowers the render resolution: at 75 the targets are
1125 x 2250 (-44% pixels) while characters keep their size and placement.

Patches every actor metadata entry in the CharactersConfiguration asset (Data/resources.assets),
found by its layout (Pivot.x, Pivot.y, PixelsPerUnit, EnableDepthPass, DepthAlphaCutoff); the
asset has no typetree. Idempotent.
"""
from __future__ import annotations

import argparse
import logging
import struct
import sys
from pathlib import Path

import UnityPy

log = logging.getLogger("patch_character_ppu")

CONFIG = "CharactersConfiguration"
ORIGINAL_PPU = 100.0
TARGET_PPU = 75.0


def ppu_offsets(raw: bytes, ppus: tuple[float, ...]) -> list[int]:
    """Offsets of each OrthoActorMetadata.PixelsPerUnit whose value is in `ppus`."""
    hits = []
    for i in range(8, len(raw) - 12, 4):
        px, py, ppu = struct.unpack_from("<3f", raw, i - 8)
        depth_pass, cutoff = struct.unpack_from("<If", raw, i + 4)
        if ppu in ppus and 0 <= px <= 1 and 0 <= py <= 1 and depth_pass in (0, 1) and 0 <= cutoff <= 1:
            hits.append(i)
    return hits


def patch_raw(raw: bytes, ppu: float = TARGET_PPU) -> tuple[bytes, int]:
    """Returns (patched data, number of actor entries changed)."""
    offsets = ppu_offsets(raw, (ORIGINAL_PPU, ppu))
    if not offsets:
        raise ValueError("no actor metadata found")
    out = bytearray(raw)
    changed = 0
    for off in offsets:
        if struct.unpack_from("<f", out, off)[0] != ppu:
            struct.pack_into("<f", out, off, ppu)
            changed += 1
    return bytes(out), changed


def object_name(raw: bytes) -> str:
    n = struct.unpack_from("<i", raw, 28)[0]
    return raw[32:32 + n].decode("utf-8", "replace") if 0 < n < 256 else ""


def patch_assets(path: Path, ppu: float = TARGET_PPU) -> int:
    env = UnityPy.load(str(path))
    for obj in env.objects:
        if obj.type.name != "MonoBehaviour":
            continue
        raw = obj.get_raw_data()
        if object_name(raw) != CONFIG:
            continue
        new, changed = patch_raw(raw, ppu)
        log.info("%s: %d actor entries at PPU %g, %d changed", CONFIG, len(ppu_offsets(new, (ppu,))), ppu, changed)
        if changed:
            obj.set_raw_data(new)
            tmp = path.with_name(path.name + ".tmp")
            tmp.write_bytes(env.file.save())
            tmp.replace(path)
        return changed
    raise ValueError(f"{path.name}: no {CONFIG}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("data_dir", type=Path, help="staged Data/ directory (modified in place)")
    ap.add_argument("--ppu", type=float, default=TARGET_PPU)
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    patch_assets(args.data_dir / "resources.assets", args.ppu)
    return 0


if __name__ == "__main__":
    sys.exit(main())
