# Teensy IR Loader

A low-latency guitar cabinet IR loader for the **Teensy 4.0 + Audio Shield (Rev D)**.

```
tube amp LINE OUT ──► shield LINE IN (L) ──► amp ──────────► FFT convolution ──► delay ──────────► reverb ───────────► LINE OUT L/R + headphone jack
                                             (NAM A2,          (IR from SD card)    (studio 80s,      (80s hall or
                                              optional)                              stereo)           gated, stereo)
                                                                                    each effect: dry/wet mix, on/off
                                                                + drum machine (sampled or synthesized kit, stereo, added to the outputs)
```

## Features

- **Long IRs:** up to 4096 samples (85 ms at 48 kHz), longer than any cabinet has audible response. Longer files are cut there with a short fade (`IR_MAX_LENGTH` in `include/config.h`).
- **Zero added latency from the convolution.** The partition size equals the audio block size, so total latency is only the audio buffering, about 1.3 ms at the default 32-sample blocks, plus the codec's converter delay.
- **Just copy WAVs to the SD card:**
  - Files can go in the root folder or an `/IR` folder.
  - Any sample rate from 8 to 384 kHz is accepted and resampled on the Teensy. The file is streamed from the card, so its length does not matter. Above 192 kHz loading is slower.
  - Mono or stereo files, 8/16/24/32-bit PCM or 32/64-bit float, including `WAVE_FORMAT_EXTENSIBLE`.
  - Each IR is loudness-normalized on load, so switching IRs does not cause big level jumps.
  - Leading silence (pure latency) and the inaudible tail (wasted CPU) are trimmed automatically.
- **Click-free switching:** IR changes use a 25 ms crossfade, and the audio never drops out while a file loads.
- **Studio 80s stereo delay after the IR** (details in [DELAY.md](DELAY.md)): a wet/dry/wet dual delay voiced after the digital racks of 80s guitar rigs (TC 2290, Korg SDD-3000, Roland SDE-3000). Left repeats at T and right at ¾ T, from 20 to 750 ms. Pitch-stable repeats, with ducking under your playing, input-preamp grit and an "era" control for converter bandwidth and resolution.
- **80s stereo reverb after the delay** (details in [REVERB.md](REVERB.md)): the rack reverbs of 80s rock and metal records, in two modes:
  - a Lexicon-style **hall** (default): big, wide, chorused tail
  - an AMS "Nonlin"-style **gated** reverb: a dense burst that stops dead
  - both with pre-delay, dry/wet mix, size, tone and the era's converter character
- **Both effects:**
  - They run in 32-bit float in the same audio object as the convolution, so the path from the IR through the delay and reverb never drops to 16 bits.
  - Switching an effect off is click-free and lets its tail ring out. When the tail has died away, the effect stops using CPU and passes its input through bit-identically.
- **Optional amp block** (details in [NAM.md](NAM.md)): plays Neural Amp Modeler **A2** captures (`.nam` files in `/AMP` on the SD card) before the IR, so a guitar (through a buffer) or a preamp can go straight in. It runs the A2-Lite network, levels each model by its loudness, and uses the capture's input calibration when it has one. Off by default; with a real amp's line out, leave it off.
- **Practice drum machine** (details in [DRUMS.md](DRUMS.md)):
  - **Songs:** five built-in 32-bar songs in the style of classic rock songs, and any General MIDI drum file (`.mid`) copied to `/DRUMS` on the SD card, with section looping.
  - Eight one-bar styles at 40–240 BPM, with sample-accurate timing: metronome, rock, three practice beats (hat only on the offbeat 8ths, offbeat 16ths, or triplets), pop, blues shuffle and 6/8 ballad.
  - Two kits:
    - a recorded acoustic kit, played from flash: our own kick, snare and closed hi-hat, with cymbals, rim and toms from the AVL Drumkits "Red Zeppelin" (CC BY-SA 3.0).
    - a synthesized kit.
  - Crash accents on beat 1 of every 2, 4, 8 or 16 bars.
  - Played like a drummer: velocities that follow the groove (softer hats under the snare), recorded velocity layers, a little random variation, and stereo panning from the drummer's seat.
  - A count-in (2 bars by default), and a speed trainer that raises the tempo every few bars up to a target, with a count-in at each new tempo.
  - A plucked-string bass playing along: the songs' bass lines, the bass part of MIDI files, and a line for each style in a key you choose.
  - Mixed into the outputs in stereo after the effects, with their own volumes, so the drums and bass never go through your guitar effects.
