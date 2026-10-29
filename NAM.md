# Amp block (NAM A2): plan

**Status (2026-10-29): M0 to M3 done.** The A2-Lite engine is built and measured on the Teensy (32.4 % of the CPU at 48 kHz, see [M0 results](#m0-results)), the firmware runs at 48 kHz ([M1 results](#m1-results)), the engine matches NeuralAmpModelerCore on a real A2 model, and `.nam` files are read ([M2 and M3 results](#m2-and-m3-results)). The firmware integration (Step 4) is still a plan. This is the research and the build plan for an amp block that plays [Neural Amp Modeler](https://www.neuralampmodeler.com/) **A2** profiles (`.nam` files) on the Teensy 4.0.

```
shield LINE IN (L) ──► AMP (NAM A2-Lite) ──► IR (cab) ──► delay ──► reverb ──► LINE OUT L/R + headphones
                       off by default                                           + drums and bass
```

- **Optional.** The amp block is off by default. With a tube amp's line out in LINE IN, as today, leave it off (or load a pedal capture). With a guitar (through a buffer, see [Input](#input-level-and-calibration)), switch it on and use the IR as the cab.
- **A2-Lite only.** An A2 file holds two networks, Full and Lite. Only Lite fits the Teensy. Older A1 files are listed but refused with a clear message.
- **The whole system moves to 48 kHz** first. NAM models are trained at 48 kHz, and played at 44.1 kHz they would sound about 9 % off.
- **Just copy `.nam` files** to `/AMP` on the SD card, as with the IRs. The firmware reads them directly; no converter on a computer.

## Contents

- [The A2 format](#the-a2-format)
- [Why Lite, and what it costs](#why-lite-and-what-it-costs)
- [Step 1: the whole system at 48 kHz](#step-1-the-whole-system-at-48-khz)
- [Step 2: the A2-Lite engine](#step-2-the-a2-lite-engine)
- [Step 3: reading .nam files](#step-3-reading-nam-files)
- [Step 4: the firmware](#step-4-the-firmware)
- [Input level and calibration](#input-level-and-calibration)
- [CPU and memory budget](#cpu-and-memory-budget)
- [Milestones](#milestones)
- [Tests](#tests)
- [Open questions](#open-questions)
- [Sources](#sources)

## The A2 format

A2 is NAM's second-generation architecture, released in 2026 by TONE3000 with NAM's author, Steven Atkinson. It replaces the WaveNet family now called A1 (standard, lite, feather, nano). The authoritative implementation is NeuralAmpModelerCore (v0.5.2 or later), in `NAM/wavenet/a2_fast.{h,cpp}` and `NAM/container.cpp`.

### The file

A `.nam` file is JSON:

| Key | What it holds |
|---|---|
| `version` | The file format version, `0.7.0` for A2 containers. |
| `architecture` | `"SlimmableContainer"` for A2 (a plain model says `"WaveNet"`, `"LSTM"` …). |
| `config.submodels` | A2 only: a list of `{ "max_value": …, "model": {…} }`, sorted by ascending `max_value`. Each `model` is a complete NAM model with its own `architecture`, `config` and `weights`. |
| `weights` | A flat list of floats (empty for a container). |
| `sample_rate` | The model's rate. 48000 when absent. |
| `metadata` | `name`, `modeled_by`, `gear_make`, `gear_model`, `gear_type`, `tone_type`, `loudness`, `gain`, `input_level_dbu`, `output_level_dbu`, `training` … |

An A2 container holds **A2-Lite** (`max_value` 0.5, 3 channels) and **A2-Full** (`max_value` 1.0, 8 channels). Each has its own weights: Lite is not a slice of Full. A player picks one by its CPU budget.

The metadata the amp block uses:
- `loudness`: the model's loudness on a standard input, in dB. Players level models with `gain_dB = -18 - loudness`.
- `input_level_dbu`: the input level, in dBu, of a 1 kHz sine at 0 dBFS peak when the capture was made (calibrated captures only).
- `sample_rate`, `name`, `gear_make` / `gear_model`, `gear_type`, `tone_type`: checks and display.

### The network

A2 has one fixed shape. NeuralAmpModelerCore's fast path accepts a model only if every setting matches (`is_a2_shape()`), and so will we:

| | A2 |
|---|---|
| Layers | 23, in one layer array |
| Kernel sizes | 6 × 14, then 15, 15, then 6 × 7 |
| Dilations | 1 3 7 17 41 101 239, twice; then 1 13 (the "degridding" pair); then 1 3 7 17 41 101 239 |
| Channels | 3 (Lite) or 8 (Full); `bottleneck` = `channels` |
| Activation | LeakyReLU, slope 0.01, no gating |
| Input mixin | The raw input sample into every layer (`condition_size` 1) |
| Per layer | A 1x1 convolution back to the channels (`layer1x1`), with bias; no `head1x1`, no FiLM |
| Head | A convolution, channels → 1, 16 taps, with bias, then a scale (the last weight) |
| Receptive field | 6,331 samples, plus 15 for the head: 132 ms at 48 kHz |

For each sample, with `x` the layer's input (C channels) and `in` the input sample:

```
rechannel:   x = r · in                                  (1 → C)
each layer:  z = b + Σk Wk · x(t − (K−1−k)·D) + m · in    (C → C, K taps, dilation D)
             a = LeakyReLU(z)
             h += a                                      (the head's input, summed over layers)
             x = x + L · a + l                           (1x1, residual)
head:        y = (hb + Σk Hk · h(t − 15 + k)) · scale     (C → 1, 16 taps)
```

The weights come in one flat list, in this order, and must be used up exactly:

1. The rechannel `r` (C values).
2. Per layer: the conv weights `W` [out][in][tap], its bias `b` (C), the mixin `m` (C), the 1x1 `L` [out][in], its bias `l` (C).
3. The head `H` [channel][tap], its bias `hb`, then `scale`.

For Lite that is **1,871 floats** (7.5 KB): 72 per 6-tap layer, 153 per 15-tap layer, 49 for the head, 3 for the rechannel and 1 for the scale.

## Why Lite, and what it costs

| | A2-Lite | A2-Full |
|---|---|---|
| Multiply-adds per sample | about 1,730 | about 12,000 |
| At 48 kHz | 83 M/s | 565 M/s |
| Weights | 7.5 KB | 48 KB |
| History (the dilated taps' memory) | 74 KB (87 KB as ring buffers) | 198 KB |

- **Full is out of reach.** 565 M multiply-adds a second is about a whole 600 MHz Cortex-M7 at one cycle each, before anything else runs.
- **Lite fits.** TONE3000 quotes it at "50 % CPU on an ARM Cortex M7 600MHz" (the Teensy 4's chip), with general code. Our engine is specialized for this one shape and processes a block at a time, so the goal was **about 20 %** (1.5 cycles per multiply-add). [M0](#m0-results) measured **32.4 %** (2.34 cycles): the M7 doesn't overlap the float loads with the arithmetic, so moving the data costs as much as the math.
- **LeakyReLU is cheap** (a compare and a multiply), unlike A1's tanh.

## Step 1: the whole system at 48 kHz

**Done** (its own commit, before any NAM code). Everything still sounds the same, at 8.5 % more CPU.

**What changes**

- **The rate:** `-D AUDIO_SAMPLE_RATE_EXACT=48000.0f` in `platformio.ini`. The core's `AudioStream.h` only sets 44.1 kHz `#ifndef`, and the Teensy 4 I2S output (`output_i2s.cpp`) sets its PLL from this value.
- **The codec:** `AudioControlSGTL5000::enable()` writes `CHIP_CLK_CTRL = 0x0004` (44.1 kHz). `Codec::setRate()` (`src/AudioGraph.h`, a small subclass, because `write()` is protected) writes `0x0008` (48 kHz, 256·Fs) after it.
- **The reverb:** `Reverb80.h` sizes its memory in samples for 44.1 kHz (`kPreMax` 6620 = 150 ms, `kGateMax` 26464 = 600 ms, the hall lines' lengths). Now sized for 48 kHz (`kPreMax` 7205, `kGateMax` 28804), and every length (hall lines, diffusers, output taps) scaled by 48/44.1 with primes kept prime, so the times and the sound stay the same. RAM2 +12 KB.
- **The drum kit:** `tools/make_kits.py` requires 44.1 kHz WAVs (`FS = 44100`). Now `FS = 48000`. `tools/import_kit.py` resamples sources at other rates (Kaiser-windowed sinc, pure Python), and the rock kit was rebuilt from its 44.1 kHz sources and committed. `pio_kits.py` reads the build's rate from `build_flags` and passes it to `make_kits.py`, which stops the build if the kits don't match. Flash +53 KB (666 KB). Levels: the same within 0.1 dB, except the open hat (−0.7 dB RMS) and the ride (+0.8 dB): they are levelled by their sample peak, which lands a little differently on a 48 kHz grid.
- **The defaults:** drop the `44100.0f` defaults in the irdsp constructors and `IRPrepOptions`, so nothing can quietly run at the wrong rate.
- **Already rate-aware:** the IR loader (it resamples to `AUDIO_SAMPLE_RATE_EXACT`), the delay (its line length in `AudioGraph.cpp`, +12 KB in each of RAM1 and RAM2), `FxSlot`, the drum machine, the bass and the CPU figures.

**Gains:** the buffering latency drops from 1.45 to 1.33 ms (2 × 32 samples), and IRs and NAM models captured at 48 kHz play without resampling.

### M1 results

Measured on 2026-10-29:

- **Host tests:** all pass at 48 kHz. The tests' sample positions are now written in terms of the rate (`kS`, `kBeat`, `kBar` …), and the loader test targets 48 kHz with 44.1 kHz as one of the resampled source rates.
- **The real rate:** silent drums at 100 BPM for 48 s of wall clock reached bar 21 (20 bars of 2.4 s). At 44.1 kHz they would have reached bar 19.
- **CPU, IR and reverb:** 23.0 % average and 25.5 % max, against 21.2 % and 23.3 % before: +8.5 %.
- **CPU, worst case:** drums and bass at 240 BPM with crashes, the delay, the reverb and four IR switches: 32.5 % average, **53.6 % max**.
- **Memory:** 195 KB of RAM1 left for the stack (was 207), 64 KB of RAM2 (was 88). ITCM unchanged.
- **IRs:** 44.1 kHz files are resampled on load; a 4096-sample IR is now 85 ms instead of 93.

## Step 2: the A2-Lite engine

`lib/irdsp/src/NamA2.h` (built): portable C++ with no Arduino, like the rest of `irdsp`, tested on the PC by `test/host/test_nam.cpp`.

```cpp
template <int B> class NamA2 {
 public:
  static constexpr int kHistoryFloats;     // 22,176 at B = 32 (87 KB)
  void begin(float* history);              // kHistoryFloats, in DTCM
  void setWeights(const float* w);         // a .nam file's 1,871 Lite weights, in file order
  void reset();                            // clears the history
  void process(const float* in, float* out);  // one block of B samples
};
```

**How it runs**

- **A layer at a time over the whole block**, not a sample at a time through all layers. Its taps are read as runs of consecutive frames.
- **Two taps per pass** over the block: their 18 weights stay in registers, and the running sums `z` are loaded and stored once per pair. The first pass starts from the bias and the mixin.
- **One more pass** does LeakyReLU, the head sum and the 1x1 residual (18 constants in registers). It writes the next layer's input straight into that layer's history, so nothing is copied between layers.
- **Fusing more into one pass is slower:** with more than about 20 constants they no longer fit in the 32 float registers and are reloaded every frame (measured: 41 % instead of 32 %).
- **The history:** a ring buffer per layer, channel-interleaved, as long as the taps reach back plus a block, rounded up to whole blocks. A block is written without wrapping; a tap's run that crosses the end is read in two pieces. So every block costs the same (no copy-back spikes), and the history is 87 KB.
- **The head** keeps its own 15-frame history of the layer sum and runs two taps per pass.

**Warm-up:** a fresh or cleared network needs its receptive field (132 ms) of input before its output is right. The firmware handles this by keeping the output muted that long after a load ([Step 4](#step-4-the-firmware)).

### M0 results

Measured on 2026-10-29 on the Teensy 4.0 at 600 MHz, 32-sample blocks, random weights (`pio run -e nambench -t upload`; the loop shapes in `test/teensy/nam_micro.cpp`, `-e nammicro`):

| Version | Cycles per block | Cycles per multiply-add | CPU at 48 kHz | Max |
|---|---|---|---|---|
| First: one tap per pass, separate activation pass, copied histories | 175,000 | 3.16 | 43.8 % | 49 % |
| Everything after the taps fused into the last pass | 164,700 | 2.97 | 41.2 % | 47 % |
| Two taps per pass, lean activation pass, copy-back histories | 130,200 | 2.35 | 32.6 % | 38 % |
| **Ring buffers (now)** | **129,400** | **2.34** | **32.4 %** | **32.8 %** |
| The same with the history in OCRAM | 144,400 | 2.61 | 36.1 % | 37.5 % |

What the micro-tests showed about the Cortex-M7:
- A float multiply-add has a 3-cycle latency and issues every cycle: three independent sums are enough (1.02 cycles each), one alone costs 3.
- Float loads and stores don't overlap with the arithmetic in these loops: each costs about a cycle. So the floor of this design is about one cycle per float moved plus one per multiply-add.
- Hand-written assembly with VLDM/VSTM (three floats and the pointer step in one instruction) makes the tap passes 13 % faster (1.78 instead of 2.04 cycles per multiply-add). For the whole engine that would be about 29 %.

**Verdict:** 32.4 % misses the 30 % mark, but the per-block maximum is now steady (32.8 %), and the newer measurements of the rest of the firmware are lower than the plan assumed (see [CPU and memory budget](#cpu-and-memory-budget)). The ways down: assembly for the tap passes (about −3 %), 720 MHz (−17 % of everything), and the IR capped at 2048 samples while the amp is on (about −6 %).

## Step 3: reading .nam files

`lib/irdsp/src/NamFile.{h,cpp}` (built): portable, reading through the same `irdsp::ByteSource` (read and seek) as the WAV loader. `readNamA2Lite(src, weights, &info, &err)` fills the 1,871 weights and a `NamInfo` (name, gear, tone type, sample rate, loudness, input and output level).

An A2 file is about 670 KB of JSON (NeuralAmpModelerCore's example), most of it Full's weights. There's no room for a parsed tree, so:

- **A pull reader** over a 256-byte buffer, with the file offset of every character, so any value can be read again later. Objects and arrays it doesn't need are skipped character by character, their numbers never converted.
- **Pass 1** reads the top level (`architecture`, `sample_rate`, the `metadata` fields used) and notes the file offset of each submodel's `model`, then of each model's `config` and `weights`. JSON doesn't fix the order of keys, so the offsets are collected first.
- **Pass 2** seeks to the Lite submodel's `config` and checks every setting of [the A2 shape](#the-network). It then seeks to its `weights` and reads exactly 1,871 numbers, with `strtod` on each token (it's in flash, and this is background code). Full's weights are skipped without converting them.
- **The network's own metadata wins:** each network in an A2 file has its own `loudness` (Lite −24.0 dB and Full −26.4 dB in the example); the top-level one belongs to Full. `input_level_dbu` is only at the top level.
- **Also accepted:** a plain `"WaveNet"` file in the Lite shape (a Lite model exported on its own).
- **Errors say why**, in the serial log and on the panel:

| Case | Message |
|---|---|
| An A1 WaveNet, an LSTM, or any other setting than A2's (gating, FiLM, kernel sizes …) | `not an A2 model: download the A2 version` |
| A2-Full only (a container or a plain WaveNet) | `no A2-Lite network in this file` |
| Wrong number of weights | `damaged file (wrong number of weights)` |
| Bad JSON, a cut-off file | `damaged file (not valid JSON)` |
| `sample_rate` not 48000 | The firmware's job (Step 4): load it, with a warning: `model is 44100 Hz: pitch shifted` |

`test/host/namtool file.nam` runs the device's parser on a computer and prints what the Teensy would see: the metadata, the submodel chosen and the weight count. Like `irtool`, it's for checking a file before copying it to the card.

### M2 and M3 results

- **Against NeuralAmpModelerCore** (the official implementation, commit 0b3d3c9, built from source): its example A2 model's Lite network, read with `NamFile.h`, on a 1 s test signal (plucked notes, a sweep, noise bursts up to 0.8). `NamA2` matches Core's output within **4.8e-7**, 130 dB below the signal (output peak 0.36), against both Core's A2 fast path and its generic WaveNet code. The first 6,346 samples differ only because Core warms the network up on reset; the firmware mutes that time anyway.
- **The test files:** `test/host/nam/A2.nam` (MIT, from Core) and `A2_lite_ref.f32` (Core's output), made again by `test/host/nam/make_reference.sh`.
- **The reader:** the real file, made-up files with their keys in every order, all the files it must refuse, 400 cut-off files and 300 randomly damaged ones pass (`test_namfile`), also under AddressSanitizer and UBSan.
- **Still to do on the device:** load time from the SD card (two passes over 670 KB), and a real capture: a boosted JCM800 2203 (scottcorgan's "JCM 800 2203 boosted and unboosted" on TONE3000). Its license (T3K) allows using it but not redistributing it, so it stays out of git.

## Step 4: the firmware

**Files**

| File | Contents |
|---|---|
| `src/AmpLibrary.{h,cpp}` | Scans `/AMP/*.nam` (sorted, hidden files skipped, up to 64; the names in DMAMEM), loads a model into a staging `NamA2Weights` in DMAMEM, remembers it in the settings. Like `IRLibrary`. |
| `src/AudioConvolverIR.h` | The amp stage, before the convolver, and its CPU share. |
| `src/Params.cpp`, `lib/oledui/src/ParamDefs.cpp` | The amp's settings. |
| `src/Settings.{h,cpp}` | The model's path in the EEPROM record. |
| `src/SerialUI.cpp` | The `amp` commands. |
| `lib/oledui/src/Menu.cpp`, `Screens.cpp` | The Amp page and the AMP block. |
| `tools/imxrt1062_cold.ld` | `AmpLibrary` and `NamFile` on the cold list. |

**The amp stage** in `AudioConvolverIR::update()`:

```
in → × input gain → NamA2 → × level (−18 − loudness) → × output level → on/off blend → convolver
```

- **On/off** is an `FxSlot`-style 20 ms blend with the dry input. When it's off and the blend has reached the dry signal, the network isn't run (no CPU), and its history is cleared.
- **Changing the model:**
  1. Fade the amp's output down (20 ms).
  2. The audio side copies the staged weights in (7.5 KB) and clears the history.
  3. Run the network muted for 132 ms (the warm-up).
  4. Fade up (20 ms).
  
  That's a 170 ms dip in the sound. Two networks for a crossfade, as the IRs have, would need a second 87 KB history, which won't fit.
- **`s`** gets an `Amp:` line (the model, on/off, the levels) and the amp's CPU share.

**Settings** (new rows at the end of the saved block in `ParamDefs.cpp`, after `kBassTone`)

| Command | Range | Default | What it does |
|---|---|---|---|
| `amp` | toggle | off | The amp block on/off. |
| `amp in` | −24 … +24 dB, 0.5 | 0 | Input trim: drives the model harder or softer. |
| `amp out` | −24 … +24 dB, 0.5 | 0 | Output level after the model's own levelling. |
| `amp cal` | toggle | on | Use the file's `input_level_dbu` ([below](#input-level-and-calibration)). |

**Other commands:** `amp list`, `amp <n|name>` (load), `amp info` (the name, gear, type, `loudness`, `input_level_dbu`, sample rate and the submodel used), and `r` rescans `/AMP` too.

**EEPROM:** the record gets an `ampPath` after `irPath`, with a new magic. The current `IRLP` record is read once and carried over, as `fromLegacy()` does for the one before it.

**Panel**

- The home blocks become **AMP › IR › DLY › REV › DRM**. Push opens the model list, hold switches the amp on/off.
- An **Amp** page in the menu: On, Model (the list; push loads, as in the IR list), In, Out, Cal.
- Renders for `screenshots/` from `render_ui`, and DISPLAY.md's menu tree.

## Input level and calibration

**The hardware stays as it is.** The shield's LINE IN is about 29 kΩ. A tube amp's line out, a preamp or a pedal drives that fine. Passive guitar pickups don't: they lose treble into anything much below 1 MΩ. To plug a guitar in, put a buffer in front (any buffered pedal, or a JFET or op-amp buffer with a 1 MΩ input). The README's hardware section will say so.

**Calibration:** a NAM model only reacts like the real amp when the input level matches the capture's. Calibrated captures store `input_level_dbu`: the dBu of a sine that reached 0 dBFS when the amp was captured.

- The shield's full scale is known for each `in` step from the SGTL5000 datasheet: `in 0` is 3.12 V p-p, which is +3.1 dBu, down to 0.24 V p-p at `in 15`.
- With `amp cal` on, the input gain into the model is (the shield's full-scale dBu − the model's `input_level_dbu`) + `amp in`.
- Without the field (an uncalibrated capture), it is `amp in` alone.
- The Meter screen and `m` still show the shield's input, so the level procedure in the README is unchanged.

**Output:** `−18 − loudness` dB evens out the models' levels, as the NAM plugin does, then `amp out`. The IR's own normalization follows.

## CPU and memory budget

**CPU** (32-sample blocks; measured: the amp in M0, the 48 kHz column in M1)

| | Today (44.1 kHz) | At 48 kHz | + amp |
|---|---|---|---|
| Worst case: drums and bass at 240 BPM with crashes, delay, reverb, IR switches | 41–47 % | **53.6 %** (32.5 % average) | **86 %** (65 % average) |
| Typical playing: IR, delay and reverb | about 25 % | about 28 % | about 61 % |

The amp is counted at its measured 32.4 % average, 32.8 % max.

The worst case is close to the limit. Ways to buy room, in order:
1. **Cap the IR at 2048 samples while the amp is on.** A cab IR has nothing audible after 43 ms. That saves about 6 %.
2. **64-sample blocks:** the convolver drops from 12 to about 6.5 %, for 1.3 ms more latency.
3. **720 MHz** (`board_build.f_cpu`): 20 % more of everything. The Teensy 4 runs it without cooling.

**Memory**

| | Free today | After 48 kHz | After the amp |
|---|---|---|---|
| RAM1 (stack) | 205 KB | 195 KB | about 100 KB |
| RAM2 (heap) | 88 KB | 64 KB | about 50 KB |
| ITCM | 13 KB left in 2 banks | same | must fit in it |

- **RAM1** takes the history (87 KB, measured), the weights (7.5 KB) and the per-layer pointers.
- **ITCM** has 13 KB left in its two 32 KB banks (52.5 KB used); the engine's code is about 4 KB. If the engine's code doesn't fit, a third bank takes 32 KB from the stack. Then the fallback is to move the three dilation-239 layers' history (43 KB) to OCRAM, or shorten `DELAY_MAX_MS` from 750 to 600 ms (29 KB from each RAM).
- **RAM2** takes the file list (6 KB) and the staging weights (7.5 KB).
- **Flash:** about 20 KB of code and the 55 KB the kit grows.

## Milestones

| | What | Done when |
|---|---|---|
| **M0** | Benchmark the engine alone on the Teensy: random A2-Lite weights, the cycle counter, a sketch on its own. | **Done:** 32.4 % at 48 kHz ([results](#m0-results)). |
| **M1** | [Step 1](#step-1-the-whole-system-at-48-khz): the whole system at 48 kHz. | **Done** ([results](#m1-results)). |
| **M2** | [Step 2](#step-2-the-a2-lite-engine): the engine. | **Done** ([results](#m2-and-m3-results)). |
| **M3** | [Step 3](#step-3-reading-nam-files): the loader and `namtool`. | **Done** on the PC ([results](#m2-and-m3-results)); a TONE3000 capture and the load time on the device come with M4. |
| **M4** | [Step 4](#step-4-the-firmware): serial and settings. | Load, switch, on/off and `s` on the device, no dropouts. |
| **M5** | The panel. | Home block, Amp page, renders. |
| **M6** | Docs. | README (features, hardware, commands, budgets), DISPLAY.md (menu tree, RAM1 table), this file rewritten from a plan into a description. |

## Tests

On the PC (`make -C test/host test`):

- **`test_nam`, the engine:**
  - Against a plain reference written straight from the formulas above, one sample at a time: within 1e-5, on random weights.
  - The same output for every block size from 16 to 128, and for odd chunk lengths.
  - The history copy-back: no step in the output when it happens.
  - 60 s of loud noise stays finite; `reset()` gives exact silence from silence plus the biases' steady output.
- **The golden test** (in `test_nam`): NeuralAmpModelerCore's A2 example model, run once through Core by `nam/make_reference.sh` on a test signal (plucked notes, a sweep, noise bursts). Its Lite output is committed; ours must match within 1e-5 and 100 dB (it does: 4.8e-7, 130 dB).
- **`test_namfile`, the loader:**
  - The keys in any order; the Lite submodel picked from either position.
  - A plain Lite WaveNet accepted; A1, LSTM and Full-only files refused with the right message.
  - A wrong weight count, a cut-off file and 200 randomly damaged files: an error, never a crash or an overrun.

On the Teensy:

- `s` with everything on at once: the amp, drums and bass at 240 BPM, the delay, the reverb and IR switches. No dropouts and no clipped samples.
- Switching models: the dip, and no click.
- `amp` off: the output is the dry signal, and the amp's CPU share drops to 0.
- A model compared with the NAM plugin on a computer, same input file, by ear and on a spectrum.

## Open questions

- **A noise gate** in front of the amp. High-gain models amplify the shield's noise floor, and most NAM players have one.
- **A tone stack or EQ** after the amp (bass, mid, treble, presence). It overlaps with the README's "tone controls" idea.
- **Home screen row 0:** the IR's name, the model's, or the model's only while the amp is on.
- **A1 models:** an A1-nano engine (tanh, 4 channels) is possible later, if it's asked for.

## Sources

- [Introducing NAM Architecture 2 (A2)](https://www.tone3000.com/blog/introducing-neural-amp-modeler-nam-architecture-2-a2), [Announcing A2](https://www.tone3000.com/blog/a2-next-generation-neural-amp-modeler) and [NAM A2: the complete guide](https://www.tone3000.com/guides/nam-a2-the-complete-guide) (TONE3000): the architecture, Full and Lite, CPU figures, developer notes.
- [Architecture "A2"](https://www.neuralampmodeler.com/post/architecture-a2) (the NAM blog): goals and naming.
- [NAM A2 Architecture](https://namforum.com/wiki/index.php/NAM_A2_Architecture) (NAMFORUM wiki): release, minimum versions.
- [.nam file specification](https://neural-amp-modeler.readthedocs.io/en/latest/model-file.html): the top-level keys and the metadata.
- [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore): `NAM/wavenet/a2_fast.h` and `a2_fast.cpp` (the shape, the weight order, the computation), `NAM/container.cpp` (`SlimmableContainer`).
- [tone-3000/nam-pedal](https://github.com/tone-3000/nam-pedal): NAM on a Daisy (Cortex-M7, 48 kHz, 48-sample blocks) and the `.namb` binary format.
