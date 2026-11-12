#!/usr/bin/env python3
"""Shrink the game's audio: re-encode the FMOD Vorbis AudioClips inside the Addressables bundles.

The Steam build ships every clip as FSB5/Vorbis at FMOD quality 100 (~313 kbps for voice, ~413 kbps for
music), and the voice clips are stereo files with identical channels. Per clip this tool

  * decodes the stream with the real FMOD library (the one UnityPy ships),
  * keeps stereo only when the two channels differ, otherwise downmixes with the clipping-safe
    ``pan=mono|c0=0.5*c0+0.5*c1`` (voice and SFX; music always stays stereo) and adds +3.01 dB, because
    Unity pans every mono AudioSource through FMOD's constant-power law (-3.01 dB per speaker at centre)
    while a stereo clip passes unchanged; without it each downmixed clip would play 3 dB quieter,
  * encodes it with ffmpeg/libvorbis at a Unity-style quality (voice 45, music and SFX 63),
  * writes a new FSB5 container (header, VORBISDATA chunk with the setup-header CRC, seek table), and
  * checks it by decoding it back with FMOD (duration within 1 ms, channels, rate, waveform correlation)
    and with ffmpeg (peak <= 0 dBFS; clips that overshoot are re-encoded with a little headroom).

Only setup headers that FMOD knows are emitted: the CRC of the encoder's setup packet (a plain zlib crc32)
must be in FMOD's table of (quality, channels, rate) triples, otherwise the clip is left untouched.
A clip that still fails verification keeps its original data and is named in the log and report; the rest of
its bundle is converted and saved (LZ4, via tmp + rename, like transcode_textures.py). A bundle is refused
(left untouched, exit code 1) only for structural problems, e.g. the saved file does not read back correctly.
Clips already at or below the target quality are skipped, so re-running is a no-op.
`Song_004_001` and every clip longer than 120 s set to DecompressOnLoad become Streaming.

Usage, from the repo root:  .venv/bin/python -I tools/transcode_audio.py <data_dir> [--voice-quality 45]
    [--music-quality 63] [--mono-gain-db 3.01] [--only GLOB] [--report out.json] [--jobs N]
``<data_dir>/Raw/aa/StandaloneOSX/**/*.bundle`` is rewritten in place.

Requirements (repo venv, exact versions; ``uv pip install --python .venv/bin/python <pkg>==<ver>``):
    UnityPy==1.25.4  lz4==4.4.5  numpy==2.5.4  fmod-toolkit==0.1.3  pyfmodex==0.7.2  fsb5==1.0
and an ``ffmpeg`` (tested 9.0.2) on PATH built with libvorbis. ``fsb5`` only supplies FMOD's CRC table
(pure data); its native libvorbis loader is never used.
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import ctypes
import dataclasses
import fnmatch
import hashlib
import json
import logging
import math
import os
import shutil
import struct
import subprocess
import sys
import time
import zlib
from collections import Counter
from collections.abc import Iterable, Iterator
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np
import UnityPy
from fsb5.vorbis_headers import lookup as FMOD_VORBIS_HEADERS  # crc32 -> (quality, channels, rate)
from UnityPy.helpers.CompressionHelper import DECOMPRESSION_MAP
from UnityPy.streams import EndianBinaryReader

log = logging.getLogger("transcode_audio")

FFMPEG = os.environ.get("FFMPEG", "ffmpeg")

LOAD_DECOMPRESS_ON_LOAD, LOAD_COMPRESSED_IN_MEMORY, LOAD_STREAMING = 0, 1, 2
COMPRESSION_VORBIS = 1
FSB5_MODE_VORBIS = 15
FREQUENCY_INDEX = {8000: 1, 11000: 2, 11025: 3, 16000: 4, 22050: 5, 24000: 6, 32000: 7, 44100: 8, 48000: 9}
STREAM_CLIPS = frozenset({"Song_004_001"})  # DecompressOnLoad would keep 300 s of PCM resident
STREAM_ABOVE_SECONDS = 120.0

DEFAULT_VOICE_QUALITY = 45
DEFAULT_MUSIC_QUALITY = 63
SIDE_MID_MAX_DB = -40.0  # side/mid energy ratio under which L and R count as identical (game: -45..-74)
SIDE_PEAK_MAX_DB = -34.0  # and side peak relative to mid peak
MAX_DURATION_ERROR_S = 0.001
MIN_CORRELATION = 0.7  # waveform sanity check against the source (a perceptual codec stays well above)
MAX_LAG_SAMPLES = 1  # decoded audio must line up with the source sample for sample
MAX_LEVEL_ERROR_DB = 1.5  # decoded RMS vs source RMS plus the gain we applied
NEAR_SILENCE_RMS = 10 ** (-60 / 20)  # below this the source is dither noise: a codec does not reproduce it, so only
#                                      the "still silent" property is checked, not the waveform (placeholder lines)
PEAK_LIMIT = 1.0
HEADROOM_TARGET = 0.97  # peak the re-encode aims for after an overshoot
PAN_LAW_DB = 20 * math.log10(math.sqrt(2))  # FMOD centres a mono source at 1/sqrt(2) per speaker (checked with FMOD)
MAX_ENCODE_ATTEMPTS = 3


# --------------------------------------------------------------------------------------------------
# FSB5 container (version 1, one Vorbis subsong)
# --------------------------------------------------------------------------------------------------
class AudioError(Exception):
    """A clip cannot be handled; the message is the log/report reason."""


@dataclass(frozen=True)
class Fsb5Vorbis:
    channels: int
    rate: int
    samples: int
    crc: int
    packets: tuple[bytes, ...]
    seek: tuple[tuple[int, int], ...]  # (granule position, byte offset into the packet stream)
    tail: bytes  # header bytes 28..60 (flags, hash, filler); flags and filler are reused for the new clip


def parse_fsb5_vorbis(data: bytes | memoryview) -> Fsb5Vorbis:
    """Parse an FSB5 holding exactly one FMOD Vorbis subsong; anything else raises AudioError."""
    if len(data) < 68 or bytes(data[:4]) != b"FSB5":
        raise AudioError("not an FSB5 file")
    version, count, header_size, name_size, data_size, mode = struct.unpack_from("<6I", data, 4)
    if version != 1 or count != 1 or name_size != 0:
        raise AudioError(f"unsupported FSB5 layout (version {version}, {count} subsongs, names {name_size})")
    if mode != FSB5_MODE_VORBIS:
        raise AudioError(f"FSB5 codec {mode} is not Vorbis")
    raw = struct.unpack_from("<Q", data, 60)[0]
    channels, samples = ((raw >> 5) & 1) + 1, (raw >> 34) & 0x3FFFFFFF
    freq_index = (raw >> 1) & 0xF
    rate = {v: k for k, v in FREQUENCY_INDEX.items()}.get(freq_index)
    if rate is None:
        raise AudioError(f"unsupported FSB5 frequency index {freq_index}")
    crc, seek = None, ()
    pos, more = 68, raw & 1
    while more:
        chunk = struct.unpack_from("<I", data, pos)[0]
        more, size, kind = chunk & 1, (chunk >> 1) & 0xFFFFFF, chunk >> 25
        if kind == 11:  # VORBISDATA: crc32, seek table size, (granule, offset) pairs
            crc = struct.unpack_from("<I", data, pos + 4)[0]
            table = struct.unpack_from("<I", data, pos + 8)[0]
            pairs = struct.unpack_from(f"<{table // 4}I", data, pos + 12)
            seek = tuple(zip(pairs[0::2], pairs[1::2], strict=True))
        pos += 4 + size
    if crc is None:
        raise AudioError("FSB5 sample has no VORBISDATA chunk")
    start = 60 + header_size
    if start + data_size > len(data):
        raise AudioError("FSB5 data section is truncated")
    stream = bytes(data[start : start + data_size])
    packets, at = [], 0
    while at + 2 <= len(stream):
        size = struct.unpack_from("<H", stream, at)[0]
        if size == 0 or at + 2 + size > len(stream):
            break
        packets.append(stream[at + 2 : at + 2 + size])
        at += 2 + size
    return Fsb5Vorbis(channels, rate, samples, crc, tuple(packets), seek, bytes(data[28:60]))


def seek_pairs(granules: list[int], offsets: list[int], rate: int, samples: int) -> list[tuple[int, int]]:
    """FMOD's seek table: for each whole second inside the clip, the last packet that starts before it."""
    pairs, second = [], 1
    for i in range(len(granules) - 1):
        while granules[i] < second * rate <= granules[i + 1] and second * rate <= samples:
            pairs.append((granules[i], offsets[i]))
            second += 1
    return pairs


