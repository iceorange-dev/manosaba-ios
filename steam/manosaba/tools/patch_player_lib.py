#!/usr/bin/env python3
"""Patch the Unity 6000.0.48f1 iOS player static lib so it accepts macOS-built data.

SerializedFile::ReadMetadata rejects any file whose target platform != 9 (iOS):

    cmp  w10, #0x9
    b.ne <"created for another build target" error>

We NOP the b.ne in place (same size, so the archive's symbol table stays valid).

The iOS player also calls managed callbacks that only exist in the mobile variant of
UnityEngine.AudioModule (AudioSettings.Mobile); the macOS game code lacks them and the
engine would invoke a null method. Those proxies are stubbed to return 0 (false / void).

Idempotent: re-running on an already patched lib is a no-op.
"""
from __future__ import annotations

import argparse
import logging
import struct
import sys
from collections.abc import Iterator

log = logging.getLogger("patch_player_lib")

NOP = 0xD503201F
RET = 0xD65F03C0
MOV_W0_0 = 0x52800000
RETURN_ZERO = (MOV_W0_0, RET)

# (member, function-relative description, text offset, expected prev insn, expected insn)
PATCHES = [
    ("Runtime_Serialize_0_4ia5l.o", "SerializedFile::ReadMetadata platform check",
     0xB4D8, 0x7100255F, 0x540049C1),
]

# (member, symbol, expected first two insns of the original function)
STUBS = [
    ("artifacts_iOS_codegenerator_6_4ia5l.o",
     "__ZN9Scripting11UnityEngine13AudioSettings11MobileProxy24InvokeOnMuteStateChangedEbP21ScriptingExceptionPtr",
     (0xD105C3FF, 0xA9146FFC)),
    ("artifacts_iOS_codegenerator_6_4ia5l.o",
     "__ZN9Scripting11UnityEngine13AudioSettings11MobileProxy36InvokeIsStopAudioOutputOnMuteEnabledEP21ScriptingExceptionPtr",
     (0xD10583FF, 0xA9144FF4)),
]


def iter_ar_members(buf: bytes) -> Iterator[tuple[str, int, int]]:
    if buf[:8] != b"!<arch>\n":
        raise ValueError("not an ar archive")
    pos = 8
    while pos + 60 <= len(buf):
        hdr = buf[pos:pos + 60]
        name = hdr[:16].decode().rstrip()
        size = int(hdr[48:58].decode().strip())
        data = pos + 60
        if name.startswith("#1/"):
            nlen = int(name[3:])
            name = buf[data:data + nlen].rstrip(b"\0").decode()
            yield name, data + nlen, size - nlen
        else:
            yield name.rstrip("/"), data, size
        pos = data + size + (size & 1)


def text_section(buf: bytes, base: int) -> tuple[int, int, int]:
    magic, _, _, _, ncmds = struct.unpack_from("<IiiII", buf, base)
    if magic != 0xFEEDFACF:
        raise ValueError("member is not a 64-bit Mach-O object")
    off = base + 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", buf, off)
        if cmd == 0x19:  # LC_SEGMENT_64
            nsects = struct.unpack_from("<I", buf, off + 64)[0]
            s = off + 72
            for _ in range(nsects):
                sect = buf[s:s + 16].rstrip(b"\0").decode()
                addr, size, foff = struct.unpack_from("<QQI", buf, s + 32)
                if sect == "__text":
                    return addr, size, base + foff
                s += 80
        off += cmdsize
    raise ValueError("__text not found")


def symbol_address(buf: bytes, base: int, name: str) -> int:
    """n_value of a defined symbol in the Mach-O object at base (LC_SYMTAB)."""
    _, _, _, _, ncmds = struct.unpack_from("<IiiII", buf, base)
    off = base + 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", buf, off)
        if cmd == 0x2:  # LC_SYMTAB
            symoff, nsyms, stroff, _ = struct.unpack_from("<IIII", buf, off + 8)
            want = name.encode()
            for i in range(nsyms):
                strx, ntype, _, _, value = struct.unpack_from("<IBBHQ", buf, base + symoff + 16 * i)
                s = base + stroff + strx
                if buf[s:s + len(want) + 1] == want + b"\0" and ntype & 0x0E == 0x0E:  # N_SECT
                    return value
            break
        off += cmdsize
    raise ValueError(f"symbol {name} not found")


def apply_stubs(buf: bytearray, members: dict[str, tuple[int, int]], dry_run: bool) -> int:
    changed = 0
    for member, sym, orig in STUBS:
        if member not in members:
            raise ValueError(f"member {member} not found")
        moff, _ = members[member]
        addr, _, foff = text_section(buf, moff)
        at = foff + (symbol_address(buf, moff, sym) - addr)
        cur = struct.unpack_from("<II", buf, at)
        short = sym[-60:]
        if cur == RETURN_ZERO:
            log.info("stub %s: already patched", short)
            continue
        if cur != orig:
            raise ValueError(f"stub {short}: unexpected prologue {cur[0]:08x} {cur[1]:08x}")
        log.info("stub %s -> return 0 at file offset 0x%x", short, at)
        if not dry_run:
            struct.pack_into("<II", buf, at, *RETURN_ZERO)
            changed += 1
    return changed


def apply_patches(buf: bytearray, dry_run: bool = False) -> int:
    """Patch buf in place. Returns number of sites changed; raises ValueError on mismatch."""
    members = {name: (off, size) for name, off, size in iter_ar_members(buf)}
    changed = 0
    for member, what, toff, prev_want, insn_want in PATCHES:
        if member not in members:
            raise ValueError(f"member {member} not found")
        moff, _ = members[member]
        addr, _, foff = text_section(buf, moff)
        at = foff + (toff - addr)
        prev, insn = struct.unpack_from("<II", buf, at - 4)
        if prev != prev_want:
            raise ValueError(f"{what}: unexpected insn before patch site: {prev:08x}")
        if insn == NOP:
            log.info("%s: already patched", what)
            continue
        if insn != insn_want:
            raise ValueError(f"{what}: unexpected insn at patch site: {insn:08x}")
        log.info("%s: %08x -> NOP at file offset 0x%x", what, insn, at)
        if not dry_run:
            struct.pack_into("<I", buf, at, NOP)
            changed += 1
    if STUBS and all(m in members for m, _, _ in STUBS):
        changed += apply_stubs(buf, members, dry_run)
    elif STUBS:
        log.warning("stub members absent; skipping %d stub(s)", len(STUBS))
    return changed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("lib", help="path to libiPhone-lib.a (patched in place)")
    ap.add_argument("--check", action="store_true", help="report state only")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")

    with open(args.lib, "rb") as f:
        buf = bytearray(f.read())
    try:
        changed = apply_patches(buf, dry_run=args.check)
    except ValueError as e:
        log.error("%s", e)
        return 3
    if changed:
        with open(args.lib, "wb") as f:
            f.write(buf)
        log.info("wrote %s (%d patch(es))", args.lib, changed)
    return 0


if __name__ == "__main__":
    sys.exit(main())
