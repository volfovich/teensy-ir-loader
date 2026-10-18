# Teensy IR Loader

A low-latency guitar cabinet IR loader for the **Teensy 4.0 + Audio Shield (Rev D)**.

```
tube amp LINE OUT ──► shield LINE IN (L) ──► FFT convolution ──► LINE OUT pads + headphone jack
                                                (IR from SD card)
```

## Features

- **Full-length IRs:** up to 8192 samples (186 ms at 44.1 kHz). That is far longer than any cabinet IR needs.
- **Zero added latency from the convolution.** The partition size equals the audio block size, so total latency is only the audio buffering, about 1.5 ms at the default 32-sample blocks, plus the codec's converter delay.
- **Just copy WAVs to the SD card:**
  - Files can go in the root folder or an `/IR` folder.
  - Any sample rate from 8 to 192 kHz is accepted and resampled on the Teensy.
  - Mono or stereo files, 8/16/24/32-bit PCM or 32/64-bit float, including `WAVE_FORMAT_EXTENSIBLE`.
  - Each IR is loudness-normalized on load, so switching IRs does not cause big level jumps.
  - Leading silence (pure latency) and the inaudible tail (wasted CPU) are trimmed automatically.
- **Click-free switching:** IR changes use a 25 ms crossfade, and the audio never drops out while a file loads.
- **Remembers settings:** the last IR and your levels are stored in EEPROM.
- **Controls:** USB serial commands, plus optional *next*, *previous* and *bypass* buttons.

## Hardware

| Connection | Shield pad / pin | Notes |
|---|---|---|
| Amp line out signal | **LINE IN L** | Only the left input is processed. |
| Amp line out ground | **GND** next to the line-in pads | |
| To mixer / interface / power amp | **LINE OUT L** (or R, both carry the same mono signal) + GND | If the gear you feed is not AC-coupled, add a 2.2–10 µF capacitor in series. |
| Headphones | 3.5 mm jack | **Headphones only.** The jack's ground is a *virtual* ground, so never connect it to another device's ground. |
| Buttons (optional) | pins 2 (next), 3 (prev), 4 (bypass) to GND | Change or disable them in `include/config.h`. |

### ⚠️ Level and safety

- **Never connect a speaker output to the shield,** and never run a tube amp without a speaker or load box attached, even when you are only using its line out.
- **Tube-amp line outs can be hot.** The firmware starts at the least sensitive input setting (`in 0` = 3.12 V peak-to-peak). To check your level:
  1. Type `m` to show the live meter.
  2. Play hard. The input should peak below about −3 dBFS.
  3. If it still shows `CLIP!`, add a resistive pad: a **10 kΩ resistor in series** with the signal, then **4.7 kΩ to ground** (about −10 dB) or **2.2 kΩ to ground** (about −15 dB).
  4. If the signal is weak instead, raise the input sensitivity with `in 3`, `in 5`, and so on.
- **Ground loops:** if you hear hum, check that the amp and the Teensy's USB power are not creating a loop. A USB power bank is a quick way to test this.

## SD card

1. Format the card as **FAT32 or exFAT**.
2. Copy `.wav` files to the root folder or to `/IR`.
3. Insert the card into the Audio Shield's slot.

Hidden files (such as macOS `._name.wav` files) are ignored. Files are listed alphabetically, so a numeric prefix like `01 V30 SM57.wav` sets the order.

## Build and upload

1. Install VS Code and the PlatformIO extension, or the `pio` command-line tool.
2. Open this folder.
3. Build and upload (this opens the Teensy Loader; press the button on the board if asked):
   ```
   pio run -t upload
   ```
4. Open the serial monitor:
   ```
   pio device monitor
   ```

PlatformIO is used instead of the Arduino IDE because the block size is set with a build flag (`-D AUDIO_BLOCK_SAMPLES=32` in `platformio.ini`). The Arduino IDE would require editing Teensyduino's `AudioStream.h` instead.

## Serial commands

| Command | Action |
|---|---|
| `l` | List IR files (`>` marks the current one) |
| `3` | Load IR number 3 |
| `n` / `p` | Next / previous IR |
| `b` | Toggle bypass (dry signal, with a click-free fade) |
| `g -6` | Output gain after the IR, in dB |
| `in 0..15` | Line-in sensitivity (0 = 3.12 V peak-to-peak, 5 = 1.33 V peak-to-peak) |
| `out 13..31` | Line-out level (13 = loudest, 3.16 V peak-to-peak) |
| `hp 0..100` | Headphone volume |
| `m` | Live input/output meter |
| `s` | Status: CPU load, peak levels, clip count, latency |
| `r` | Rescan the SD card (after swapping cards) |