def build_fsb5_vorbis(packets: list[bytes], granules: list[int], samples: int, rate: int, channels: int, crc: int,
                      template: bytes | None = None) -> bytes:
    """Serialise one Vorbis subsong the way FMOD's fsbankcl does (byte-identical to the game's own files).

    Header bytes 28..60 are a flags qword (FMOD refuses the file unless it is 1), a 16-byte hash FMOD never
    checks and 8 filler bytes. `template` (a source clip's header bytes 28..60) donates flags and filler;
    the hash is always recomputed from the new stream.
    """
    offsets, at = [], 0
    for p in packets:
        offsets.append(at)
        at += 2 + len(p)
    pairs = seek_pairs(granules, offsets, rate, samples)
    table = struct.pack("<II", crc, 8 * len(pairs)) + b"".join(struct.pack("<II", g, o) for g, o in pairs)
    stream = b"".join(struct.pack("<H", len(p)) + p for p in packets)
    stream += b"\0" * (-len(stream) % 32)
    raw = 1 | (FREQUENCY_INDEX[rate] << 1) | ((channels - 1) << 5) | (samples << 34)
    sample_header = struct.pack("<QI", raw, (len(table) << 1) | (11 << 25)) + table
    sample_header += b"\0" * (-(60 + len(sample_header)) % 32)
    header = bytearray(b"FSB5" + bytes(56))
    struct.pack_into("<6I", header, 4, 1, 1, len(sample_header), 0, len(stream), FSB5_MODE_VORBIS)
    flags, filler = (template[:8], template[24:32]) if template else (struct.pack("<Q", 1), bytes(8))
    header[28:60] = flags + hashlib.blake2b(stream, digest_size=16).digest() + filler
    return bytes(header) + sample_header + stream


# --------------------------------------------------------------------------------------------------
# Ogg / Vorbis helpers (the encoder's output is parsed, never re-muxed)
# --------------------------------------------------------------------------------------------------
@dataclass(frozen=True)
class OggStream:
    packets: list[bytes]
    page_ends: list[tuple[int, int]]  # (index of last packet finished on the page, page granule)


