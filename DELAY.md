# Delay

The delay sits between the IR and the reverb ([REVERB.md](REVERB.md)). It takes the mono signal after the IR and gives a stereo output: a wet/dry/wet dual delay voiced after the digital racks of 80s guitar rigs (TC 2290, Korg SDD-3000, Roland SDE-3000). It has clean, pitch-stable repeats that spread wide without bouncing, with ducking, preamp grit and converter character.

It runs in 32-bit float inside the same audio object as the convolution. Delay times run from 20 to 750 ms.

(Until 2026-10-29 there was also a ping-pong delay, a port of the HexeFX `AudioEffectDelayStereo_F32`. It was removed to save code; it is in the git history.)

## Contents

- [Controls](#controls)
- [How it works](#how-it-works)
- [On/off and mix](#onoff-and-mix)
- [Fixed voicing (`config.h`)](#fixed-voicing-configh)
- [Memory and CPU](#memory-and-cpu)
- [Starting points](#starting-points)
- [Code and tests](#code-and-tests)

## Controls

Every control is a serial command and a row on the panel's Delay page ([DISPLAY.md](DISPLAY.md#menu-tree)). Holding the push button on the home screen's DLY block toggles the delay. Every value is saved to EEPROM a few seconds after the last change.

| Command | Range | Default | What it does |
|---|---|---|---|
| `dl` | toggle | off | Delay on/off. Switching off lets the repeats ring out. |
| `dmix` | 0–100 | 30 | Dry/wet blend, with an equal-power crossfade. 0 = dry only, 100 = repeats only. |
| `dtime` | 20–750 ms | 380 | Delay time: the **left** time; the right one is ¾ of it. |
| `dfb` | 0–100 | 35 | Repeats (feedback). |
| `dtone` | 0–100 | 60 | Brightness of the repeats. Lower values make each repeat darker than the one before. |
| `dmod` | 0–100 | 15 | Modulation depth: a chorus on the tail. |
| `dduck` | 0–100 | 40 | How far the repeats drop while you play. 100 = −24 dB. |
| `ddrive` | 0–100 | 20 | Grit from the rack's input preamp. 0 = clean. |
| `dera` | 0–100 | 40 | Converter era: 0 = late-80s 16-bit / 16 kHz, 100 = early 10-bit / 6 kHz. |

`s` prints the delay's state and settings, and its share of the CPU.

## How it works

### Signal flow

```
                                     +--> envelope follower --> duck gain ------------+
                                     |                                                |
in --> drive --> anti-alias LP ------+--> (+) --> quantize --> line A (time T) ------+-x--> out L
                                     |     ^                                          |
                                     |     +-- fb x ((1-c) x filt A + c x filt B) --+ |
                                     |                                              | |
                                     +--> (+) --> quantize --> line B (time ¾ T) ---+-x--> out R
                                           ^
                                           +-- fb x ((1-c) x filt B + c x filt A)

filt X = bass cut (lowpass (line X output)): every repeat loses a little treble and bass
```

- **Two loops, not a ping-pong.** Line A (left) repeats at T and line B (right) at ¾ T, so each side has its own rhythm. The image is wide and the repeats don't bounce side to side, which is the classic wet/dry/wet rack setup. A little **cross-feed** (`c`, 20 %) takes part of each side's repeats into the other loop and blends the two tails.
- **Stable at any setting.** The cross-feed is a blend, `(1-c) x own + c x other`, so the loop gain never exceeds the feedback, which is at most 0.96. More feedback also lowers the input level a little, so the repeats don't build up.
- **Pitch-stable time changes.** A new time doesn't glide. The delay crossfades from the old read position to the new one over 20 ms, as digital racks do. The repeats never bend in pitch. During those 20 ms their phase moves once, from the old position to the new.

### The 80s controls

**Drive** is the rack's input preamp: up to 8× gain into a cubic soft clip, then scaled back down by the same amount. Loud input gets squashed and gritty, while quiet playing stays nearly clean. At 0 the stage is skipped, so the signal passes exactly unchanged.

**Era** is the converters. It sets two things at once:

| Era | Bandwidth (anti-alias lowpass) | Resolution |
|---|---|---|
| 0 | 16 kHz | 16 bits |
| 40 (default) | about 11 kHz | about 13.6 bits |
| 100 | 6 kHz | 10 bits |

- The bandwidth limits the input. The tone control's lowpass on the repeats then runs from 1.5 kHz (`dtone 0`) up to that ceiling (`dtone 100`), and both lowpasses are 2-pole Butterworths.
- Every sample written to the lines is truncated to the era's resolution. At high settings the end of a tail gets grainy, like an early digital unit.
- Truncation is toward zero, which can't sustain a limit cycle (a low-level tone that never dies away). So every tail decays to exact silence and the delay goes idle.

**Duck** works like the TC 2290's "dynamic delay":

- An envelope follower on the delay input (5 ms attack, 200 ms release) pulls the wet output down while you play.
- Ducking starts when the input passes −36 dBFS and is complete 12 dB above that. At that point the repeats are `dduck × 24 dB` quieter.
- When you stop playing, the repeats swell back up within about half a second.
- Ducking acts only on the outputs, not inside the loops, so the repeats keep building underneath and come back at full length.

**Modulation** is a 0.8 Hz sine. `dmod 100` moves the read positions by ±1.5 ms, with L and R in opposite phase. That gives a wide, chorused tail and keeps the repeats on time on average. The default of 15 is a subtle shimmer, about 2 cents.

## On/off and mix

The delay runs in an effect stage (`lib/irdsp/src/FxStage.h`, with `FxSlot.h`):

- **On** fades the delay's input in over 20 ms. **Off** fades the input out and lets the repeats ring.
- Once the delay is off and its output has stayed below −100 dBFS for 0.5 s, it goes **idle**:
  - The lines are cleared, a few KB per block, and the next start jumps straight to the set time.
  - From then on the delay passes its input through bit-identically and uses no CPU.
  - With both the delay and the reverb idle, the output is mono again.
- **Mix** is an equal-power crossfade with 20 ms ramps.

## Fixed voicing (`config.h`)

These are compile-time settings in `include/config.h`, under *Delay*.

| Define | Default | Effect |
|---|---|---|
| `DELAY80_RIGHT_RATIO` | 0.75 | Right time = left time × this (0.1–1). 0.75 gives a dotted feel against the left. 1.0 makes both sides repeat together (a mono delay with a chorused stereo tail). |
| `DELAY80_CROSS` | 0.2 | Share of each side's repeats fed into the other loop (0–0.5). |
| `DELAY80_BASS_CUT` | 0.15 | Bass loss per repeat (0–1). |
| `DELAY80_MOD_RATE_HZ` | 0.8 | Chorus rate. |
| `DELAY80_DUCK_THRESHOLD_DB` | −36 | Input level where ducking starts. It is complete 12 dB higher. |
| `DELAY80_DUCK_RELEASE_MS` | 200 | How fast the repeats swell back after you stop. |

`DELAY_MAX_MS` (750) sets the longest time and the size of the lines.

## Memory and CPU

**Memory**

- The delay uses two float lines of `DELAY_MAX_MS` each, about 141 KB per line at 750 ms (48 kHz). One is in RAM1 (DTCM) and the other in RAM2 (OCRAM), because neither bank has room for both.
- Every 100 ms of `DELAY_MAX_MS` costs about 19 KB in each bank. See [DISPLAY.md, RAM1 strategy](DISPLAY.md#ram1-strategy) for the current budget.

**CPU**

About 4.4 % on a Teensy 4.0 with 32-sample blocks, while the delay is on or ringing (twice the reads during a time crossfade, plus filters, quantizer, drive and envelope). An idle delay uses nothing. `s` shows the live figure.

## Starting points

These are suggestions to get near a familiar 80s sound quickly, not exact settings from any record. Type the commands one per line.

| Sound | Settings |
|---|---|
| **Wet/dry/wet lead.** Big solos that stay clear while you play and bloom in the gaps. | `dtime 450` `dfb 30` `dmix 30` `dtone 60` `dmod 20` `dduck 60` `ddrive 10` `dera 20` |
| **Dotted eighths.** Rhythmic arpeggios; 375 ms is a dotted eighth at 120 BPM. | `dtime 375` `dfb 35` `dmix 40` `dtone 70` `dmod 10` `dduck 0` `ddrive 50` `dera 40` |
| **Early digital doubler.** A short, gritty thickener. | `dtime 120` `dfb 10` `dmix 30` `dtone 50` `dmod 25` `dduck 0` `ddrive 30` `dera 80` |
| **Ambient swells.** Long, dark, chorused tails behind volume swells. | `dtime 700` `dfb 60` `dmix 35` `dtone 40` `dmod 40` `dduck 80` `ddrive 0` `dera 50` |

For dotted eighths at another tempo, use `dtime` = 45000 / BPM. For the classic single dotted-eighth delay, set `DELAY80_RIGHT_RATIO` to 1.0 so both sides repeat together.

## Code and tests

| File | Contents |
|---|---|
| `lib/irdsp/src/StudioDelay.h` | The delay. Portable C++, no Arduino. |
| `lib/irdsp/src/irdsp_blocks.h` | Shared building blocks: Hermite read, biquad lowpass, shelving filter, LFO, mix law. |
| `lib/irdsp/src/FxStage.h`, `FxSlot.h` | On/off with trails, dry/wet mix, idle detection and clearing. |
| `src/AudioConvolverIR.h` | Runs the delay stage each block. |
| `lib/oledui/src/ParamDefs.cpp`, `src/Params.cpp` | The delay's settings: ranges and defaults, and what each one does. |
| `test/host/test_studio_delay.cpp`, `test_fxstage.cpp` | PC tests. |

Run the tests on a PC with `make -C test/host test`.

- The repeats land at T (left) and ¾ T (right), with the expected gains and cross-feed.
- 60 s of noise with feedback, modulation, drive, era and cross-feed all at maximum stays finite and bounded.
- At era 100 with high feedback the tail reaches exact silence.
- Clearing empties both lines.
- A time change has no jumps, and the pitch is exact outside the 20 ms crossfade.
- Ducking is −24 dB at `dduck 100` while you play, and fully recovered 1 s after you stop.
- Drive 0 is exactly linear, drive 100 squashes loud input, and era 100 cuts 10 kHz by more than 6 dB.
- After a clear, the next start plays at the set time, with no crossfade from the old one (`test_fxstage`).
