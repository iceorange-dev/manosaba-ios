#!/usr/bin/env python3
"""Fill the game's empty mobile resolution table so its iOS code path does not crash.

CameraManagerExtended.SetResolutionLevel takes the mobile branch on iOS and indexes
CameraConfigurationExtended.ResolutionCoefficientsForMobile, which ships empty (the game
only ever ran on PC). The IndexOutOfRangeException kills Naninovel's post-initialization
and the game sits on its loading bar forever.

We give it one coefficient per PC resolution level, proportional to the PC heights
(2560x1440 -> 1.0 ... 1280x720 -> 0.5), so the in-game resolution option keeps working.
Operates on Data/resources.assets in place; idempotent.
"""
from __future__ import annotations

import argparse
import logging
import struct
import sys
from pathlib import Path

import UnityPy

log = logging.getLogger("patch_game_config")

NAME = b"CameraConfigurationExtended"


def find_config(env):
    for obj in env.objects:
        if obj.type.name != "MonoBehaviour":
            continue
        raw = obj.get_raw_data()
        if len(raw) > 32 + len(NAME) and struct.unpack_from("<i", raw, 28)[0] == len(NAME) and raw[32:32 + len(NAME)] == NAME:
            return obj, bytes(raw)
    raise ValueError("CameraConfigurationExtended not found")


def patched(raw: bytes) -> bytes | None:
    """New object bytes, or None when the table is already filled."""
    off = 32 + ((len(NAME) + 3) & ~3)
    n_pc = struct.unpack_from("<i", raw, off)[0]
    if not 1 <= n_pc <= 32:
        raise ValueError(f"unexpected ResolutionsForPc length {n_pc}")
    pcs = [struct.unpack_from("<ii", raw, off + 4 + 8 * i) for i in range(n_pc)]
    mob = off + 4 + 8 * n_pc
    n_mob = struct.unpack_from("<i", raw, mob)[0]
    log.info("ResolutionsForPc=%s, ResolutionCoefficientsForMobile length=%d", pcs, n_mob)
    if n_mob:
        return None
    top = max(h for _, h in pcs)
    coeffs = [round(h / top, 4) for _, h in pcs]
    log.info("filling ResolutionCoefficientsForMobile=%s", coeffs)
    table = struct.pack("<i", len(coeffs)) + struct.pack(f"<{len(coeffs)}f", *coeffs)
    return raw[:mob] + table + raw[mob + 4:]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("data_dir", type=Path)
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    path = args.data_dir / "resources.assets"
    env = UnityPy.load(str(path))
    try:
        obj, raw = find_config(env)
        new = patched(raw)
    except ValueError as e:
        log.error("%s", e)
        return 3
    if new is None:
        log.info("already filled")
        return 0
    obj.set_raw_data(new)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_bytes(env.file.save())
    tmp.replace(path)
    log.info("wrote %s", path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
