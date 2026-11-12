#!/usr/bin/env python3
"""Apply the transplant patches to the iOS *simulator* player (libiPhone-lib.dylib).

Same two fixes as patch_player_lib.py, located by symbol + instruction pattern because
offsets differ from the device static lib:
  * SerializedFile::ReadMetadata: NOP the `cmp wN, #9 ; b.ne` target-platform rejection.
  * AudioSettings.Mobile proxies (mobile-only managed callbacks): return 0.
  * Debug.isDebugBuild -> false: Unity ships the simulator player only as a Development
    build, and the game switches to its developer flow (debug overlay, no normal boot)
    when isDebugBuild is true.
Re-sign the dylib (ad-hoc is fine for the simulator) after patching. Idempotent.
"""
from __future__ import annotations

import argparse
import logging
import struct
import subprocess
import sys

log = logging.getLogger("patch_player_dylib")

NOP, RET, MOV_W0_0 = 0xD503201F, 0xD65F03C0, 0x52800000
READ_METADATA = "__ZN14SerializedFile12ReadMetadataE27SerializedFileFormatVersionN3VFS8FileSizeEPKhmS2_"
STUB_SYMBOLS = [
    "__ZN9Scripting11UnityEngine13AudioSettings11MobileProxy24InvokeOnMuteStateChangedEbP21ScriptingExceptionPtr",
    "__ZN9Scripting11UnityEngine13AudioSettings11MobileProxy36InvokeIsStopAudioOutputOnMuteEnabledEP21ScriptingExceptionPtr",
    "__Z33Debug_Get_Custom_PropIsDebugBuildv",
]
SCAN_BYTES = 0x1800


def load_commands(buf: bytes):
    magic, _, _, _, ncmds = struct.unpack_from("<IiiII", buf, 0)
    if magic != 0xFEEDFACF:
        raise ValueError("not a thin 64-bit Mach-O")
    off = 32
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", buf, off)
        yield cmd, off
        off += size


def segments(buf: bytes) -> list[tuple[int, int, int, int]]:
    """(vmaddr, vmsize, fileoff, filesize) per LC_SEGMENT_64."""
    out = []
    for cmd, off in load_commands(buf):
        if cmd == 0x19:
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", buf, off + 24)
            out.append((vmaddr, vmsize, fileoff, filesize))
    return out


def vm_to_file(buf: bytes, addr: int) -> int:
    for vmaddr, vmsize, fileoff, _ in segments(buf):
        if vmaddr <= addr < vmaddr + vmsize:
            return fileoff + (addr - vmaddr)
    raise ValueError(f"address 0x{addr:x} not mapped")


def symbols(buf: bytes, names: set[str]) -> dict[str, int]:
    found: dict[str, int] = {}
    for cmd, off in load_commands(buf):
        if cmd != 0x2:
            continue
        symoff, nsyms, stroff, _ = struct.unpack_from("<IIII", buf, off + 8)
        for i in range(nsyms):
            strx, ntype, _, _, value = struct.unpack_from("<IBBHQ", buf, symoff + 16 * i)
            if ntype & 0x0E != 0x0E or ntype & 0xE0:  # defined in a section, not a stab
                continue
            end = buf.index(b"\0", stroff + strx)
            name = buf[stroff + strx:end].decode(errors="replace")
            if name in names:
                found[name] = value
    return found


def is_cmp_imm9(insn: int) -> bool:
    # SUBS WZR, Wn, #9  ==  0x7100241F | (n << 5)
    return insn & 0xFFFFFC1F == 0x7100241F


def is_bne(insn: int) -> bool:
    return insn & 0xFF00001F == 0x54000001


def patch(buf: bytearray) -> int:
    syms = symbols(buf, {READ_METADATA, *STUB_SYMBOLS})
    if READ_METADATA not in syms:
        raise ValueError("ReadMetadata symbol not found")
    changed = 0

    start = vm_to_file(buf, syms[READ_METADATA])
    sites = []
    for at in range(start, start + SCAN_BYTES, 4):
        a, b = struct.unpack_from("<II", buf, at)
        if is_cmp_imm9(a) and (is_bne(b) or b == NOP):
            sites.append((at + 4, b))
    if len(sites) != 1:
        raise ValueError(f"expected one platform check in ReadMetadata, found {len(sites)}")
    at, insn = sites[0]
    if insn == NOP:
        log.info("ReadMetadata platform check: already patched")
    else:
        log.info("ReadMetadata platform check: %08x -> NOP at 0x%x", insn, at)
        struct.pack_into("<I", buf, at, NOP)
        changed += 1

    for sym in STUB_SYMBOLS:
        short = sym[-60:]
        if sym not in syms:
            log.warning("stub %s: symbol absent (inlined?), skipped", short)
            continue
        at = vm_to_file(buf, syms[sym])
        if struct.unpack_from("<II", buf, at) == (MOV_W0_0, RET):
            log.info("stub %s: already patched", short)
            continue
        log.info("stub %s -> return 0 at 0x%x", short, at)
        struct.pack_into("<II", buf, at, MOV_W0_0, RET)
        changed += 1
    return changed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dylib")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    with open(args.dylib, "rb") as f:
        buf = bytearray(f.read())
    try:
        changed = patch(buf)
    except ValueError as e:
        log.error("%s", e)
        return 3
    if changed:
        with open(args.dylib, "wb") as f:
            f.write(buf)
        subprocess.run(["codesign", "-f", "-s", "-", args.dylib], check=True, capture_output=True)
        log.info("wrote and ad-hoc re-signed %s (%d patch(es))", args.dylib, changed)
    return 0


if __name__ == "__main__":
    sys.exit(main())
