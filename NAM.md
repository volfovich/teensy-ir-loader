# Amp block (NAM A2): plan

**Status: plan, nothing built yet (2026-10-29).** This is the research and the build plan for an amp block that plays [Neural Amp Modeler](https://www.neuralampmodeler.com/) **A2** profiles (`.nam` files) on the Teensy 4.0.

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
| History (the dilated taps' memory) | 74 KB | 198 KB |

- **Full is out of reach.** 565 M multiply-adds a second is about a whole 600 MHz Cortex-M7 at one cycle each, before anything else runs.
- **Lite fits.** TONE3000 quotes it at "50 % CPU on an ARM Cortex M7 600MHz" (the Teensy 4's chip), with general code. Our engine is specialized for this one shape and processes a block at a time, so the goal is **about 20 %** (1.5 cycles per multiply-add). [Milestone M0](#milestones) measures that before anything else is built.
- **LeakyReLU is cheap** (a compare and a multiply), unlike A1's tanh.

## Step 1: the whole system at 48 kHz

Its own commit, before any NAM code. Everything still sounds the same afterwards, at about 9 % more CPU.

**What changes**

- **The rate:** `-D AUDIO_SAMPLE_RATE_EXACT=48000.0f` in `platformio.ini`. The core's `AudioStream.h` only sets 44.1 kHz `#ifndef`, and the Teensy 4 I2S output (`output_i2s.cpp`) sets its PLL from this value.
- **The codec:** `AudioControlSGTL5000::enable()` writes `CHIP_CLK_CTRL = 0x0004` (44.1 kHz). Write `0x0008` (48 kHz, 256·Fs) after it. `write()` is protected, so through a small subclass in `AudioGraph.cpp`.
- **The reverb:** `Reverb80.h` sizes its memory in samples for 44.1 kHz (`kPreMax` 6620 = 150 ms, `kGateMax` 26464 = 600 ms, the hall lines' lengths). Size them for 48 kHz and scale the hall lines by 48/44.1, so the times and the sound stay the same. RAM2 +12 KB.
- **The drum kit:** `tools/make_kits.py` requires 44.1 kHz WAVs (`FS = 44100`). Convert `kits/rock/*.wav` to 48 kHz once (`tools/import_kit.py` gains a rate option; band-limited sinc with numpy) and commit them. `pio_kits.py` passes the build's rate to `make_kits.py`, which checks the WAVs against it. Flash +55 KB.
- **The defaults:** drop the `44100.0f` defaults in the irdsp constructors and `IRPrepOptions`, so nothing can quietly run at the wrong rate.
- **Already rate-aware:** the IR loader (it resamples to `AUDIO_SAMPLE_RATE_EXACT`), the delay (its line length in `AudioGraph.cpp`, +12 KB in each of RAM1 and RAM2), `FxSlot`, the drum machine, the bass and the CPU figures.

**Gains:** the buffering latency drops from 1.45 to 1.33 ms (2 × 32 samples), and IRs and NAM models captured at 48 kHz play without resampling.

**Checks:**
- The host tests pass at 48 kHz (the Makefile gets a rate variable).
- On the device: `bass key A` plays an A on a tuner, the metronome's tempo is right against a phone metronome, and `dtime` repeats are on time.
- `s` CPU: about 9 % more than before.

## Step 2: the A2-Lite engine

`lib/irdsp/src/NamA2.{h,cpp}`: portable C++ with no Arduino, like the rest of `irdsp`, and tested on the PC.

```cpp
struct NamA2Weights { /* fixed-size arrays for C = 3, filled by the loader */ };

class NamA2 {
 public:
  void begin(float* history);                // kHistoryFloats, in DTCM
  void setWeights(const NamA2Weights& w);    // the audio side copies them in
  void reset();                              // clears the history
  void process(const float* in, float* out, int n);  // n = the block size
};
```

**How it runs**

- **A layer at a time over the whole block**, not a sample at a time through all layers. A layer's weights stay in registers for all 32 frames, and its taps are read as runs of consecutive frames.
- **Per output channel**, the 18 conv weights of a 6-tap layer (45 for the 15-tap pair) are loaded once into registers. Then the mixin, LeakyReLU, the head sum and the 1x1 residual are done in the same pass.
- **Templates** on the channel count and the kernel size, fully unrolled. C = 3 is too narrow for any SIMD, so the speed comes from keeping the FPU fed: dual-issued loads from DTCM next to single-cycle multiply-adds.
- **The history:** one buffer per layer, channel-interleaved, holding the (K−1)·D frames the taps reach back plus some slack. When the write position reaches the end, the last (K−1)·D frames are copied back to the start (NeuralAmpModelerCore's `RingBuffer` does the same). The inner loops never wrap or use a modulo. The slack is tuned on the device: more slack means fewer copies.
- **The head** keeps its own 15-frame history of the layer sum.
- **Placement:** the history and the weights in DTCM, the real-time code in ITCM. Setup and clearing are `IRDSP_COLD` (flash). Denormals are already flushed to zero on the Teensy.

**Warm-up:** a fresh or cleared network needs its receptive field (132 ms) of input before its output is right. The firmware handles this by keeping the output muted that long after a load ([Step 4](#step-4-the-firmware)).

## Step 3: reading .nam files

`lib/irdsp/src/NamFile.{h,cpp}`: portable, reading through the same `irdsp::ByteSource` (read and seek) as the WAV loader.

An A2 file is about 150–300 KB of JSON, most of it Full's weights. There's no room for a parsed tree, so:

- **A streaming tokenizer** that keeps track of the current key path (`config.submodels[0].model.weights`) and never stores more than one token.
- **Pass 1** reads the top level (`version`, `architecture`, `sample_rate`, the `metadata` fields used) and notes the file offset of each submodel's `max_value`, `config` and `weights`. JSON doesn't fix the order of keys, so the offsets are collected first.
- **Pass 2** seeks to the Lite submodel's `config` and checks every setting of [the A2 shape](#the-network). It then seeks to its `weights` and reads exactly 1,871 numbers, with `strtod` on each token (it's in flash, and this is background code). Full's weights are skipped without converting them.
- **Also accepted:** a plain `"WaveNet"` file in the Lite shape (a Lite model exported on its own).
- **Errors say why**, in the serial log and on the panel:

| Case | Message |
|---|---|
| An A1 WaveNet or an LSTM | `A1 model: download the A2 version` |
| A container without a 3-channel A2 submodel | `no A2-Lite network in this file` |
| `sample_rate` not 48000 | Load it, with a warning: `model is 44100 Hz: pitch shifted` |
| Wrong number of weights, bad JSON, a cut-off file | `damaged file (<detail>)` |

`test/host/namtool file.nam` runs the device's parser on a computer and prints what the Teensy would see: the metadata, the submodel chosen and the weight count. Like `irtool`, it's for checking a file before copying it to the card.

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
  
  That's a 170 ms dip in the sound. Two networks for a crossfade, as the IRs have, would need a second 74 KB history, which won't fit.
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

**CPU** (32-sample blocks; estimates until M0 and M1 measure them)

| | Today (44.1 kHz) | At 48 kHz | + amp |
|---|---|---|---|
| Worst case: drums, bass, reverb, delay, an IR switch | 55–58 % | 60–63 % | **80–93 %** |
| Typical playing | about 30 % | about 33 % | 53–63 % |

The worst case is close to the limit. Ways to buy room, in order:
1. **Cap the IR at 2048 samples while the amp is on.** A cab IR has nothing audible after 43 ms. That saves about 6 %.
2. **64-sample blocks:** the convolver drops from 12 to about 6.5 %, for 1.3 ms more latency.
3. **720 MHz** (`board_build.f_cpu`): 20 % more of everything. The Teensy 4 runs it without cooling.

**Memory**

| | Free today | After 48 kHz | After the amp |
|---|---|---|---|
| RAM1 (stack) | 205 KB | about 193 KB | about 85 KB |
| RAM2 (heap) | 88 KB | about 64 KB | about 50 KB |
| ITCM | 4 KB left in 2 banks | same | must fit in it |

- **RAM1** takes the history (74 KB plus slack, about 100 KB in total), the weights (7.5 KB) and the per-layer pointers.
- **ITCM** has about 4 KB left in its two 32 KB banks. If the engine's code doesn't fit, a third bank takes 32 KB from the stack. Then the fallback is to move the three dilation-239 layers' history (43 KB) to OCRAM, or shorten `DELAY_MAX_MS` from 750 to 600 ms (29 KB from each RAM).
- **RAM2** takes the file list (6 KB) and the staging weights (7.5 KB).
- **Flash:** about 20 KB of code and the 55 KB the kit grows.

## Milestones

| | What | Done when |
|---|---|---|
| **M0** | Benchmark the engine alone on the Teensy: random A2-Lite weights, the cycle counter, a sketch on its own. | Measured at 48 kHz. **Over 30 %: stop and rethink before going further.** |
| **M1** | [Step 1](#step-1-the-whole-system-at-48-khz): the whole system at 48 kHz. | Host tests pass, the device checks pass, the CPU and memory tables updated. |
| **M2** | [Step 2](#step-2-the-a2-lite-engine): the engine. | `test_nam` passes, including the golden test. |
| **M3** | [Step 3](#step-3-reading-nam-files): the loader and `namtool`. | `test_namfile` passes; real A2 files from TONE3000 load. |
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
- **The golden test:** a real A2 file, run once through the reference implementation (the Python `neural-amp-modeler` package, v0.13 or later, or NeuralAmpModelerCore) by a script in `test/host`, on a test signal (a sweep, noise bursts and a guitar riff). Its Lite output is committed. Ours must match within 1e-4. The `.nam` file is committed too if its license allows; otherwise the script downloads it.
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