- **Remembers settings:** the last IR and amp model, your levels and the amp, delay, reverb and drum settings are stored in EEPROM.
- **Controls:** a 0.96" OLED with a rotary encoder and two keys (every setting, the IR and amp model lists, a level meter and a status page; see [DISPLAY.md](DISPLAY.md)), and USB serial commands. Both work at the same time.

## Hardware

| Connection | Shield pad / pin | Notes |
|---|---|---|
| Amp line out signal | **LINE IN L** | Only the left input is processed. LINE IN is about 29 kΩ: fine for a line out, a preamp or a pedal, but passive pickups lose treble into it, so put a buffer (any buffered pedal) in front for the amp block. |
| Amp line out ground | **GND** next to the line-in pads | |
| To mixer / interface / power amp | **LINE OUT L** and **R** + GND | Stereo when the delay or reverb is on; both carry the same signal when they are off. For a mono rig use L. If the gear you feed is not AC-coupled, add a 2.2–10 µF capacitor in series. |
| Headphones | 3.5 mm jack | **Headphones only.** The jack's ground is a *virtual* ground, so never connect it to another device's ground. |
| OLED + encoder module (128x64, SH1106 1.3" or SSD1306 0.96") | VCC **3.3 V**, GND, SDA **17**, SCL **16** (its own bus, `Wire1`); encoder A **2**, B **3**, push **4**; CONFIRM **5**; BACK **9** | Never 5 V. Wiring, controls and screens: [DISPLAY.md](DISPLAY.md). Pins, I2C address, the controller (`OLED_SH1106`) and encoder direction are in `include/config.h`. |

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
   The build also compiles the drum kits in `kits/` into the firmware (a pre-build step that needs only Python, which PlatformIO already has).
4. Open the serial monitor:
   ```
   pio device monitor
   ```

PlatformIO is used instead of the Arduino IDE because the block size is set with a build flag (`-D AUDIO_BLOCK_SAMPLES=32` in `platformio.ini`). The Arduino IDE would require editing Teensyduino's `AudioStream.h` instead.

## Serial commands

`?` prints them all with their current values; the settings' part of that list is generated from the settings table (`lib/oledui/src/ParamDefs.cpp`). A setting's command with no value prints it, flips it (a toggle) or steps to the next choice; `on` / `off` work for every toggle.

