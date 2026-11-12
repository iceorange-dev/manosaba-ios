#!/usr/bin/env python3
"""Re-encode the game's BC (DXT/BC4/BC7, incl. crunched) textures into iOS-native formats.

The iOS Unity player has no software BC decoder ("Failed to decompress ... DXT5|BC3"),
so every BC texture in the staged Data/ is converted in place:

    DXT1, DXT1Crunched            -> ASTC 6x6   (3.56 bpp, <= DXT1 memory)
    DXT5, DXT5Crunched, BC7, BC5  -> ASTC 4x4   (8 bpp, == DXT5 memory)
    BC4                           -> R8         (single channel, uncompressed)

Large uncompressed textures (RGBA32 / RGB24, mostly the naninovel-spawn CGs, ~0.9 GB of GPU
memory) are compressed too, because uploading them is what pushes the app past the iOS
memory limit: RGBA32 -> ASTC 4x4 (4x smaller), RGB24 -> ASTC 6x6. Readable textures and
textures under 128 px are left alone.

Every converted texture is decoded again and compared against the original (PSNR).
Idempotent: files without BC textures are left untouched.
"""
from __future__ import annotations

import argparse
import json
import logging
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import astc_encoder as ae
import numpy as np
import UnityPy
from PIL import Image
from UnityPy.enums import TextureFormat as TF
from UnityPy.export import Texture2DConverter as conv
from UnityPy.streams import EndianBinaryWriter

log = logging.getLogger("transcode_textures")

ASTC_4x4, ASTC_6x6 = TF.ASTC_RGB_4x4, TF.ASTC_RGB_6x6  # Unity 6: ASTC_4x4 (48) / ASTC_6x6 (50), RGBA-capable
TARGET = {
    TF.DXT1: ASTC_6x6, TF.DXT1Crunched: ASTC_6x6,
    TF.DXT5: ASTC_4x4, TF.DXT5Crunched: ASTC_4x4, TF.BC7: ASTC_4x4, TF.BC5: ASTC_4x4,
    TF.BC4: TF.R8,
    TF.RGBA32: ASTC_4x4, TF.ARGB32: ASTC_4x4, TF.BGRA32: ASTC_4x4,
    TF.RGB24: ASTC_6x6,
}
UNCOMPRESSED = {TF.RGBA32, TF.ARGB32, TF.BGRA32, TF.RGB24}
MIN_UNCOMPRESSED_SIDE = 128
# Character sprite atlases (up to 8192x16384) and the UI atlases are ~3x oversampled for an
# 11" iPad: ASTC 6x6 instead of 4x4 halves their GPU memory (~-390 MiB for the full cast)
# at PSNR 53 dB (characters, device scale) / 44-50 dB (UI atlases).
PREFER_6x6_BUNDLES = ("naninovel-characters_", "general-sprites_")
PREFER_6x6_MIN_SIDE = 2048
# Objects other than Texture2D that may keep data in a bundle's .resS; such bundles keep the
# converted textures inline instead of getting their .resS rebuilt.
OTHER_RESS_USERS = {"Mesh", "Texture3D", "Texture2DArray", "Cubemap", "CubemapArray"}
BLOCK = {ASTC_4x4: (4, 4), ASTC_6x6: (6, 6)}
ASTC_QUALITY = 60  # astcenc "medium"; ~0.1 s per 4096x2048 on 14 threads
MIN_PSNR = 32.0
UNITY_VERSION = (6000, 0, 48, 0)

_contexts: dict[tuple[int, int], ae.ASTCContext] = {}


@dataclass(frozen=True)
class Converted:
    name: str
    src: str
    dst: str
    size: tuple[int, int]
    mips: int
    psnr: float
    kept: bool = False


def astc_context(block: tuple[int, int]) -> ae.ASTCContext:
    if block not in _contexts:
        cfg = ae.ASTCConfig(ae.ASTCProfile.LDR, *block, block_z=1, quality=ASTC_QUALITY,
                            flags=ae.ASTCConfigFlags.USE_DECODE_UNORM8)
        _contexts[block] = ae.ASTCContext(cfg, threads=os.cpu_count() or 4)
    return _contexts[block]


