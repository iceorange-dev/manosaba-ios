#!/usr/bin/env python3
"""Replace Unity's default app icon in the generated Xcode project with the game's own.

Takes the largest image of the Mac app's icns (Info.plist `CFBundleIconFile`) and rewrites
`<xcode-dir>/Unity-iPhone/Images.xcassets/AppIcon.appiconset` with every size iOS 15+ needs
(iPhone, iPad, iPad Pro, Settings/Spotlight/Notification, plus the 1024 App Store icon).
Every size is listed explicitly: with a deployment target of iOS 15, actool compiles a lone
universal 1024 entry into an Assets.car that holds just that one image.

iOS wants a full-bleed, opaque square and applies its own corner mask, so the macOS icon
template is undone first: the transparent margin and drop shadow are cropped away and the
four rounded-corner slivers are filled with the neighbouring artwork colours. An icon that
is not such a tile (a glyph on a transparent canvas) is flattened onto `--background`.
Alpha is always dropped, since iOS renders it black.

Rewrites only files whose bytes change, so re-running leaves an up-to-date catalog (and
Xcode's incremental build) untouched.
"""
from __future__ import annotations

import argparse
import io
import json
import logging
import plistlib
import sys
from pathlib import Path

import numpy as np
from PIL import Image

log = logging.getLogger("make_app_icon")

MASTER = 1024
MIN_SOURCE = 512  # below this an upscale to 1024 would visibly blur
OPAQUE = 250  # alpha counted as artwork, not anti-aliasing or shadow
TILE_FILL = 0.85  # a squircle covers ~96% of its bounding box, a glyph far less
TILE_SQUARE = 0.98  # min(w, h) / max(w, h) of the artwork bounding box

# (idiom, point size, scale) for AppIcon.appiconset on iOS 15+.
SLOTS = [
    ("iphone", 20, 2), ("iphone", 20, 3),  # notification
    ("iphone", 29, 2), ("iphone", 29, 3),  # settings
    ("iphone", 40, 2), ("iphone", 40, 3),  # spotlight
    ("iphone", 60, 2), ("iphone", 60, 3),  # home screen
    ("ipad", 20, 1), ("ipad", 20, 2),
    ("ipad", 29, 1), ("ipad", 29, 2),
    ("ipad", 40, 1), ("ipad", 40, 2),
    ("ipad", 76, 1), ("ipad", 76, 2),
    ("ipad", 83.5, 2),  # iPad Pro
    ("ios-marketing", 1024, 1),  # App Store
]


def icns_images(data: bytes) -> list[Image.Image]:
    """Every PNG / JPEG 2000 representation in an icns (the legacy RLE ones top out at 128 px)."""
    if data[:4] != b"icns":
        raise ValueError("not an icns file")
    images, off = [], 8
    while off + 8 <= len(data):
        tag, size = data[off:off + 4], int.from_bytes(data[off + 4:off + 8], "big")
        if size < 8:
            raise ValueError(f"corrupt icns chunk {tag!r}")
        payload = data[off + 8:off + size]
        if payload[:4] == b"\x89PNG" or payload[:12] == b"\x00\x00\x00\x0cjP  \r\n\x87\n":
            im = Image.open(io.BytesIO(payload))
            im.load()
            log.info("icns %s: %dx%d %s", tag.decode("latin-1"), *im.size, im.mode)
            images.append(im)
        off += size
    return images


def icon_path(app: Path) -> Path:
    """The icns named by the app's Info.plist `CFBundleIconFile` (extension optional)."""
    name = "PlayerIcon.icns"
    info = app / "Contents" / "Info.plist"
    if info.exists():
        name = plistlib.loads(info.read_bytes()).get("CFBundleIconFile", name)
    if not name.endswith(".icns"):
        name += ".icns"
    path = app / "Contents" / "Resources" / name
    if not path.exists():
        raise ValueError(f"{path} does not exist")
    return path


def box_sum(a: np.ndarray, r: int) -> np.ndarray:
    """Sum over a (2r+1) x (2r+1) window, zero outside the array."""
    k = 2 * r + 1
    for axis in (0, 1):
        pad = [(0, 0)] * a.ndim
        pad[axis] = (r + 1, r)
        cs = np.cumsum(np.pad(a, pad), axis=axis)
        hi = [slice(None)] * a.ndim
        lo = [slice(None)] * a.ndim
        hi[axis], lo[axis] = slice(k, None), slice(0, -k)
        a = cs[tuple(hi)] - cs[tuple(lo)]
    return a


def bleed(rgb: np.ndarray, known: np.ndarray) -> np.ndarray:
    """Fill the unknown pixels with the colours around them, widening the window until done."""
    out = rgb.astype(np.float64)
    have = known.copy()
    r = 2
    while not have.all():
        w = have.astype(np.float64)[..., None]
        weight = box_sum(w, r)
        near = ~have & (weight[..., 0] > 0.5)
        if near.any():
            out[near] = (box_sum(out * w, r)[near] / weight[near])
            have |= near
        r *= 2
    return out


