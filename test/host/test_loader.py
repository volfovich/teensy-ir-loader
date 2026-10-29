#!/usr/bin/env python3
"""Tests the IR loading pipeline (irtool = same code as the firmware).

Generates WAV files in many formats, runs ./irtool on them and checks the
decoded / resampled / normalized result. Needs numpy.
"""
import os, struct, subprocess, sys, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
IRTOOL = os.path.join(HERE, "irtool")
RATE = 48000  # the firmware's rate (irtool's target, as on the Teensy)
TMP = tempfile.mkdtemp(prefix="irtest_")
failures = 0


def wav_bytes(data, rate, fmt, bytes_per, extensible=False, extra_chunks=b""):
    """data: (frames, channels) float array in [-1, 1)."""
    data = np.atleast_2d(np.asarray(data, dtype=np.float64).T).T
    frames, ch = data.shape
    if fmt == 3:
        raw = data.astype("<f4" if bytes_per == 4 else "<f8").tobytes()
    else:
        if bytes_per == 1:
            raw = np.clip(np.round(data * 128 + 128), 0, 255).astype("u1").tobytes()
        elif bytes_per == 2:
            raw = np.clip(np.round(data * 32768), -32768, 32767).astype("<i2").tobytes()
        elif bytes_per == 3:
            v = np.clip(np.round(data * 8388608), -8388608, 8388607).astype("<i4")
            raw = b"".join(int(x).to_bytes(4, "little", signed=True)[:3] for x in v.ravel())
        else:
            raw = np.clip(np.round(data * 2147483648.0), -2147483648, 2147483647).astype("<i4").tobytes()
    block = ch * bytes_per
    if extensible:
        guid = struct.pack("<H", fmt) + bytes.fromhex("000000001000800000aa00389b71")
        fmt_body = struct.pack("<HHIIHHHHI", 0xFFFE, ch, rate, rate * block, block, bytes_per * 8, 22, bytes_per * 8, 0) + guid
    else:
        fmt_body = struct.pack("<HHIIHH", fmt, ch, rate, rate * block, block, bytes_per * 8)
    body = b"WAVE" + extra_chunks + b"fmt " + struct.pack("<I", len(fmt_body)) + fmt_body
    body += b"data" + struct.pack("<I", len(raw)) + raw
    return b"RIFF" + struct.pack("<I", len(body)) + body


def run(name, blob, *args):
    path = os.path.join(TMP, name + ".wav")
    out = os.path.join(TMP, name + ".f32")
    with open(path, "wb") as f:
        f.write(blob)
    p = subprocess.run([IRTOOL, path, out] + [str(a) for a in args], capture_output=True, text=True)
    info = p.stdout.strip()
    if p.returncode != 0:
        return info, None
    return info, np.fromfile(out, dtype="<f4")


def check(ok, label, detail=""):
    global failures
    print(f"  {label:<58} {'OK' if ok else 'FAIL'} {detail}")
    if not ok:
        failures += 1


def bandlimited_ir(rate, dur=0.12, fmax=16000.0):
    """Continuous-time test IR (Gaussian-windowed sines) sampled at `rate`,
    with the 1/fs scaling of a sampled impulse response."""
    t = np.arange(int(dur * rate)) / rate
    freqs = [180, 950, 2400, 5100, 9800, fmax]
    s = sum(np.sin(2 * np.pi * f * t + i) / (i + 1) for i, f in enumerate(freqs))
    w = np.exp(-0.5 * ((t - 0.05) / 0.012) ** 2)
    return s * w * 0.4 * (RATE / rate)


print("Format decoding (48 kHz, no trim, no normalization)")
rng = np.random.default_rng(1)
left = rng.uniform(-0.9, 0.9, 3000) * np.exp(-np.arange(3000) / 600)
right = -left[::-1].copy()
stereo = np.stack([left, right], axis=1)
for fmt, bp, ext, tol, label in [
    (1, 1, False, 1 / 128, "8-bit PCM"),
    (1, 2, False, 1 / 32768, "16-bit PCM"),
    (1, 3, False, 2e-7, "24-bit PCM"),
    (1, 4, False, 1e-7, "32-bit PCM"),
    (3, 4, False, 1e-7, "32-bit float"),
    (3, 8, False, 1e-7, "64-bit float"),
    (1, 3, True, 2e-7, "24-bit PCM, WAVE_FORMAT_EXTENSIBLE"),
    (3, 4, True, 1e-7, "32-bit float, WAVE_FORMAT_EXTENSIBLE"),
]:
    info, y = run(f"fmt_{fmt}_{bp}_{ext}", wav_bytes(stereo, RATE, fmt, bp, ext), 8192, "none", 0)
    ok = y is not None and len(y) == 3000 and np.max(np.abs(y - left)) <= tol * 1.01
    check(ok, f"{label} stereo -> left channel", "" if y is not None else info)