def parse_ogg(data: bytes) -> OggStream:
    packets: list[bytes] = []
    ends: list[tuple[int, int]] = []
    current = bytearray()
    pos = 0
    while pos < len(data):
        if data[pos : pos + 4] != b"OggS":
            raise AudioError("broken Ogg stream")
        granule = struct.unpack_from("<q", data, pos + 6)[0]
        lacing = data[pos + 27 : pos + 27 + data[pos + 26]]
        body = pos + 27 + data[pos + 26]
        for size in lacing:
            current += data[body : body + size]
            body += size
            if size < 255:
                packets.append(bytes(current))
                current.clear()
        ends.append((len(packets) - 1, granule))
        pos = body
    return OggStream(packets, ends)


def vorbis_blocksizes(id_packet: bytes) -> tuple[int, int]:
    if id_packet[:7] != b"\x01vorbis":
        raise AudioError("missing Vorbis identification header")
    return 1 << (id_packet[28] & 15), 1 << (id_packet[28] >> 4)


def _bits(data: bytes, pos: int, count: int) -> int:
    value = 0
    for i in range(count):
        value |= ((data[(pos + i) >> 3] >> ((pos + i) & 7)) & 1) << i
    return value


def vorbis_mode_flags(setup_packet: bytes) -> list[int]:
    """Block flag of every mode, read from the tail of the setup header (modes sit right before the framing bit)."""
    if setup_packet[:7] != b"\x05vorbis":
        raise AudioError("missing Vorbis setup header")
    last = len(setup_packet) - 1
    while last >= 0 and setup_packet[last] == 0:
        last -= 1
    if last < 0:
        raise AudioError("empty Vorbis setup header")
    framing = last * 8 + setup_packet[last].bit_length() - 1
    for count in range(64, 0, -1):  # the largest consistent count wins; smaller ones can match by accident
        first = framing - 41 * count
        if first < 7 * 8 + 6 or _bits(setup_packet, first - 6, 6) != count - 1:
            continue
        modes = [(_bits(setup_packet, first + 41 * i, 1), _bits(setup_packet, first + 41 * i + 1, 32)) for i in range(count)]
        if all(window_and_transform == 0 for _, window_and_transform in modes):
            return [flag for flag, _ in modes]
    raise AudioError("cannot locate the Vorbis mode table")