def flatten(im: Image.Image, background: tuple[int, int, int]) -> Image.Image:
    """An opaque RGB square from an icon that may carry alpha."""
    rgba = np.asarray(im.convert("RGBA"))
    alpha = rgba[..., 3]
    if alpha.min() == 255:
        log.info("source is opaque")
        return im.convert("RGB")
    known = alpha >= OPAQUE
    if not known.any():
        raise ValueError("icon is fully transparent")
    ys, xs = np.nonzero(known)
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    w, h = x1 - x0, y1 - y0
    fill = known.sum() / (w * h)
    log.info("alpha %d..255, artwork box %dx%d at (%d,%d) covers %.1f%% of it", alpha.min(), w, h, x0, y0, fill * 100)
    if fill >= TILE_FILL and min(w, h) / max(w, h) >= TILE_SQUARE:
        side = min(w, h)
        x0, y0 = x0 + (w - side) // 2, y0 + (h - side) // 2
        sl = (slice(y0, y0 + side), slice(x0, x0 + side))
        log.info("icon is a tile: cropping the %dpx template margin, filling %d corner pixels from the artwork",
                 x0, int((~known[sl]).sum()))
        return Image.fromarray(bleed(rgba[sl][..., :3], known[sl]).round().astype(np.uint8), "RGB")
    log.info("icon is a glyph: flattening onto #%02x%02x%02x", *background)
    canvas = Image.new("RGBA", im.size, (*background, 255))
    return Image.alpha_composite(canvas, im.convert("RGBA")).convert("RGB")


def make_master(icns: bytes, background: tuple[int, int, int]) -> Image.Image:
    images = icns_images(icns)
    if not images:
        raise ValueError("icns has no PNG / JPEG 2000 representation")
    src = max(images, key=lambda i: i.width * i.height)
    log.info("using the %dx%d representation (%s)", *src.size, src.mode)
    if min(src.size) < MIN_SOURCE:
        raise ValueError(f"largest representation is only {src.size}, need at least {MIN_SOURCE}px")
    art = flatten(src, background)
    if art.size != (MASTER, MASTER):
        log.info("resizing %dx%d -> %d", *art.size, MASTER)
        art = art.resize((MASTER, MASTER), Image.Resampling.LANCZOS)
    return art


def png_bytes(master: Image.Image, px: int) -> bytes:
    im = master if px == MASTER else master.resize((px, px), Image.Resampling.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, "PNG")
    return buf.getvalue()


def write_catalog(master: Image.Image, iconset: Path) -> int:
    """Write the PNGs and Contents.json; returns how many files changed."""
    iconset.mkdir(parents=True, exist_ok=True)
    files: dict[str, bytes] = {}
    entries = []
    for idiom, pt, scale in SLOTS:
        label = {"iphone": "iPhone", "ipad": "iPad", "ios-marketing": "Marketing"}[idiom]
        name = f"Icon-{label}-{pt:g}@{scale}x.png"
        files[name] = png_bytes(master, round(pt * scale))
        entries.append({"filename": name, "idiom": idiom, "scale": f"{scale}x", "size": f"{pt:g}x{pt:g}"})
    contents = {"images": entries, "info": {"author": "xcode", "version": 1}}
    files["Contents.json"] = (json.dumps(contents, indent=2) + "\n").encode()

    changed = 0
    for name, data in files.items():
        path = iconset / name
        if path.exists() and path.read_bytes() == data:
            continue
        path.write_bytes(data)
        changed += 1
        log.info("wrote %s", name)
    for stale in sorted(iconset.glob("*.png")):  # Unity's defaults and earlier layouts
        if stale.name not in files:
            stale.unlink()
            changed += 1
            log.info("removed stale %s", stale.name)
    return changed


def parse_color(text: str) -> tuple[int, int, int]:
    h = text.lstrip("#")
    if len(h) != 6:
        raise argparse.ArgumentTypeError("expected #RRGGBB")
    try:
        return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))  # type: ignore[return-value]
    except ValueError:
        raise argparse.ArgumentTypeError("expected #RRGGBB") from None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("app", type=Path, help="the Mac game bundle (manosaba.app)")
    ap.add_argument("xcode_dir", type=Path, help="Unity's generated Xcode project (build/xcode)")
    ap.add_argument("--background", type=parse_color, default=(255, 255, 255), metavar="#RRGGBB",
                    help="fill for icons that are not full-bleed tiles (default #FFFFFF)")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    catalog = args.xcode_dir / "Unity-iPhone" / "Images.xcassets"
    if not catalog.is_dir():
        log.error("%s does not exist", catalog)
        return 2
    try:
        path = icon_path(args.app)
        log.info("reading %s", path)
        master = make_master(path.read_bytes(), args.background)
    except ValueError as e:
        log.error("%s", e)
        return 3
    changed = write_catalog(master, catalog / "AppIcon.appiconset")
    log.info("%s", f"{changed} file(s) updated" if changed else "already up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