def mip_chain(img: Image.Image, count: int) -> list[Image.Image]:
    levels = [img]
    w, h = img.size
    for _ in range(count - 1):
        w, h = max(1, w // 2), max(1, h // 2)
        levels.append(levels[-1].resize((w, h), Image.Resampling.BOX))
    return levels


def encode_level(img: Image.Image, fmt: TF) -> bytes:
    flipped = img.transpose(Image.Transpose.FLIP_TOP_BOTTOM)  # Unity stores rows bottom-up
    if fmt == TF.RGBA32:
        return flipped.convert("RGBA").tobytes()
    if fmt == TF.R8:
        return np.asarray(flipped.convert("RGBA"))[:, :, 0].tobytes()
    rgba = flipped.convert("RGBA")
    astc = ae.ASTCImage(ae.ASTCType.U8, rgba.width, rgba.height, 1, rgba.tobytes())
    return astc_context(BLOCK[fmt]).compress(astc, ae.ASTCSwizzle.from_str("RGBA"))


def psnr(a: Image.Image, b: Image.Image, single_channel: bool) -> float:
    x = np.asarray(a.convert("RGBA"), dtype=np.float32)
    y = np.asarray(b.convert("RGBA"), dtype=np.float32)
    if single_channel:
        x, y = x[:, :, 0], y[:, :, 0]
    mse = float(((x - y) ** 2).mean())
    return 99.0 if mse == 0 else 10 * np.log10(255.0 ** 2 / mse)


def target_format(rel_path: str, src: TF, width: int, height: int) -> TF:
    dst = TARGET[src]
    if dst == ASTC_4x4 and any(part.startswith(PREFER_6x6_BUNDLES) for part in Path(rel_path).parts) \
            and max(width, height) >= PREFER_6x6_MIN_SIDE:
        return ASTC_6x6
    return dst


def convert_texture(tex, dst: TF | None = None) -> Converted:
    src = TF(tex.m_TextureFormat)
    dst = dst or TARGET[src]
    top = tex.image.convert("RGBA")  # decoded top-down, BC/crunch handled by UnityPy
    mips = max(1, int(tex.m_MipCount or 1))
    data = b"".join(encode_level(level, dst) for level in mip_chain(top, mips))

    back = conv.parse_image_data(data, top.width, top.height, dst, UNITY_VERSION, 0)
    quality = psnr(top, back, single_channel=(dst == TF.R8))
    if quality < MIN_PSNR:
        if src in UNCOMPRESSED:
            # e.g. noise/glitch patterns: block compression wrecks them; keep the original.
            return Converted(tex.m_Name, src.name, src.name, top.size, mips, round(quality, 1), kept=True)
        dst = TF.RGBA32  # a BC source must change format; fall back to lossless
        data = b"".join(encode_level(level, dst) for level in mip_chain(top, mips))
        quality = 99.0

    tex.m_TextureFormat = int(dst)
    tex.image_data = data
    tex.m_CompleteImageSize = len(data)
    if tex.m_StreamData is not None:
        tex.m_StreamData.path = ""
        tex.m_StreamData.offset = 0
        tex.m_StreamData.size = 0
    tex.save()
    return Converted(tex.m_Name, src.name, dst.name, top.size, mips, round(quality, 1))


def restream(env, textures: list, origin: dict[int, tuple[str, int, int]], converted: set[int]) -> int | None:
    """Rebuild the bundle's .resS so it holds exactly the textures' current data.

    convert_texture() writes the new data inline; left alone, the original BC/RGBA data stays
    in the .resS as dead weight (~0.8 GB over all bundles). Moves every streamed or converted
    Texture2D's data into a freshly packed .resS and points m_StreamData at it.
    `origin` holds each texture's m_StreamData (path, offset, size) from before conversion.
    Returns the size change of the .resS in bytes, or None when the bundle is left as is.
    """
    nodes = env.file.files
    ress = [name for name in nodes if name.endswith(".resS")]
    if len(ress) != 1 or any(o.type.name in OTHER_RESS_USERS for o in env.objects):
        return None
    paths = {path for path, _, _ in origin.values() if path}
    if len(paths) != 1:
        return None  # nothing streamed, or data spread over several resource files
    stream_path = paths.pop()
    old = nodes[ress[0]]
    old_bytes = bytes(old.bytes)
    moves = []
    for tex in textures:
        path, offset, size = origin.get(id(tex), ("", 0, 0))
        if id(tex) in converted:
            moves.append((tex, bytes(tex.image_data)))
        elif path:
            moves.append((tex, old_bytes[offset:offset + size]))
        # textures that were inline to begin with and were not converted stay inline
    out = bytearray()
    for tex, data in moves:
        out += b"\0" * (-len(out) % 16)
        offset = len(out)
        out += data
        tex.image_data = b""
        tex.m_StreamData.path, tex.m_StreamData.offset, tex.m_StreamData.size = stream_path, offset, len(data)
        tex.save()
    writer = EndianBinaryWriter(bytes(out))
    writer.flags, writer.endian = old.flags, getattr(old, "endian", ">")
    nodes[ress[0]] = writer
    return len(out) - len(old_bytes)


def process_file(path: Path, rel_path: str = "") -> list[Converted]:
    env = UnityPy.load(str(path))
    done: list[Converted] = []
    textures = []
    converted: set[int] = set()
    origin: dict[int, tuple[str, int, int]] = {}
    for obj in env.objects:
        if obj.type.name != "Texture2D":
            continue
        tex = obj.read()
        textures.append(tex)
        sd = tex.m_StreamData
        if sd is not None and sd.path:
            origin[id(tex)] = (sd.path, sd.offset, sd.size)
        try:
            fmt = TF(tex.m_TextureFormat)
        except ValueError:
            continue
        if fmt not in TARGET:
            continue
        if fmt in UNCOMPRESSED and (tex.m_IsReadable or min(tex.m_Width, tex.m_Height) < MIN_UNCOMPRESSED_SIDE):
            continue
        c = convert_texture(tex, target_format(rel_path or path.as_posix(), fmt, tex.m_Width, tex.m_Height))
        if not c.kept:
            converted.add(id(tex))
        done.append(c)
    if any(not c.kept for c in done):
        if path.suffix == ".bundle":
            delta = restream(env, textures, origin, converted)
            if delta is not None:
                log.info("%s: rebuilt .resS (%+.1f MB)", path.name, delta / 1e6)
        packed = env.file.save(packer="lz4") if path.suffix == ".bundle" else env.file.save()
        tmp = path.with_name(path.name + ".tmp")
        tmp.write_bytes(packed)
        tmp.replace(path)
    return done


def data_files(data_dir: Path) -> list[Path]:
    files = sorted((data_dir / "Raw" / "aa").rglob("*.bundle"))
    files += sorted(p for p in data_dir.glob("*.assets"))
    return [f for f in files if not f.name.startswith("general-voice-")]  # audio-only bundles


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("data_dir", type=Path, help="staged Data/ directory (modified in place)")
    ap.add_argument("--report", type=Path, help="write a JSON report of every converted texture")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

    report: dict[str, list[dict]] = {}
    t0 = time.time()
    for path in data_files(args.data_dir):
        t = time.time()
        done = process_file(path, path.relative_to(args.data_dir).as_posix())
        if done:
            worst = min(c.psnr for c in done)
            log.info("%s: %d textures, min PSNR %.1f dB, %.1fs", path.name, len(done), worst, time.time() - t)
            report[str(path.relative_to(args.data_dir))] = [c.__dict__ for c in done]
    converted = [c for cs in report.values() for c in cs]
    bad = [c for c in converted if c["psnr"] < MIN_PSNR and not c["kept"]]
    for c in converted:
        if c["kept"]:
            log.info("kept original (ASTC PSNR %.1f dB): %s %s", c["psnr"], c["name"], c["src"])
    log.info("converted %d textures in %d files, %.0fs; below %.0f dB: %d",
             len(converted), len(report), time.time() - t0, MIN_PSNR, len(bad))
    for c in bad:
        log.warning("low PSNR: %s", c)
    if args.report:
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
