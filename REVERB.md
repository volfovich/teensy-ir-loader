# Reverb

The reverb is the last effect, after the IR and the delay. It takes the stereo signal and returns a stereo signal. There are two types, which share the same memory and the basic controls (Mix, Size, Tone):

| Type | `rtype` | Character |
|---|---|---|
| **Plate** (default) | `0` | A port of the HexeFX `AudioEffectPlateReverb_F32` (MIT, Piotr Zapart): a smooth, dense FV-1-style plate. |
| **80s** | `1` | The rack reverbs of 80s rock and metal records, in two modes: a Lexicon-style **hall** (`rmode hall`) and an AMS-style **gated** reverb (`rmode gated`). |

Both run in 32-bit float inside the same audio object as the convolution.

## Contents

- [Controls](#controls)
- [80s hall](#80s-hall)
- [80s gated](#80s-gated)
- [Plate](#plate)
- [On/off, mix and switching type](#onoff-mix-and-switching-type)
- [Starting points](#starting-points)
- [Memory and CPU](#memory-and-cpu)
- [Code and tests](#code-and-tests)

## Controls

All controls are serial commands. The reverb button (pin 5) toggles the reverb on and off. Every value is saved to EEPROM a few seconds after the last change. The planned OLED menu gives each one a row on the Reverb page, showing only the rows that apply to the current type and mode; see [DISPLAY.md](DISPLAY.md#menu-tree) and `screenshots/17_menu_reverb_80s.png`, `18_menu_reverb_gated.png`.

| Command | Range | Default | Applies to | What it does |
|---|---|---|---|---|
| `rv` | toggle | off | all | Reverb on/off. Switching off lets the tail ring out. |
| `rtype` / `rtype 0\|1` | 0, 1 | 0 | — | Toggles or sets the type: 0 = plate, 1 = 80s. |
| `rmode` / `rmode hall\|gated` | hall, gated | hall | 80s | Toggles or sets the 80s mode. |
| `mix` | 0–100 | 25 | all | Dry/wet blend, with an equal-power crossfade. 0 = dry only, 100 = reverb only. |
| `size` | 0–100 | 50 | all | Plate: reverb time, about 1.1–6 s. 80s hall: reverb time, 0.8–8 s (2.5 s at 50). 80s gated: the burst's shape, from decaying (0) to flat (50) to rising (100). |
| `tone` | 0–100 | 50 | all | Brightness of the tail. 80s: 2.5 to 12 kHz, never above the era's bandwidth. |
| `rpre` | 0–150 ms | 40 | 80s | Pre-delay: a gap before the reverb starts, so your picking stays clear. |
| `rmod` | 0–100 | 40 | 80s hall | Chorus in the tail, from still to lush. |
| `rera` | 0–100 | 40 | 80s | Converter era: 0 = late-80s 16-bit / 16 kHz, 100 = early 10-bit / 6 kHz. |
| `rgate` | 100–600 ms | 300 | 80s gated | How long the burst lasts before it stops. |

`s` prints the reverb's state, type and settings, and its share of the CPU, for example:

```
Reverb:    on, 80s hall, mix 25%, size 50%, tone 50%, pre 40 ms, era 40%, mod 40%
```

The plate ignores the 80s settings, and each 80s mode ignores the other's (Mod, Gate), but they keep their values for when you switch back.

## 80s hall

The big rack halls of 80s rock records: Lexicon 224/224XL and 480L. They're the "arena" sound of the leads, power ballads and clean arpeggios of the time.

```
in (L+R)/2 -> pre-delay -> era: anti-alias LP + 16..10-bit -> 4 allpass diffusers
   -> 8-line feedback delay network (Hadamard mixing), each line:
        damping lowpass (Tone) x decay (Size); 4 of them chorused (Mod)
   -> output taps along the lines (4 per side) -> 100 Hz low cut -> L / R
```

- **Wide and dense:** eight delay lines of 45–100 ms, mixed by an 8-point Hadamard matrix. Each side takes its output from different lines, so even a mono guitar gives a wide, decorrelated tail.
- **Starts right after the pre-delay:** the output taps sit along the lines, not only at their ends, so the reverb starts 2.5 ms after the pre-delay.
- **Chorused tail:** four of the lines are read at slowly moving positions, two LFOs at 0.53 and 0.89 Hz in quadrature. That gives the shimmering, slightly detuned tail of the Lexicons and keeps it from ringing metallically.
- **Dark highs, lean lows:** each line loses treble on every pass (Tone), and a 100 Hz low cut keeps the tail from muddying the low strings.
- **Era:** the input passes a steep (4th-order) anti-alias filter at the era's bandwidth and is truncated to its resolution, as in the studio 80s delay. At 40 it's about 10.8 kHz and 13.6 bits.

## 80s gated

The AMS RMX16 "Nonlin" sound. Each hit gets a dense burst of reverb that stays at the same level for the gate time, then stops dead, instead of decaying slowly. It was famous on 80s snares, and on guitar it gives tight, huge rhythm parts that never wash out the next chord.

```
in (L+R)/2 -> pre-delay -> era -> 2 short allpass diffusers -> line
   -> 24 taps per side, spread over the gate time, gains shaped by Size -> Tone lowpass -> L / R
```

- **Gate** (`rgate`, 100–600 ms) sets how long the burst lasts.
- **Size** shapes it:
  - 0 = decaying, a short room
  - 50 = flat, the classic Nonlin
  - 100 = rising, a reverse-like swell
- **Stops dead:** the burst is 40 dB down within 40 ms after the gate time, because the diffusion is kept short.
- **Wide:** left and right use different tap positions and signs.

## Plate

The original reverb, ported from HexeFX:

1. The signal goes through two chains of four allpass diffusers, one per side.
2. They feed a loop of four delay lines and allpasses with shelving filters, scaled by the reverb time.
3. The outputs are taken from different points in the loop.
4. Two slow LFOs modulate the loop.

Its fixed voicing (diffusion, bass damping, output filters) is set in `include/config.h` (`REVERB_*`).

## On/off, mix and switching type

- **Off is click-free:** the tail rings out, then the reverb uses no CPU and passes the signal through bit for bit.
- **Switching while it sounds:** a change of type, or of the 80s mode, fades the tail out over 20 ms. The shared memory is then cleared in about 8 audio blocks before the new reverb starts, so there's never a burst of the old tail through the new algorithm.
- **Switching while idle:** the change takes effect at once.

## Starting points

| Sound | Settings |
|---|---|
| 80s lead (hall) | `rtype 1`, `rmode hall`, `size 60`, `rpre 60`, `rmod 50`, `rera 30`, `mix 25` |
| Power-ballad cleans | `rtype 1`, `rmode hall`, `size 80`, `rpre 80`, `rmod 60`, `tone 40`, `mix 30` |
| Tight, huge rhythm (gated) | `rtype 1`, `rmode gated`, `rgate 250`, `size 50`, `rpre 10`, `mix 20` |
| Reverse-swell ambience | `rtype 1`, `rmode gated`, `rgate 500`, `size 100`, `mix 30` |

To listen on a computer first, `make -C test/host render` writes `test/host/out/reverb80_*.wav`:
- a plucked-guitar riff through the hall
- palm-muted chugs through the gated reverb
- the drum machine's rock groove through the gated reverb (the classic 80s snare)
- the dry versions

## Memory and CPU

**Memory:** both types share one block in RAM2 (OCRAM): 136 KB, the size of the larger (the 80s reverb). Only one type runs at a time.

| Part | Size |
|---|---|
| Pre-delay | 26 KB |
| Diffusers | 4 KB |
| Hall lines / gate line (shared by the two modes) | 106 KB |

RAM1 holds only the reverbs' state and the real-time code. Setup, clearing and the coefficient math run from flash.

**CPU**, measured on the Teensy 4.0 with 32-sample blocks:

| Type | CPU (while on or ringing) |
|---|---|
| Plate | 6.2 % |
| 80s hall | 7.5 % |
| 80s gated | 6.0 % |

**How the 80s reverb stays fast:**
- It works in 32-sample chunks, one line at a time, so it streams through each line in the cached OCRAM instead of jumping between all of them every sample.
- The chorus LFOs run once per chunk and are interpolated.
- The chorused reads use linear interpolation, as the units of the day did.

## Code and tests

| File | Contents |
|---|---|
| `lib/irdsp/src/Reverb80.h`, `Reverb80.cpp` | The 80s reverb (hall and gated). Portable C++, no Arduino. The real-time code is in the header; setup, clearing and coefficients are in the .cpp, in flash. |
| `lib/irdsp/src/PlateReverb.h` | The plate. |
| `src/AudioConvolverIR.h` | Runs the reverb each block, switches types, mixes. |
| `src/AudioGraph.cpp`, `src/SerialUI.cpp`, `src/Settings.cpp` | Setters, the `r*` commands, EEPROM (`"IRL8"`). |

`make -C test/host test` runs `test_reverb80`. It checks:

- **Hall:**
  - Stable at the maximum size.
  - The measured RT60 follows Size (0.8, 2.5 and 8 s, within 25 %).
  - Nothing before the pre-delay.
  - Left and right decorrelated.
  - Mod moves the tail.
  - Era 100 removes the top end (more than 20 dB less above 14 kHz).
  - 100 % wet about as loud as the input on pink noise.
- **Gated:**
  - More than 40 dB down 40 ms after a 200 or 500 ms gate.
  - Flat at Size 50, decaying at 0, rising at 100.
  - Levelled to the input.
- **Both:** 60 s of loud noise stays finite, and a clear leaves exact silence.

`test_reverb` checks the plate.