| Command | Action |
|---|---|
| `l` | List IR files (`>` marks the current one) |
| `3` | Load IR number 3 |
| `n` / `p` | Next / previous IR |
| `amp` | Toggle the amp block (NAM A2) on/off, click-free. All amp commands are explained in [NAM.md](NAM.md#serial-commands). |
| `amp list` / `amp <n\|name>` / `amp info` | List the models in `/AMP` / load one by number or part of its name / what the loaded one says about itself |
| `amp in -24..24` / `amp out -24..24` | Drive into the model / level after it, in dB |
| `amp cal` | Toggle the model's input calibration (on by default; only calibrated captures have one) |
| `b` | Toggle IR bypass (the signal before the IR, with a click-free fade). The delay and reverb are not affected. |
| `dl` | Toggle the delay on/off (the repeats ring out). All delay commands are explained in [DELAY.md](DELAY.md#controls). |
| `dmix 0..100` | Delay dry/wet blend (0 = dry only, 100 = repeats only) |
| `dtime 20..750` | Delay time in ms (the left time; the right is ¾ of it) |
| `dfb 0..100` | Delay repeats (feedback) |
| `dtone 0..100` | Brightness of the repeats (lower values make each repeat darker) |
| `dmod 0..100` | Delay modulation depth (chorus on the tail) |
| `dduck 0..100` | How far the repeats duck while you play (100 = −24 dB) |
| `ddrive 0..100` | Delay input preamp grit |
| `dera 0..100` | Delay converter era, 0 = 16-bit / 16 kHz, 100 = 10-bit / 6 kHz |
| `drum` | Start / stop the drum machine (count-in, then the groove). All drum commands are explained in [DRUMS.md](DRUMS.md#controls). |
| `bpm 40..240` | Drum tempo |
| `drum style [n\|name]` | List the styles / choose one (from the next bar while playing) |
| `drum kit [n\|name]` | List the kits / choose one (synth, rock) |
| `drum song [n\|name\|off]` | List the songs (built-in + `/DRUMS/*.mid`) / play one instead of the style |
| `drum loop <a> <b>` / `drum loop off` | Loop bars a–b of the song |
| `drum comp 0..100` | Drum-bus compression: 0 = natural, 100 = squashed |
| `drum crash 0\|2\|4\|8\|16` | Crash on beat 1 of every N bars, 0 = off (alone: the next) |
| `drum vol 0..100` | Drum volume |
| `drum count [0-4]` | Count-in bars (0 = off; alone: the next), also played at each trainer tempo step |
| `drum train [step every target]` | Toggle / set the speed trainer (+step BPM every `every` bars, up to `target`) |
| `train step 1..20` / `train every 1..16` / `train target 40..240` | Set one trainer setting, on or off |
| `bass [on\|off]` | The bass with the drums on/off (alone toggles) |
| `bass vol 0..100` / `bass tone 0..100` | Bass volume; brightness (0 = dark, 100 = bright) |
| `bass key C..B` | The styles' key (`E`, `F#`, `Bb` …); songs keep their own notes |
| `rv` | Toggle the reverb on/off (the tail rings out). All reverb commands are explained in [REVERB.md](REVERB.md#controls). |
| `mix 0..100` | Reverb dry/wet blend (0 = dry only, 100 = reverb only). Equal-power crossfade. |
| `size 0..100` | Reverb time (hall 0.8 to 8 s); gated: the burst's shape |
| `tone 0..100` | Reverb brightness (100 = brightest tail) |
| `rmode [hall\|gated]` | Reverb mode: hall or gated |
| `rpre 0..150` | Reverb pre-delay (ms) |
| `rmod 0..100` | Hall: chorus in the tail |
| `rera 0..100` | Reverb converter era (0 = 16-bit/16 kHz, 100 = 10-bit/6 kHz) |
| `rgate 100..600` | Gated: burst length (ms) |
| `g -6` | Output gain after the IR, in dB |
| `in 0..15` | Line-in sensitivity (0 = 3.12 V peak-to-peak, 5 = 1.33 V peak-to-peak) |
| `out 13..31` | Line-out level (13 = loudest, 3.16 V peak-to-peak) |
| `hp 0..100` | Headphone volume |
| `m` | Live input/output meter |
| `s` | Status: CPU load (including the amp's, delay's and reverb's shares), effect settings, peak levels, clip count, latency |
| `r` | Rescan the SD card (after swapping cards): IRs, amp models and drum songs |
| `?` | This list, with the current values |

## Latency and CPU

The block size and the sample rate (48 kHz) are set in `platformio.ini`. The 32-sample row was measured with `s` at 44.1 kHz (2026-10-29) and scaled to 48 kHz (+9 %); the others are scaled from it. Run `s` on the device to see real numbers.

| `AUDIO_BLOCK_SAMPLES` | I/O buffering | Convolver CPU, 4096-sample IR | During a 25 ms IR switch |
|---|---|---|---|
| 16 | ~0.7 ms | ~24 % | ~43 % |
| **32 (default)** | **~1.3 ms** | **~12 %** | ~22 % |
| 64 | ~2.7 ms | ~6.5 % | ~12 % |
| 128 | ~5.3 ms | ~4 % | ~6.5 % |

The codec's ADC and DAC add their own fixed delay on top of the buffering. To measure your exact total, loop LINE OUT back into LINE IN R and record both channels.

CPU cost scales with IR length. A 2048-sample cabinet IR uses about half the figures above.

The amp block adds 33 % CPU while it's on (see [NAM.md](NAM.md#m4-results); everything at once peaks at 85 %). The reverb adds about 8 % CPU (hall; gated about 6.5 %), the delay about 4.7 % and the drum machine about 5 % (recorded kits at 240 BPM, with the bass and the drum-bus compressor), measured at 48 kHz with 32-sample blocks, while each is on (or playing) or still ringing out. None of them uses any CPU once its tail has died away. Run `s` to see the current values. The delay's fixed voicing is set in `include/config.h`; see [DELAY.md](DELAY.md#fixed-voicing-configh).

Smaller blocks cost more CPU because the same IR is split into more partitions, and every partition is processed each block. At 16 samples there is still plenty of headroom, so try it if you want the lowest possible latency.

Memory use with 32-sample blocks at 48 kHz (2026-10-29):

- **RAM1 (fast DTCM/ITCM):** about 415 of 512 KB. This holds the real-time code (about 57 KB), the IR spectra and the input history used every block (about 100 KB), the amp network's history and weights (about 95 KB), and one delay line (about 141 KB). Cold code (SD card, printf, the UI) runs from flash; see [DISPLAY.md, RAM1 strategy](DISPLAY.md#ram1-strategy).
- **RAM2 (OCRAM/DMAMEM):** about 462 of 512 KB. This holds the reverb's lines (145 KB), the other delay line (about 141 KB), the bass, and the file lists and file-loading buffers.

About 96 KB stays free in RAM1 for the stack, and 51 KB in RAM2 for the heap.

## How it works

The convolution uses **uniformly partitioned overlap-save** (UPOLS):

1. The IR is split into `N = length / B` partitions of `B` samples each. Each partition is zero-padded to `2B` samples and stored as a spectrum.
2. Every audio block, the last `2B` input samples are FFT'd and pushed into a ring buffer of past input spectra (the frequency-domain delay line).
3. The output spectrum is the sum, over all partitions, of each past input spectrum multiplied by its matching IR partition spectrum.
4. One inverse FFT turns that sum into the output. Its second half is exactly the next `B` output samples, with no added delay.

When switching IRs, the new IR's spectra are computed in the background into a second buffer. For 25 ms both IRs are evaluated against the same input history and crossfaded. That is why switching causes no click and no tail cut-off.

The delay and how it works is described in [DELAY.md](DELAY.md), the reverb (hall and gated) in [REVERB.md](REVERB.md), and the drum machine in [DRUMS.md](DRUMS.md).

## Project layout

```
platformio.ini              build settings (block size here)
include/config.h            compile-time settings (IR length, pins, memory sizes, the delay's voicing)
tools/imxrt1062_cold.ld     linker script: which code stays in flash, out of RAM1
src/main.cpp                setup() and loop()
src/AudioGraph.{h,cpp}      audio objects and the codec
src/Params.{h,cpp}          reading and changing any setting by number (what each one does)
src/IRLibrary.{h,cpp}       SD card scanning and IR loading
src/AmpLibrary.{h,cpp}      the NAM amp models in /AMP: listing, loading, their gains
src/SdSource.h              an SD file as the loaders' byte source
src/DrumSongs.{h,cpp}       drum songs: the built-in ones and MIDI files in /DRUMS
src/Settings.{h,cpp}        the settings saved in EEPROM
src/SerialUI.{h,cpp}        serial commands and level meter
src/Controls.{h,cpp}        the panel's encoder (interrupt-driven) and keys
src/Oled.{h,cpp}            minimal SH1106 / SSD1306 driver on its own I2C bus (Wire1)
src/DisplayUI.{h,cpp}       the OLED panel: settings and model for lib/oledui, sending changed pages
src/AudioConvolverIR.h      Teensy Audio Library object (amp, IR, gain, bypass, delay, reverb, meters, CPU stats)
lib/irdsp/src/              portable DSP core (no Arduino dependencies)
  PartitionedConvolver.h    the UPOLS convolution engine
  StudioDelay.h             the studio 80s dual delay
  DrumMachine.{h,cpp}       the drum machine (synth and sample voices, sequencer, count-in, trainer, crash accents)
  DrumPatterns.cpp          the drum styles
  DrumSong.h, MidiDrums.{h,cpp}  drum songs, and reading them from MIDI files
  DrumKit.h                 the drum kit format (DrumKitData.cpp is generated from kits/)
  Reverb80.{h,cpp}          the 80s reverb (Lexicon-style hall, AMS-style gated)
  FxStage.h, FxSlot.h       per effect: on/off with trails, dry/wet mix, idle detection and clearing
  AmpStage.h                the amp block: on/off, model changes and gains around the network
  NamA2.h                   the Neural Amp Modeler A2-Lite network (NAM.md)
  NamFile.{h,cpp}           reading the A2-Lite network out of a .nam file
  irdsp_blocks.h            shared shelving filter, biquad filters, Hermite read, LFO and mix law
  irdsp_fft.h               small radix-2 FFT
  IRLoader.{h,cpp}          WAV parser, resampler, IR trimming and normalization
lib/oledui/src/             the panel UI, Arduino-free: the settings table (ParamDefs), canvas, fonts,
                            menu tree, screens, meters, keys
fonts/spleen/               the panel's fonts (Spleen, BSD-2-Clause; tools/make_font.py compiles them in)
kits/rock/                  the recorded drum kit (WAVs, kit.txt, credits, our originals)
songs/                      the built-in drum songs (text, compiled in)
tools/                      drum kit tools: import_kit.py (build a kit's WAVs), make_kits.py (compile kits in)
test/host/                  PC tests for the DSP core (nam/: the A2 test model and its reference output)
test/teensy/                benchmarks run on the Teensy (pio run -e nambench / nammicro)
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
- The delay: repeat timing and gains, stability, time changes, tails, clearing, ducking, drive and era (see [DELAY.md](DELAY.md#code-and-tests)).
- The effect stages: on/off, idle detection and clearing, and the reverb's mode switch.
- The drum machine: sample-accurate timing, count-in, speed trainer, style and tempo change points, the shuffle grid, stopping, every style's lanes and level, sampled-kit playback (onsets, velocity layers, fades, cymbal voices), crash accents, dynamics, humanize and panning, and the real kits' samples, layers and balance (see [DRUMS.md](DRUMS.md#code-and-tests)).
- The reverb: the hall's decay time, pre-delay, stereo, chorus, era and level, and the gated reverb's cut-off, burst shapes and level (see [REVERB.md](REVERB.md#code-and-tests)).
- WAV decoding for every supported format.
- Resampling accuracy from 22.05, 32, 44.1, 88.2, 96 and 192 kHz to 48 kHz.
- Anti-aliasing, trimming, truncation and normalization.
- The NAM amp network against a plain reference and against NeuralAmpModelerCore on a real A2 model, and reading `.nam` files (see [NAM.md](NAM.md#m2-and-m3-results)).

`test/host/namtool model.nam` reads a NAM model the way the Teensy will and prints what it found.

`test/host/irtool file.wav out.f32` runs the exact on-device loading pipeline on a file, which is useful for seeing how the Teensy will interpret an IR before copying it to the card.

## Ideas for next steps

- **Tone controls:** low-cut, high-cut and a presence biquad after the convolver.
- **Two-IR blending:** the engine already evaluates two IRs during crossfades.
- **USB audio:** build with `-D USB_MIDI_AUDIO_SERIAL` and add `AudioOutputUSB` to record directly into a computer (verify it with your chosen block size).