def granule_positions(packets: list[bytes], mode_flags: list[int], blocksizes: tuple[int, int]) -> list[int]:
    """Granule position after each audio packet (libvorbis/Ogg rule: a packet yields (prev + cur) / 4 samples)."""
    mode_bits = max(1, (len(mode_flags) - 1).bit_length()) if len(mode_flags) > 1 else 0
    positions, granule, previous = [], 0, 0
    for p in packets:
        if p[0] & 1:
            raise AudioError("non-audio packet in the audio stream")
        mode = (p[0] >> 1) & ((1 << mode_bits) - 1)
        size = blocksizes[mode_flags[mode]]
        granule = (granule + (size + previous) // 4) if previous else 0
        previous = size
        positions.append(granule)
    return positions


# --------------------------------------------------------------------------------------------------
# ffmpeg (libvorbis encoder, float decoder) and FMOD (reference decoder)
# --------------------------------------------------------------------------------------------------
def vorbis_quality(quality: int) -> float:
    """Unity/FMOD quality 1..100 -> libvorbis quality (-0.1..1.0), the mapping FMOD's header table is keyed on."""
    return -0.1 + (quality - 1) / 99 * 1.1


def _run_ffmpeg(args: list[str], data: bytes) -> bytes:
    done = subprocess.run([FFMPEG, "-v", "error", "-nostdin", "-threads", "1", *args], input=data,
                          capture_output=True, check=False)
    if done.returncode != 0:
        raise AudioError(f"ffmpeg failed: {done.stderr.decode(errors='replace').strip()[:300]}")
    return done.stdout


def encode_vorbis(pcm: np.ndarray, rate: int, out_channels: int, quality: int, gain: float = 1.0) -> bytes:
    """pcm: float32 [frames, channels]; returns an Ogg Vorbis stream."""
    in_channels = pcm.shape[1]
    filters = []
    if in_channels == 2 and out_channels == 1:
        filters.append("pan=mono|c0=0.5*c0+0.5*c1")
    if gain != 1.0:
        filters.append(f"volume={gain:.6f}")
    cmd = ["-f", "f32le", "-ar", str(rate), "-ac", str(in_channels), "-i", "pipe:0"]
    if filters:
        cmd += ["-af", ",".join(filters)]
    cmd += ["-c:a", "libvorbis", "-q:a", f"{vorbis_quality(quality) * 10:.4f}", "-fflags", "+bitexact",
            "-flags:a", "+bitexact", "-f", "ogg", "pipe:1"]
    return _run_ffmpeg(cmd, np.ascontiguousarray(pcm, dtype="<f4").tobytes())


def decode_ogg_float(ogg: bytes, channels: int) -> np.ndarray:
    raw = _run_ffmpeg(["-i", "pipe:0", "-f", "f32le", "-"], ogg)
    return np.frombuffer(raw, dtype="<f4").reshape(-1, channels)


_FMOD: Any = None


def _fmod() -> Any:
    """One silent (NOSOUND) FMOD system per process."""
    global _FMOD
    if _FMOD is None:
        from fmod_toolkit.importer import import_pyfmodex

        pyfmodex = import_pyfmodex()
        system = pyfmodex.System()
        system.output = pyfmodex.enums.OUTPUTTYPE.NOSOUND
        system.init(8, pyfmodex.flags.INIT_FLAGS.NORMAL, None)
        _FMOD = (pyfmodex, system)
    return _FMOD


@dataclass(frozen=True)
class DecodedClip:
    pcm: np.ndarray  # int16 [frames, channels]
    rate: int


def fmod_decode(fsb: bytes) -> DecodedClip:
    """Decode an FSB5 file with FMOD, exactly as the game's audio engine would (rejects unknown CRCs)."""
    pyfmodex, system = _fmod()
    try:
        sound = system.create_sound(bytes(fsb), pyfmodex.flags.MODE.OPENMEMORY,
                                    exinfo=pyfmodex.structure_declarations.CREATESOUNDEXINFO(length=len(fsb)))
    except Exception as exc:  # pyfmodex raises FmodError subclasses
        raise AudioError(f"FMOD rejected the stream: {exc}") from exc
    try:
        if sound.num_subsounds != 1:
            raise AudioError(f"FMOD found {sound.num_subsounds} subsounds")
        sub = sound.get_subsound(0)
        try:
            fmt = sub.format
            if fmt.format != pyfmodex.enums.SOUND_FORMAT.PCM16:
                raise AudioError(f"FMOD decoded to {fmt.format}, expected PCM16")
            size = sub.get_length(pyfmodex.enums.TIMEUNIT.PCMBYTES)
            lock = sub.lock(0, size)
            data = b"".join(ctypes.string_at(ptr, length.value) for ptr, length in lock)
            sub.unlock(*lock)
            return DecodedClip(np.frombuffer(data, dtype="<i2").reshape(-1, fmt.channels), int(sub.default_frequency))
        finally:
            sub.release()
    finally:
        sound.release()


# --------------------------------------------------------------------------------------------------
# One clip
# --------------------------------------------------------------------------------------------------
KIND_VOICE, KIND_MUSIC, KIND_SFX = "voice", "music", "sfx"


def classify(bundle_name: str, clip_name: str) -> str:
    if bundle_name.startswith("general-voice-"):
        return KIND_VOICE
    if clip_name.startswith(("Bgm_", "Song_")) or "audio-bgm" in bundle_name or "audio-song" in bundle_name:
        return KIND_MUSIC
    return KIND_SFX


def side_mid_stats(pcm: np.ndarray) -> tuple[float, float]:
    """(side/mid energy dB, side/mid peak dB) of a stereo int16 clip; -inf when the channels are identical."""
    left, right = pcm[:, 0].astype(np.float64), pcm[:, 1].astype(np.float64)
    mid, side = (left + right) / 2, (left - right) / 2
    mid_energy, side_energy = float(mid @ mid), float(side @ side)
    if side_energy == 0:
        return -math.inf, -math.inf
    if mid_energy == 0:
        return math.inf, math.inf
    return (10 * math.log10(side_energy / mid_energy),
            20 * math.log10(float(np.abs(side).max()) / float(np.abs(mid).max())))


def channels_identical(pcm: np.ndarray) -> bool:
    energy_db, peak_db = side_mid_stats(pcm)
    return energy_db <= SIDE_MID_MAX_DB and peak_db <= SIDE_PEAK_MAX_DB


def correlation(a: np.ndarray, b: np.ndarray) -> float:
    """Normalised cross-correlation at zero lag over all samples (1.0 for identical signals)."""
    a, b = a.astype(np.float64).ravel(), b.astype(np.float64).ravel()
    n = min(a.size, b.size)
    a, b = a[:n], b[:n]
    norm = math.sqrt(float(a @ a) * float(b @ b))
    return 1.0 if norm == 0 else float(a @ b) / norm


def best_lag(decoded: np.ndarray, reference: np.ndarray, window: int = 1 << 18, search: int = 2048) -> int:
    """Lag (samples) at which `decoded` best matches `reference`, from the first `window` samples; 0 = aligned."""
    n = min(decoded.size, reference.size, window)
    if n < 2:
        return 0
    size = 1 << (2 * n - 1).bit_length()
    corr = np.fft.irfft(np.fft.rfft(decoded[:n], size) * np.conj(np.fft.rfft(reference[:n], size)), size)
    lags = np.concatenate([corr[-search:], corr[: search + 1]])
    return int(np.argmax(lags)) - search


@dataclass(frozen=True)
class ClipTask:
    key: tuple[str, int, int]  # (resource node, offset, size) in the source bundle
    name: str
    kind: str
    data: bytes
    length: float
    quality: int
    mono_gain_db: float = PAN_LAW_DB  # added when a stereo clip becomes mono


@dataclass
class ClipResult:
    key: tuple[str, int, int]
    name: str
    kind: str
    status: str  # converted | skipped | kept (failed verification: the original data stays)
    code: str = ""  # why it was skipped: already-converted | unknown-crc | unsupported
    reason: str = ""
    data: bytes | None = None
    channels: int = 0
    rate: int = 0
    samples: int = 0
    stats: dict[str, Any] = field(default_factory=dict)


def convert_clip(task: ClipTask) -> ClipResult:
    """Pure function of its input; runs in a worker process. Never raises."""
    result = ClipResult(task.key, task.name, task.kind, "skipped")
    try:
        return _convert_clip(task, result)
    except AudioError as exc:
        result.status, result.reason = "kept", str(exc)
    except Exception as exc:  # noqa: BLE001 - a worker must always report back
        result.status, result.reason = "kept", f"{type(exc).__name__}: {exc}"
    return result


def _skip(result: ClipResult, code: str, reason: str) -> ClipResult:
    result.status, result.code, result.reason = "skipped", code, reason
    return result


def _convert_clip(task: ClipTask, result: ClipResult) -> ClipResult:
    try:
        src = parse_fsb5_vorbis(task.data)
    except AudioError as exc:
        return _skip(result, "unsupported", f"not a single-subsong FMOD Vorbis FSB5 ({exc})")
    known = FMOD_VORBIS_HEADERS.get(src.crc)
    if known and known[0] <= task.quality:
        return _skip(result, "already-converted", f"already q{known[0]} (target q{task.quality})")

    decoded = fmod_decode(task.data)
    pcm = decoded.pcm
    if pcm.shape[1] != src.channels or decoded.rate != src.rate or pcm.shape[0] != src.samples:
        raise AudioError(f"FMOD decode disagrees with the FSB5 header ({pcm.shape[1]}ch {decoded.rate} Hz "
                         f"{pcm.shape[0]} frames vs {src.channels}ch {src.rate} Hz {src.samples} frames)")
    energy_db = peak_db = None
    out_channels = src.channels
    if src.channels == 2:
        energy_db, peak_db = side_mid_stats(pcm)
        if task.kind != KIND_MUSIC and energy_db <= SIDE_MID_MAX_DB and peak_db <= SIDE_PEAK_MAX_DB:
            out_channels = 1
    source = pcm.astype(np.float32) / 32768.0
    reference = source if out_channels == src.channels else source.mean(axis=1, keepdims=True)

    mono_gain = 10 ** (task.mono_gain_db / 20) if out_channels < src.channels else 1.0
    gain, attempt = mono_gain, 0
    while True:
        attempt += 1
        ogg = encode_vorbis(source, src.rate, out_channels, task.quality, gain)
        stream = parse_ogg(ogg)
        if len(stream.packets) < 4:
            raise AudioError("encoder produced no audio packets")
        id_packet, _, setup, audio = stream.packets[0], stream.packets[1], stream.packets[2], stream.packets[3:]
        crc = zlib.crc32(setup) & 0xFFFFFFFF
        if FMOD_VORBIS_HEADERS.get(crc) != (task.quality, out_channels, src.rate):
            return _skip(result, "unknown-crc", f"setup header crc 0x{crc:08x} for q{task.quality}/{out_channels}ch/"
                                                 f"{src.rate} Hz is not in FMOD's table ({FMOD_VORBIS_HEADERS.get(crc)})")
        blocksizes = vorbis_blocksizes(id_packet)
        granules = granule_positions(audio, vorbis_mode_flags(setup), blocksizes)
        for index, granule in stream.page_ends[:-1]:  # the last page is trimmed to the true length
            if index >= 3 and granule >= 0 and granules[index - 3] != granule:
                raise AudioError(f"granule mismatch at packet {index - 3}: {granules[index - 3]} vs {granule}")
        if stream.page_ends[-1][1] != src.samples or not src.samples <= granules[-1] < src.samples + 2 * max(blocksizes):
            raise AudioError(f"stream length mismatch: {granules[-1]} samples in packets, last page says "
                             f"{stream.page_ends[-1][1]}, clip has {src.samples}")
        fsb = build_fsb5_vorbis(audio, granules, src.samples, src.rate, out_channels, crc, src.tail)

        check = decode_ogg_float(ogg, out_channels)
        peak = float(np.abs(check).max()) if check.size else 0.0
        if peak > PEAK_LIMIT and attempt < MAX_ENCODE_ATTEMPTS:
            gain *= HEADROOM_TARGET / peak if attempt == 1 else 0.9
            continue
        break

    # Verification: what the game's FMOD makes of the file we are about to ship.
    back = fmod_decode(fsb)
    parsed = parse_fsb5_vorbis(fsb)
    if (parsed.channels, parsed.rate, parsed.samples, parsed.crc) != (out_channels, src.rate, src.samples, crc):
        raise AudioError("FSB5 header does not round-trip")
    if back.pcm.shape[1] != out_channels or back.rate != src.rate:
        raise AudioError(f"FMOD decodes {back.pcm.shape[1]}ch/{back.rate} Hz, expected {out_channels}ch/{src.rate} Hz")
    duration_error = abs(back.pcm.shape[0] / back.rate - task.length)
    if duration_error > MAX_DURATION_ERROR_S:
        raise AudioError(f"duration off by {duration_error * 1000:.2f} ms")
    if peak > PEAK_LIMIT:
        raise AudioError(f"peak {20 * math.log10(peak):+.2f} dBFS after {attempt} attempts")
    heard = back.pcm.astype(np.float32) / 32768.0
    ref_rms, heard_rms = float(np.sqrt(np.mean(reference**2))), float(np.sqrt(np.mean(heard**2)))
    corr = correlation(heard, reference)
    lag = best_lag(heard.mean(axis=1), reference.mean(axis=1))
    level_error = 0.0
    if ref_rms < NEAR_SILENCE_RMS:  # placeholder lines: all that matters is that the result is silent too
        if heard_rms > max(NEAR_SILENCE_RMS, ref_rms * gain * 4):
            raise AudioError(f"near-silent source became audible (rms {20 * math.log10(heard_rms):.1f} dBFS)")
    else:
        if corr < MIN_CORRELATION or abs(lag) > MAX_LAG_SAMPLES:
            raise AudioError(f"decoded audio differs from the source (correlation {corr:.3f}, lag {lag} samples)")
        level_error = 20 * math.log10(heard_rms / ref_rms) - 20 * math.log10(gain) if heard_rms > 0 else -99.0
        if abs(level_error) > MAX_LEVEL_ERROR_DB:
            raise AudioError(f"loudness is {level_error:+.2f} dB off the intended gain")

    result.status, result.data = "converted", fsb
    result.channels, result.rate, result.samples = out_channels, src.rate, src.samples
    seconds = max(src.samples / src.rate, 1e-9)
    result.stats = {
        "bytes_before": len(task.data), "bytes_after": len(fsb), "kbps_after": round(len(fsb) * 8 / seconds / 1000, 1),
        "channels_before": src.channels, "channels_after": out_channels,
        "side_mid_db": None if energy_db is None else (round(energy_db, 1) if math.isfinite(energy_db) else None),
        "peak_dbfs": round(20 * math.log10(peak), 2) if peak > 0 else None,
        "duration_error_ms": round(duration_error * 1000, 4), "correlation": round(corr, 4), "lag_samples": lag,
        "level_error_db": round(level_error, 2),
        "gain_db": round(20 * math.log10(gain), 2), "headroom_limited": gain < mono_gain * 0.9999, "attempts": attempt,
    }
    return result


def _worker_init() -> None:
    _fmod()


# --------------------------------------------------------------------------------------------------
# Bundles
# --------------------------------------------------------------------------------------------------
def bundle_node_names(path: Path) -> list[str]:
    """Names of the files inside a UnityFS bundle, from its directory block only (no payload decompression)."""
    with path.open("rb") as f:
        head = f.read(4096)
        if not head.startswith(b"UnityFS\0"):
            return []
        pos = 8
        version = struct.unpack_from(">I", head, pos)[0]
        pos += 4
        for _ in range(2):  # player + engine version strings
            pos = head.index(b"\0", pos) + 1
        _, compressed, uncompressed, flags = struct.unpack_from(">qIII", head, pos)
        pos += 20
        if version >= 7:
            pos = (pos + 15) & ~15
        if flags & 0x80:
            f.seek(-compressed, os.SEEK_END)
        else:
            f.seek(pos)
        raw = f.read(compressed)
    kind = flags & 0x3F
    info = raw if kind == 0 else DECOMPRESSION_MAP[kind](raw, uncompressed)
    (blocks,) = struct.unpack_from(">i", info, 16)
    at = 20 + 10 * blocks
    (nodes,) = struct.unpack_from(">i", info, at)
    at += 4
    names = []
    for _ in range(nodes):
        end = info.index(b"\0", at + 20)
        names.append(info[at + 20 : end].decode())
        at = end + 1
    return names


@dataclass
class ClipRef:
    obj: Any  # UnityPy ObjectReader
    tree: dict[str, Any]
    key: tuple[str, int, int] | None  # None: no external resource (inline data)
    kind: str


@dataclass
class BundleReport:
    path: str
    status: str = "unchanged"  # converted | unchanged | refused
    reason: str = ""
    bytes_before: int = 0
    bytes_after: int = 0
    clips: dict[str, int] = field(default_factory=dict)
    skipped: dict[str, int] = field(default_factory=dict)
    kept: list[dict[str, str]] = field(default_factory=list)  # clips left unconverted because verification failed
    load_type_changes: list[str] = field(default_factory=list)
    seconds: float = 0.0
    details: list[dict[str, Any]] = field(default_factory=list)


def _resource_name(source: str) -> str:
    return source.rsplit("/", 1)[-1]


def _sha(data: bytes | memoryview) -> str:
    return hashlib.blake2b(data, digest_size=16).hexdigest()


def wants_streaming(name: str, load_type: int, length: float) -> bool:
    """Long DecompressOnLoad clips (and the named ones) would otherwise stay resident as raw PCM."""
    if load_type == LOAD_STREAMING:
        return False
    return name in STREAM_CLIPS or (load_type == LOAD_DECOMPRESS_ON_LOAD and length > STREAM_ABOVE_SECONDS)


def _map(executor: cf.Executor | None, fn: Any, items: list[Any]) -> Iterator[Any]:
    return executor.map(fn, items, chunksize=4) if isinstance(executor, cf.ProcessPoolExecutor) else map(fn, items)


def process_bundle(path: Path, quality: dict[str, int], executor: cf.Executor | None,
                   mono_gain_db: float = PAN_LAW_DB) -> BundleReport:
    """Convert every AudioClip of one bundle; rewrites the file only when all converted clips verified."""
    report = BundleReport(str(path), bytes_before=path.stat().st_size, bytes_after=path.stat().st_size)
    started = time.time()
    if not any(n.endswith(".resource") for n in bundle_node_names(path)):
        return report
    env = UnityPy.load(str(path))
    bundle = env.file
    refs: list[ClipRef] = []
    for obj in env.objects:
        if obj.type.name != "AudioClip":
            continue
        tree = obj.read_typetree()
        res = tree.get("m_Resource") or {}
        has_stream = bool(res.get("m_Source") and res.get("m_Size"))
        key = (_resource_name(res["m_Source"]), res["m_Offset"], res["m_Size"]) if has_stream else None
        refs.append(ClipRef(obj, tree, key, classify(path.name, tree["m_Name"])))
    if not refs:
        return report
    streams = {n: memoryview(f.bytes) for n, f in bundle.files.items() if any(r.key and r.key[0] == n for r in refs)}

    tasks = {r.key: ClipTask(r.key, r.tree["m_Name"], r.kind, bytes(streams[r.key[0]][r.key[1] : r.key[1] + r.key[2]]),
                             float(r.tree["m_Length"]), quality[r.kind], mono_gain_db) for r in refs if r.key}
    results: dict[tuple[str, int, int], ClipResult] = {r.key: r for r in _map(executor, convert_clip, list(tasks.values()))}
    report.clips = dict(Counter(r.status for r in results.values()))
    report.skipped = dict(Counter(r.code for r in results.values() if r.status == "skipped"))
    for r in results.values():
        if r.status == "converted":
            report.details.append({"name": r.name, "kind": r.kind, **r.stats})
        elif r.code != "already-converted":
            report.details.append({"name": r.name, "kind": r.kind, "status": r.status, "reason": r.reason})
    for r in results.values():
        if r.status == "kept":
            report.kept.append({"name": r.name, "kind": r.kind, "reason": r.reason})
        if r.status == "kept" or (r.status == "skipped" and r.code != "already-converted"):
            log.warning("%s: kept %s unchanged: %s", path.name, r.name, r.reason)

    edited: list[ClipRef] = []
    for ref in refs:
        if wants_streaming(ref.tree["m_Name"], ref.tree["m_LoadType"], float(ref.tree["m_Length"])):
            ref.tree["m_LoadType"] = LOAD_STREAMING
            report.load_type_changes.append(ref.tree["m_Name"])
            edited.append(ref)
    converted = {k: r for k, r in results.items() if r.status == "converted"}
    if not converted and not edited:
        report.seconds = time.time() - started
        return report

    # Rewrite each resource node that holds a converted clip: same clip order, tightly packed.
    placement: dict[tuple[str, int, int], tuple[int, int]] = {}
    digests: dict[tuple[str, int, int], tuple[int, str]] = {}
    for node in sorted({k[0] for k in converted}):
        spans = sorted({(k[1], k[2]) for k in tasks if k[0] == node})
        if sum(size for _, size in spans) != len(streams[node]):
            report.status = "refused"
            report.reason = f"{node}: clips cover {sum(s for _, s in spans)} of {len(streams[node])} bytes"
            return report
        blob = bytearray()
        for offset, size in spans:
            key = (node, offset, size)
            data = converted[key].data if key in converted else bytes(streams[node][offset : offset + size])
            placement[key] = (len(blob), len(data))
            digests[key] = (len(data), _sha(data))
            blob += data
        replacement = EndianBinaryReader(bytes(blob))
        replacement.flags = bundle.files[node].flags
        bundle.files[node] = replacement
    for ref in refs:
        if ref.key in placement:
            ref.tree["m_Resource"]["m_Offset"], ref.tree["m_Resource"]["m_Size"] = placement[ref.key]
            if ref.key in converted:
                r = converted[ref.key]
                ref.tree.update(m_Channels=r.channels, m_Frequency=r.rate, m_BitsPerSample=16, m_Length=r.samples / r.rate)
            if ref not in edited:
                edited.append(ref)
    for ref in edited:
        ref.obj.save_typetree(ref.tree)

    packed = bundle.save(packer="lz4")
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_bytes(packed)
    try:
        _read_back(tmp, {ref.obj.path_id: (ref.tree, ref.key, ref.key in converted) for ref in refs}, digests)
    except AudioError as exc:
        tmp.unlink(missing_ok=True)
        report.status, report.reason = "refused", f"read-back of the repacked bundle failed: {exc}"
        return report
    tmp.replace(path)
    report.status, report.bytes_after, report.seconds = "converted", len(packed), time.time() - started
    return report


def _read_back(tmp: Path, expected: dict[int, tuple[dict[str, Any], Any, bool]],
               digests: dict[tuple[str, int, int], tuple[int, str]]) -> None:
    """Re-open the saved bundle and check every clip's fields and stream bytes against what was meant to be written."""
    env = UnityPy.load(str(tmp))
    seen = 0
    for obj in env.objects:
        if obj.type.name != "AudioClip":
            continue
        if obj.path_id not in expected:
            raise AudioError(f"unexpected AudioClip path_id {obj.path_id}")
        want, key, was_converted = expected[obj.path_id]
        tree = obj.read_typetree()
        for name in ("m_Name", "m_LoadType", "m_Channels", "m_Frequency", "m_BitsPerSample", "m_CompressionFormat"):
            if tree[name] != want[name]:
                raise AudioError(f"{tree['m_Name']}: {name} is {tree[name]}, expected {want[name]}")
        if abs(tree["m_Length"] - want["m_Length"]) > 1e-4:
            raise AudioError(f"{tree['m_Name']}: m_Length {tree['m_Length']} != {want['m_Length']}")
        res = tree["m_Resource"]
        if key:
            node = memoryview(env.file.files[_resource_name(res["m_Source"])].bytes)
            data = node[res["m_Offset"] : res["m_Offset"] + res["m_Size"]]
            if (res["m_Offset"], res["m_Size"]) != tuple(want["m_Resource"][k] for k in ("m_Offset", "m_Size")):
                raise AudioError(f"{tree['m_Name']}: resource range moved")
            if key in digests and (len(data), _sha(data)) != digests[key]:
                raise AudioError(f"{tree['m_Name']}: stream bytes differ")
            if was_converted:
                fsb = parse_fsb5_vorbis(bytes(data))
                if (fsb.channels, fsb.rate) != (tree["m_Channels"], tree["m_Frequency"]) or \
                        abs(fsb.samples / fsb.rate - tree["m_Length"]) > MAX_DURATION_ERROR_S:
                    raise AudioError(f"{tree['m_Name']}: FSB5 header disagrees with the AudioClip")
        seen += 1
    if seen != len(expected):
        raise AudioError(f"{seen} AudioClips after saving, expected {len(expected)}")


# --------------------------------------------------------------------------------------------------
# Driver
# --------------------------------------------------------------------------------------------------
def find_bundles(data_dir: Path, only: str | None) -> list[Path]:
    root = data_dir / "Raw" / "aa" / "StandaloneOSX"
    if not root.is_dir():
        raise SystemExit(f"{root} does not exist")
    found = sorted(root.rglob("*.bundle"))
    if only:
        found = [p for p in found if fnmatch.fnmatch(p.name, only) or fnmatch.fnmatch(str(p.relative_to(root)), only)]
    return found


def check_ffmpeg() -> None:
    if shutil.which(FFMPEG) is None:
        raise SystemExit(f"ffmpeg not found ({FFMPEG}); install it with libvorbis support")
    encoders = subprocess.run([FFMPEG, "-hide_banner", "-encoders"], capture_output=True, text=True, check=False).stdout
    if "libvorbis" not in encoders:
        raise SystemExit("this ffmpeg has no libvorbis encoder")


def run(data_dir: Path, voice_quality: int, music_quality: int, only: str | None = None, jobs: int | None = None,
        report_path: Path | None = None, mono_gain_db: float = PAN_LAW_DB) -> int:
    check_ffmpeg()
    quality = {KIND_VOICE: voice_quality, KIND_MUSIC: music_quality, KIND_SFX: music_quality}
    bundles = find_bundles(data_dir, only)
    jobs = jobs or os.cpu_count() or 4
    executor = cf.ProcessPoolExecutor(max_workers=jobs, initializer=_worker_init) if jobs > 1 else None
    started = time.time()
    reports: list[BundleReport] = []
    try:
        for path in bundles:
            rep = process_bundle(path, quality, executor, mono_gain_db)
            if rep.status != "unchanged" or rep.clips:
                log.info("%s: %s %s -> %s MB, clips %s, skipped %s, streaming %s, %.0fs %s", path.name, rep.status,
                         f"{rep.bytes_before / 1e6:.1f}", f"{rep.bytes_after / 1e6:.1f}", rep.clips, rep.skipped,
                         rep.load_type_changes, rep.seconds, rep.reason)
            reports.append(rep)
    finally:
        if executor:
            executor.shutdown()
    touched = [r for r in reports if r.status == "converted"]
    refused = [r for r in reports if r.status == "refused"]
    totals = Counter()
    for r in reports:
        totals.update(r.clips)
    log.info("done in %.0fs: %d bundle(s) rewritten, %d unchanged, %d refused; clips %s; %.1f MB -> %.1f MB",
             time.time() - started, len(touched), len(reports) - len(touched) - len(refused), len(refused), dict(totals),
             sum(r.bytes_before for r in touched) / 1e6, sum(r.bytes_after for r in touched) / 1e6)
    for r in reports:
        for clip in r.kept:
            log.warning("kept unconverted: %s :: %s (%s)", Path(r.path).name, clip["name"], clip["reason"])
    for r in refused:
        log.error("refused %s: %s", r.path, r.reason)
    if report_path:
        report_path.write_text(json.dumps({"bundles": [dataclasses.asdict(r) for r in reports],
                                           "quality": quality, "mono_gain_db": mono_gain_db,
                                           "seconds": time.time() - started}, indent=1))
    return 1 if refused else 0


def main(argv: Iterable[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("data_dir", type=Path, help="Data/ directory containing Raw/aa/StandaloneOSX (modified in place)")
    ap.add_argument("--voice-quality", type=int, default=DEFAULT_VOICE_QUALITY, help="Unity/FMOD quality 1-100 for voice")
    ap.add_argument("--music-quality", type=int, default=DEFAULT_MUSIC_QUALITY, help="quality for BGM, songs and SFX")
    ap.add_argument("--mono-gain-db", type=float, default=PAN_LAW_DB,
                    help="gain added when a stereo clip is downmixed to mono (default %(default).2f = FMOD pan-law "
                         "compensation; 0 keeps the plain average)")
    ap.add_argument("--only", help="glob on the bundle file name or its path below StandaloneOSX")
    ap.add_argument("--report", type=Path, help="write a JSON report")
    ap.add_argument("--jobs", type=int, help="worker processes (default: all cores; 1 runs inline)")
    args = ap.parse_args(list(argv) if argv is not None else None)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    return run(args.data_dir, args.voice_quality, args.music_quality, args.only, args.jobs, args.report, args.mono_gain_db)


if __name__ == "__main__":
    sys.exit(main())
