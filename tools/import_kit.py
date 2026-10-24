#!/usr/bin/env python3
"""Builds a drum kit's WAVs from its source samples (run by hand, once).

For each sample line in kits/<kit>/kit.txt it downloads the source file
(kit.txt's `source` URL + the file name) unless it is already in the cache,
decodes it (FLAC needs the `flac` tool: brew install flac), and writes a
16-bit mono 44.1 kHz WAV into the kit folder:

  - the leading silence is trimmed, so the hit starts on its step (0.5 ms
    before the onset, faded in)
  - the tail is cut at `max` seconds (or where it falls below -60 dB) with
    a raised-cosine fade over the last `fade` seconds
  - each sample peaks at the lane's `peak` (dBFS) x velocity / 9, so a layer
    played at its own velocity has that velocity's level (the engine scales
    a hit by its velocity / the layer's)

  python3 tools/import_kit.py kits/rock [--cache DIR]

The WAVs are committed; tools/make_kits.py compiles them into the firmware.
"""
import math
import os
import subprocess
import sys
import tempfile
import urllib.request
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_kits import FS, read_kit  # noqa: E402

ONSET_DB = -40.0    # the hit starts at the first sample this far below the peak
PRE_ROLL = 0.0005   # kept before it, in seconds
TAIL_DB = -60.0     # the tail ends where it stays this far below the peak


def fetch(url, path):
    if not os.path.exists(path):
        print("  download", url)
        with urllib.request.urlopen(url) as r, open(path, "wb") as f:
            f.write(r.read())


def decode(path):
    """Returns (rate, mono samples as floats in -1..1)."""
    wav = path
    if path.lower().endswith(".flac"):
        wav = path[:-5] + ".wav"
        if not os.path.exists(wav):
            subprocess.check_call(["flac", "-s", "-d", "-f", "-o", wav, path])
    with wave.open(wav) as w:
        ch, width, rate = w.getnchannels(), w.getsampwidth(), w.getframerate()
        data = w.readframes(w.getnframes())
    if width not in (2, 3):
        sys.exit("%s: only 16- or 24-bit sources" % path)
    scale = 1.0 / (1 << (8 * width - 1))
    vals = [int.from_bytes(data[i:i + width], "little", signed=True) * scale
            for i in range(0, len(data), width)]
    mono = [sum(vals[i:i + ch]) / ch for i in range(0, len(vals), ch)]
    return rate, mono


def shape(x, rate, s):
    peak = max(abs(v) for v in x) or 1.0
    on = next(i for i, v in enumerate(x) if abs(v) >= peak * 10 ** (ONSET_DB / 20))
    start = max(0, on - int(PRE_ROLL * rate))
    last = max(i for i, v in enumerate(x) if abs(v) >= peak * 10 ** (TAIL_DB / 20))
    end = min(last + 1, start + int(s["max"] * rate))
    y = x[start:end]
    # Fade in over the pre-roll, so the first sample is 0.
    nin = max(1, on - start)
    for i in range(nin):
        y[i] *= i / nin
    nf = min(len(y), int(s["fade"] * rate))
    for i in range(nf):
        y[len(y) - nf + i] *= 0.5 + 0.5 * math.cos(math.pi * (i + 1) / nf)
    return y


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    folder = os.path.abspath(args[0])
    cache = args[args.index("--cache") + 1] if "--cache" in args else tempfile.mkdtemp(prefix="kit-")
    os.makedirs(cache, exist_ok=True)
    kit, samples = read_kit(folder)
    shaped = []
    for s in samples:
        src = os.path.join(cache, s["source"])
        fetch(kit["source"] + s["source"], src)
        rate, x = decode(src)
        if rate != FS:
            sys.exit("%s: %d Hz; only %d Hz sources for now" % (s["source"], rate, FS))
        shaped.append(shape(x, rate, s))
    for i, s in enumerate(samples):
        gain = 10 ** (s["peak"] / 20) * s["vel"] / 9 / max(abs(v) for v in shaped[i])
        shaped[i] = [v * gain for v in shaped[i]]
    total = 0
    for s, y in zip(samples, shaped):
        pcm = bytearray()
        for v in y:
            pcm += max(-32768, min(32767, int(round(v * 32767)))).to_bytes(2, "little", signed=True)
        with wave.open(os.path.join(folder, s["file"]), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(FS)
            w.writeframes(bytes(pcm))
        total += len(pcm)
        print("  %-22s %.2f s  %6.1f KB" % (s["file"], len(y) / FS, len(pcm) / 1024))
    print("%s: %d samples, %.0f KB" % (kit["name"], len(samples), total / 1024))


if __name__ == "__main__":
    main()
