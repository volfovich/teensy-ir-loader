# Drum machine

A small drum machine to practice along with. It plays one of eight built-in styles at 40–240 BPM. A count-in and a speed trainer are built in.

- **Synthesized:** every sound is generated in code, so there are no sample files and it uses almost no memory.
- **Kept apart from the guitar:** the drums are mixed into both outputs (line out and headphones), centered, **after** the IR, delay and reverb. They never go through your guitar effects, and they have their own volume.

## Contents

- [Quick start](#quick-start)
- [Controls](#controls)
- [Count-in and speed trainer](#count-in-and-speed-trainer)
- [Styles](#styles)
- [Sounds](#sounds)
- [Timing, levels, CPU and memory](#timing-levels-cpu-and-memory)
- [Defaults (`config.h`)](#defaults-configh)
- [Code and tests](#code-and-tests)
- [Ideas for later](#ideas-for-later)

## Quick start

Over USB serial at 115200:

```
drum style          list the styles
drum style shuffle  choose one (by name or number)
bpm 90              set the tempo
drum                start: one bar of count-in clicks, then the groove
drum                stop
```

Playing is never saved, so the drums are always stopped after power-up. Everything else is saved to EEPROM a few seconds after the last change.

## Controls

| Command | Range | Default | What it does |
|---|---|---|---|
| `drum` | toggle | stopped | Start / stop. Starting plays the count-in (if on), then the pattern from bar 1. Stopping lets the last hits ring out. |
| `bpm` | 40–240 | 100 | Tempo, from the next step. If the trainer is on, it starts again from this tempo. For the 6/8 ballad a beat is a dotted quarter, so 60 BPM is a slow ballad. |
| `drum style` | — | — | Lists the styles; `>` marks the current one. |
| `drum style <n\|name>` | 1–8 or a name | rock | Changes the style. While playing, the change happens at the next bar. |
| `drum vol` | 0–100 | 60 | Drum volume: 100 = 0 dB, 60 ≈ −8 dB, 0 = silent. |
| `drum count` | toggle | on | One bar of clicks before the pattern starts. |
| `drum train` | toggle | off | Speed trainer on/off. |
| `drum train <step> <every> <target>` | 1–20 BPM, 1–16 bars, 40–240 BPM | 5, 4, 140 | Turns the trainer on with these settings. |

`s` shows the drums' state and settings, the current tempo (which rises with the trainer), the bar number and the drums' share of the CPU, for example:

```
Drums:     playing blues shuffle, 105 BPM, bar 9, vol 60%, count-in on, trainer +5 every 4 bars to 140
```

**On the OLED** (once the module is wired), the menu has a **Drums** page:
- Play, Style, Tempo, Volume, Count-in, and **Trainer**, which opens its own page: On, Step, Every and Target. All four are always shown, so the trainer can be set up before it's switched on. The Trainer row on the Drums page shows ON/OFF.
- Style is chosen by turning the encoder and is shown by name.
- While the drums play, the home screen shows them on row 5, for example `DRM rock 100   bar 3`.

See [DISPLAY.md](DISPLAY.md#menu-tree) and the renders `screenshots/12_menu_drums.png`, `13_edit_drum_style.png`, `14_menu_trainer.png` and `15_edit_trainer_target.png`.

## Count-in and speed trainer

**Count-in:** one bar of clicks on the beats, with a higher click on beat 1. Then the pattern starts exactly one bar later. In 4/4 that's four clicks, and in 6/8 two (on the dotted quarters).

**Speed trainer:** for working a riff up to speed.
- It starts at the set tempo (`bpm`). After every `every` bars it adds `step` BPM, until it reaches `target`, and then it holds there.
- The tempo always changes on a bar line, never mid-bar.
- `s` shows the tempo it has reached.
- Changing `bpm` restarts the climb from the new tempo.

```
bpm 100
drum train 5 4 140     100 BPM for 4 bars, then 105, 110, ... up to 140
drum                   start
```

## Styles

Each grid is one bar. Steps are 16ths, or triplet 8ths for the triplets, the shuffle and the 6/8 ballad. `X` = accent, `x` = normal, `g` = ghost note, `.` = rest. The numbers above the grid are the beats.

### 1. metronome (`metronome`)

A plain click, higher on beat 1.

```
         1    2    3    4
Click    X... x... x... x...
```

### 2. rock (`rock`)

Straight 8ths on the hat, with a pushed kick on the "&" of 3.

```
         1    2    3    4
Kick     x... .... x.x. ....
Snare    .... x... .... x...
Hat      X.x. X.x. X.x. X.x.
```

### 3. offbeat 8ths (`offbeat8`)

A straight beat with the hat only on the "&" of each beat.

```
         1    2    3    4
Kick     x... .... x... ....
Snare    .... x... .... x...
Hat      ..x. ..x. ..x. ..x.
```

### 4. offbeat 16ths (`offbeat16`)

The same beat with the hat on "e & a", leaving the beat itself to the kick and snare. The "&" is accented.

```
         1    2    3    4
Kick     x... .... x... ....
Snare    .... x... .... x...
Hat      .xXx .xXx .xXx .xXx
```

### 5. triplets (`triplets`)

The same beat in triplets: kick or snare, hat, hat on every beat.

```
         1   2   3   4
Kick     x.. ... x.. ...
Snare    ... x.. ... x..
Hat      .xx .xx .xx .xx
```

### 6. pop (`pop`)

Straight 8ths with an open hat at the end of the bar.

```
         1    2    3    4
Kick     x... ..x. x... ....
Snare    .... x... .... x...
Hat      x.x. x.x. x.x. x...
Open hat .... .... .... ..x.
```

### 7. blues shuffle (`shuffle`)

Swung 8ths on the triplet grid.

```
         1   2   3   4
Kick     x.. ... x.. ...
Snare    ... x.. ... x..
Hat      X.x x.x X.x x.x
```

### 8. 6/8 ballad (`ballad`)

6/8: two dotted-quarter beats per bar, so 60 BPM is a slow ballad.

```
         1   2
Kick     x.. ...
Snare    ... x..
Hat      Xxx Xxx
```

To add or change a style, edit the table in `lib/irdsp/src/DrumPatterns.cpp`. Each lane is one string per voice, and `test_drums` checks that every lane fits its style.

## Sounds

All voices are synthesized, in the spirit of the classic analog drum machines:

| Voice | How it's made | Decay (to −60 dB) |
|---|---|---|
| Kick | A sine whose pitch falls from 160 to 50 Hz, plus a 12 ms noise click for the beater | 0.6 s |
| Snare | A 185 Hz body plus noise band-limited to 1–9 kHz (the snares) | body 0.2 s, snares 0.3 s |
| Rim (not in the built-in styles) | Cross-stick: 1.7 kHz and 830 Hz sines plus a high-passed noise tick | 60 ms |
| Closed / open hat | TR-808-style metal (six square waves at 205–800 Hz, inharmonic) mixed with noise, then high-passed at 7 kHz | 90 ms / 0.7 s; a closed hat cuts off a ringing open one, like a real hi-hat |
| Ride (not in the built-in styles) | The same metal and noise, band-passed around 4.5 kHz | 2.5 s |
| Click | Metronome and count-in: 2 kHz on accents, 1.2 kHz otherwise | 50 ms |

- **Velocity:** every hit varies by ±4 %, so straight 8ths and 16ths don't sound like a machine gun.
- **Levels:** ghost notes play at 30 % and normal hits at 70 % of an accent.
- **Bus:** the voices are summed and soft-clipped, so even every voice at once can't exceed full scale.
- The rim and ride are there for your own styles: add a lane for them in `DrumPatterns.cpp`.

## Timing, levels, CPU and memory

- **Timing is sample-accurate.** Steps fire at their exact sample inside an audio block. A fractional sample counter keeps odd tempos such as 97 BPM from drifting: after 16 bars the beats are still within one sample (23 µs) of the ideal grid.
- **Changes are musical:**
  - a tempo change applies from the next step
  - a style change and each trainer step from the next bar line
  - stopping cuts the sequencer at once, but the last hits ring out
- **Levels:** at the default volume (60) the drums peak around −13 dBFS. At 100 the loudest style peaks around −5 dBFS. Set the drum volume against your guitar, and watch `s` for clipped samples, because the drums add to the guitar signal at the output.
- **CPU:** about 2 % while playing (measured on the Teensy 4.0 with 32-sample blocks). Once stopped and silent it uses nothing, and the output is the guitar alone, bit for bit.
- **Memory:**
  - RAM1: about 1.8 KB of data (two pattern buffers and the voices' state) and about 3.7 KB of code (the real-time part).
  - Flash: setup, parsing and the style table.

## Defaults (`config.h`)

| Define | Default | Meaning |
|---|---|---|
| `DEFAULT_DRUM_STYLE` | 2 | Style number, 1-based (2 = rock) |
| `DEFAULT_DRUM_TEMPO` | 100 | BPM |
| `DEFAULT_DRUM_VOLUME` | 60 | 0–100 |
| `DEFAULT_DRUM_COUNT_IN` | 1 | Count-in on |
| `DEFAULT_TRAINER_ON` | 0 | Trainer off |
| `DEFAULT_TRAINER_STEP` / `_EVERY` / `_TARGET` | 5 / 4 / 140 | +5 BPM every 4 bars, up to 140 BPM |

These are used when the EEPROM is blank, and when settings saved by older firmware (before `"IRL5"`) are upgraded.

## Code and tests

| File | Contents |
|---|---|
| `lib/irdsp/src/DrumMachine.{h,cpp}` | Voices, sequencer, count-in, trainer. Portable C++, no Arduino. |
| `lib/irdsp/src/DrumPatterns.cpp` | The style table (in flash on the Teensy). |
| `src/AudioConvolverIR.h` | Runs the drums each block and adds them to both outputs. |
| `src/AudioGraph.cpp`, `src/SerialUI.cpp`, `src/Settings.cpp` | Setters, the `drum` and `bpm` commands, EEPROM. |
| `lib/oledui/src/Menu.cpp`, `Screens.cpp` | The Drums menu page and the home-screen row. |

`make -C test/host test` runs `test_drums`, which checks:
- **Timing:** the beat grid at 120 and 97 BPM over 16 bars.
- **Count-in:** four clicks, then the first kick exactly one bar later.
- **Speed trainer:** bar lengths step 100, 100, 105, 105, 110 BPM and then hold.
- **Changes:** a style change lands on the next bar and a tempo change on the next step.
- **Shuffle:** the hits land on the triplet grid.
- **Practice beats:** in the offbeat 8ths, offbeat 16ths and triplets styles the hat lands only on its offbeat positions, never on the beat, with the kick on 1 and 3 and the snare on 2 and 4.
- **Stop:** no hits after stopping, and exact silence and idle once the sound has died away.
- **Every style:** its lanes fit, it stays below full scale and it is clearly audible.

To listen on your computer, `make -C test/host render` writes 8 bars of every style, with the count-in, to `test/host/out/drums_*.wav`.

## Ideas for later

None of these are built yet. They're practice features worth adding next:

1. **Swing amount:** a 0–66 % swing control, so offbeat 8ths or rock can go from straight, to a light swing, to a full shuffle. The sequencer would delay every second subdivision by the swing amount.
2. **Gap trainer:** the drums drop out for 1–2 bars out of every 4 (or another chosen ratio), then come back. If you're still on the grid when they return, your timing is solid.
3. **Phrase markers:** a crash cymbal on bar 1 and a short fill every 4 or 8 bars, so you don't lose your place in a song form. This needs a crash voice, which can be built from the existing metal and noise sources, and a tom for fills.
4. **12-bar blues mode:** the shuffle with a bar counter (1–12) in `s` and on the display, plus a fill on the turnaround (bars 11–12), for practicing blues changes.
5. **Tap tempo:** tap `t` over serial, or later a key on the display, a few times to set the tempo from a song.
6. **More meters:** a 3/4 waltz and a 12/8 slow blues, a very common practice groove. Both fit the existing style format (3 beats x 4 steps, and 4 beats x 3 steps).