## Latency and CPU

The block size is set in `platformio.ini`. CPU figures below are estimates from the compiled code. Run `s` on the device to see real numbers.

| `AUDIO_BLOCK_SAMPLES` | I/O buffering | Convolver CPU, 8192-sample IR | During a 25 ms IR switch |
|---|---|---|---|
| 16 | ~0.7 ms | ~25 % | ~50 % |
| **32 (default)** | **~1.5 ms** | **~12 %** | ~25 % |
| 64 | ~2.9 ms | ~7 % | ~14 % |
| 128 | ~5.8 ms | ~4 % | ~8 % |

The codec's ADC and DAC add their own fixed delay on top of the buffering. To measure your exact total, loop LINE OUT back into LINE IN R and record both channels.

CPU cost scales with IR length. A typical 2048-sample cabinet IR uses about a quarter of the figures above.

Smaller blocks cost more CPU because the same IR is split into more partitions, and every partition is processed each block. At 16 samples there is still plenty of headroom, so try it if you want the lowest possible latency.

Memory use with 32-sample blocks:

- **RAM1 (fast DTCM/ITCM):** about 360 of 512 KB. This holds the IR spectra and the input history used every block.
- **RAM2 (OCRAM/DMAMEM):** about 250 of 512 KB. This holds the file-loading buffers.

## How it works

The convolution uses **uniformly partitioned overlap-save** (UPOLS):

1. The IR is split into `N = length / B` partitions of `B` samples each. Each partition is zero-padded to `2B` samples and stored as a spectrum.
2. Every audio block, the last `2B` input samples are FFT'd and pushed into a ring buffer of past input spectra (the frequency-domain delay line).
3. The output spectrum is the sum, over all partitions, of each past input spectrum multiplied by its matching IR partition spectrum.
4. One inverse FFT turns that sum into the output. Its second half is exactly the next `B` output samples, with no added delay.

When switching IRs, the new IR's spectra are computed in the background into a second buffer. For 25 ms both IRs are evaluated against the same input history and crossfaded. That is why switching causes no click and no tail cut-off.

## Project layout

```
platformio.ini              build settings (block size here)
include/config.h            all tunable settings (IR length, pins, levels, normalization)
src/main.cpp                setup() and loop()
src/AudioGraph.{h,cpp}      audio objects, codec, level and bypass controls
src/IRLibrary.{h,cpp}       SD card scanning and IR loading
src/Settings.{h,cpp}        settings saved in EEPROM
src/SerialUI.{h,cpp}        serial commands and level meter
src/Buttons.{h,cpp}         next / previous / bypass buttons
src/AudioConvolverIR.h      Teensy Audio Library object (gain, bypass, meters, CPU stats)
lib/irdsp/src/              portable DSP core (no Arduino dependencies)
  PartitionedConvolver.h    the UPOLS convolution engine
  irdsp_fft.h               small radix-2 FFT
  IRLoader.{h,cpp}          WAV parser, resampler, IR trimming and normalization
test/host/                  PC tests for the DSP core
```

## Testing on a PC

The DSP core builds with any C++17 compiler. The loader tests need Python 3 and numpy.

```
cd test/host
make test
```

The tests check:

- Convolution against a direct (time-domain) reference for block sizes 16–128 and IR lengths 1–8192.
- That crossfades are exact.
- WAV decoding for every supported format.
- Resampling accuracy at 22.05, 32, 48, 88.2, 96 and 192 kHz.
- Anti-aliasing, trimming, truncation and normalization.

`test/host/irtool file.wav out.f32` runs the exact on-device loading pipeline on a file, which is useful for seeing how the Teensy will interpret an IR before copying it to the card.

## Ideas for next steps

- **OLED + rotary encoder:** an SSD1306 display on the I2C pins (18/19) is already shared with the codec, so it can be added without extra wiring.
- **Tone controls:** low-cut, high-cut and a presence biquad after the convolver.
- **Two-IR blending:** the engine already evaluates two IRs during crossfades.
- **USB audio:** build with `-D USB_MIDI_AUDIO_SERIAL` and add `AudioOutputUSB` to record directly into a computer (verify it with your chosen block size).
