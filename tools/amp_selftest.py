#!/usr/bin/env python3
"""Checks the Teensy's amp against the PC: the same model on the same signal.

The loaded model runs on the device ("amp selftest", fed over the USB
serial) and on the PC (test/host/namtool, the same engine and file reader
built for the host, which matches NeuralAmpModelerCore). They must agree to
float rounding; a bigger difference is a firmware bug.

  python3 tools/amp_selftest.py <model.nam> [--input in.wav] [--save out.f32] [--port PORT]

<model.nam> is a copy of the model loaded on the device (amp info). The
input defaults to test_nam's golden signal (1 s: plucks, a sweep, noise
bursts); --input takes a mono WAV at 48 kHz (16/24-bit or float), e.g. a DI
recording. --save writes the device's output (raw float32) for a closer
look. The amp is turned off for the test and back on after, if it was on.
Needs pyserial (PlatformIO's Python has it).
"""
import argparse
import array
import glob
import math
import os
import struct
import subprocess
import sys
import tempfile
import time

import serial

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "test", "host")
B = 32  # AUDIO_BLOCK_SAMPLES


def read_wav(path):
    """Mono samples of a WAV (PCM 16/24/32-bit or float32) as floats."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        sys.exit(f"{path}: not a WAV file")
    pos, fmt, samples = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            samples = body
        pos += 8 + size + (size & 1)
    if not fmt or samples is None:
        sys.exit(f"{path}: no fmt or data chunk")
    tag, channels, rate, _, _, bits = fmt
    if rate != 48000:
        print(f"warning: {path} is {rate} Hz, the amp runs at 48000")
    if tag == 0xFFFE:  # WAVE_FORMAT_EXTENSIBLE: the real tag is in the GUID
        tag = struct.unpack("<H", data[data.find(b"fmt ") + 32:data.find(b"fmt ") + 34])[0]
    width = bits // 8
    n = len(samples) // (width * channels)
    out = []
    for i in range(n):
        o = i * width * channels  # the first channel
        s = samples[o:o + width]
        if tag == 3 and bits == 32:
            out.append(struct.unpack("<f", s)[0])
        elif bits == 16:
            out.append(struct.unpack("<h", s)[0] / 32768.0)
        elif bits == 24:
            out.append(int.from_bytes(s, "little", signed=True) / 8388608.0)
        elif bits == 32:
            out.append(struct.unpack("<i", s)[0] / 2147483648.0)
        else:
            sys.exit(f"{path}: {bits}-bit format {tag} not supported")
    return out


def tool(name):
    path = os.path.join(HOST, name)
    if not os.path.exists(path):
        subprocess.run(["make", "-C", HOST, name], check=True, stdout=subprocess.DEVNULL)
    return path


class Device:
    def __init__(self, port):
        ports = [port] if port else sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
        if not ports:
            sys.exit("no Teensy found (pass --port)")
        self.s = serial.Serial(ports[0], 115200, timeout=2)
        time.sleep(0.2)
        self.s.reset_input_buffer()

    def command(self, line, wait=0.6):
        """Sends a command; returns what came back within `wait` seconds."""
        self.s.write((line + "\n").encode())
        end, got = time.time() + wait, b""
        while time.time() < end:
            got += self.s.read(self.s.in_waiting or 1)
        return got.decode(errors="replace")

    def selftest(self, x):
        self.s.write(f"amp selftest {len(x)}\n".encode())
        end = time.time() + 3
        line = b""
        while time.time() < end and not line.startswith(b"selftest"):
            line = self.s.readline().strip()
            if line.startswith(b"Load") or line.startswith(b"amp selftest"):
                sys.exit("device: " + line.decode())
        if not line.startswith(b"selftest"):
            sys.exit("the device didn't start the self-test (old firmware?)")
        y = array.array("f")
        chunk = 16 * B
        for i in range(0, len(x), chunk):
            block = array.array("f", x[i:i + chunk])
            self.s.write(block.tobytes())
            want = len(block) * 4
            got = self.s.read(want)
            if len(got) != want:
                sys.exit(f"device stopped answering at sample {i}")
            y.frombytes(got)
        return list(y)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("model")
    ap.add_argument("--input")
    ap.add_argument("--save")
    ap.add_argument("--port")
    a = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        wav = a.input
        if not wav:
            wav = os.path.join(tmp, "golden.wav")
            subprocess.run([tool("test_nam"), "--write-input", wav], check=True, cwd=HOST)
        x = read_wav(wav)
        x += [0.0] * (-len(x) % B)
        inp, ref = os.path.join(tmp, "in.f32"), os.path.join(tmp, "ref.f32")
        with open(inp, "wb") as f:
            array.array("f", x).tofile(f)
        r = subprocess.run([tool("namtool"), a.model, inp, ref], capture_output=True, text=True)
        if r.returncode:
            sys.exit(r.stdout + r.stderr)
        want = array.array("f")
        with open(ref, "rb") as f:
            want.frombytes(f.read())

    dev = Device(a.port)
    info = dev.command("amp info")
    loaded = next((l.split(None, 1)[1] for l in info.splitlines() if l.startswith("File:")), "")
    if os.path.basename(loaded.strip()).lower() != os.path.basename(a.model).lower():
        sys.exit(f"the device has {loaded.strip() or 'no model'} loaded, not {os.path.basename(a.model)}"
                 " (load it with: amp <name>)")
    status = dev.command("s")
    was_on = any(l.startswith("Amp:") and l.split()[1].startswith("on") for l in status.splitlines())
    if was_on:
        dev.command("amp off", 0.3)
    try:
        t0 = time.time()
        y = dev.selftest(x)
        secs = time.time() - t0
    finally:
        if was_on:
            dev.command("amp on", 0.2)

    if a.save:
        with open(a.save, "wb") as f:
            array.array("f", y).tofile(f)
    err = max(abs(p - q) for p, q in zip(y, want))
    peak = max(abs(q) for q in want)
    e2 = sum((p - q) ** 2 for p, q in zip(y, want))
    s2 = sum(q * q for q in want)
    db = 10 * math.log10(s2 / e2) if e2 > 0 else float("inf")
    print(f"{len(x)} samples through the device in {secs:.1f} s")
    print(f"max difference {err:.2e} (output peak {peak:.3f}), {db:.0f} dB below the signal")
    if err < 1e-4 and db > 100:
        print("OK: the Teensy's amp matches the PC")
    else:
        print("MISMATCH: the device's amp differs from the PC")
        sys.exit(1)


if __name__ == "__main__":
    main()