info, y = run("mix", wav_bytes(np.stack([left, left * 0.5], 1), RATE, 3, 4), 8192, "none", 0, 1)
check(y is not None and np.allclose(y, left * 0.75, atol=1e-6), "channel mix mode averages channels")

junk = b"JUNK" + struct.pack("<I", 5) + b"abcde\x00" + b"LIST" + struct.pack("<I", 4) + b"INFO"
info, y = run("chunks", wav_bytes(left, RATE, 1, 2, extra_chunks=junk), 8192, "none", 0)
check(y is not None and len(y) == 3000, "skips odd-sized JUNK and LIST chunks")

info, y = run("bad", b"RIFX" + b"\x00" * 60)
check(y is None and "ERROR" in info, "rejects non-WAV data", f"({info})")

print("Resampling (compared with the continuous-time reference)")
for rate, fmax in [(44100, 16000.0), (96000, 18000.0), (88200, 18000.0), (22050, 9500.0), (32000, 14000.0)]:
    src = bandlimited_ir(rate, fmax=fmax)
    ref = bandlimited_ir(RATE, fmax=fmax)
    for mode in ["table", "exact"]:
        info, y = run(f"rs_{rate}_{mode}", wav_bytes(src, rate, 3, 4), 8192, "none", 0, 0, mode)
        n = min(len(ref), len(y)) - 200
        err = np.max(np.abs(y[200:n] - ref[200:n])) / np.max(np.abs(ref))
        check(err < 2e-3, f"{rate} Hz -> {RATE} Hz ({mode} kernel)", f"rel err {err:.1e}, len {len(y)}")

# High frequency content above the new Nyquist must be removed (anti-alias).
t = np.arange(4800) / 96000
tone = 0.5 * np.sin(2 * np.pi * 30000 * t) * np.hanning(4800)
info, y = run("alias", wav_bytes(tone, 96000, 3, 4), 8192, "none", 0)
check(np.max(np.abs(y)) < 1e-3, "96 kHz: 30 kHz tone suppressed (no aliasing)", f"peak {np.max(np.abs(y)):.1e}")

print("Conditioning")
long_ir = rng.uniform(-1, 1, 96000) * np.exp(-np.arange(96000) / 30000) * 0.5
info, y = run("long", wav_bytes(long_ir, 48000, 3, 4), 8192, "none", 1)
check("truncated=1" in info and 8192 - 256 <= len(y) <= 8192, "2 s IR at 48 kHz -> truncated to 8192", f"({len(y)} samples)")
check(abs(y[-1]) < abs(y).max() * 1e-2, "truncation edge is faded out")

hi_rate = rng.uniform(-1, 1, 192000) * np.exp(-np.arange(192000) / 20000)
info, y = run("r192", wav_bytes(hi_rate, 192000, 3, 4), 8192, "energy", 1)
check(y is not None and len(y) > 8000 and "truncated=1" in info, "1 s IR at 192 kHz fits the load buffers", f"({info[:40]})")

hi_rate = rng.uniform(-1, 1, 384000) * np.exp(-np.arange(384000) / 40000)
info, y = run("r384", wav_bytes(hi_rate, 384000, 3, 4), 8192, "energy", 1)
check(y is not None and len(y) > 8000 and "truncated=1" in info, "1 s IR at 384 kHz streams through the load buffer", f"({info[:40]})")

delayed = np.concatenate([np.zeros(100), left])
info, y = run("lead", wav_bytes(delayed, RATE, 3, 4), 8192, "none", 1)
check("lead=100" in info and np.allclose(y[:50], left[:50], atol=1e-7), "100 samples of leading silence removed")

tail = np.concatenate([left[:2000], np.zeros(5000)])
info, y = run("tail", wav_bytes(tail, RATE, 3, 4), 8192, "none", 1)
check(len(y) <= 2000, "silent tail removed (saves CPU)", f"({len(y)} samples)")

info, y = run("energy", wav_bytes(left, RATE, 1, 2), 8192, "energy", 0)
check(abs(np.sum(y.astype(np.float64) ** 2) - 1) < 1e-4, "energy normalization -> unit energy")
info, y = run("peak", wav_bytes(left * 0.1, RATE, 1, 2), 8192, "peak", 0)
check(abs(np.max(np.abs(y)) - 1) < 1e-6, "peak normalization -> peak 1.0")

info, y = run("silent", wav_bytes(np.zeros(1000), RATE, 1, 2), 8192, "energy", 1)
check(y is None and "silent" in info, "rejects a silent IR", f"({info})")

print("\nALL LOADER TESTS PASSED" if not failures else f"\n{failures} FAILURE(S)")
sys.exit(1 if failures else 0)
