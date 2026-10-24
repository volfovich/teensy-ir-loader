# Drum machine

A small drum machine to practice along with. It plays one of eight built-in one-bar styles, or a whole **song**, at 40–240 BPM. A count-in, a speed trainer and crash accents are built in.

- **Songs:** multi-bar drum parts with verses, choruses and fills:
  - five built-in 32-bar songs in the style of classic rock songs
  - any General MIDI drum file (`.mid`) you copy to `/DRUMS` on the SD card
  - loop any section (bars A–B) to practice it

- **Three kits:**
  - **rock** (default): a recorded acoustic kit, the AVL Drumkits "Red Zeppelin". Its samples are compiled into the firmware and played straight from flash.
  - **pearl**: the AVL Drumkits "Black Pearl", a smaller, tighter recorded kit, played the same way.
  - **synth**: every sound generated in code, in the spirit of the classic analog drum machines.
- **Crash accents:** a crash cymbal on the first beat of every 2, 4, 8 or 16 bars, so you always know where the phrase starts.
- **Like a drummer:** hits have velocities 1–9 that follow a drummer's hands, vary at random a little (more on the hats), play recorded velocity layers, and are panned like a real kit.
- **Kept apart from the guitar:** the drums are mixed into the outputs (line out and headphones), in stereo, **after** the IR, delay and reverb. They never go through your guitar effects, and they have their own volume.

## Contents

- [Quick start](#quick-start)
- [Controls](#controls)
- [Count-in and speed trainer](#count-in-and-speed-trainer)
- [Crash accents](#crash-accents)
- [Styles](#styles)
- [Songs](#songs)
- [Dynamics, humanize and panning](#dynamics-humanize-and-panning)
- [Kits and sounds](#kits-and-sounds)
- [Adding a kit](#adding-a-kit)
- [Drum bus](#drum-bus)
- [Timing, levels, CPU and memory](#timing-levels-cpu-and-memory)
- [Defaults (`config.h`)](#defaults-configh)
- [Code and tests](#code-and-tests)
- [Ideas for later](#ideas-for-later)

## Quick start

Over USB serial at 115200:

```
drum style          list the styles
drum style shuffle  choose one (by name or number)
drum kit rock       a recorded kit (or: drum kit pearl, drum kit synth)
drum crash 4        a crash every 4 bars (0 = off)
bpm 90              set the tempo
drum                start: two bars of count-in clicks, then the groove
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
| `drum song` | — | — | Lists the songs: the built-in ones, then the files in `/DRUMS`. |
| `drum song <n\|name\|off>` | a number or a name | off | Plays a song instead of the style; `off` goes back to the style. See [Songs](#songs). |
| `drum loop <a> <b>` / `drum loop off` | bars | off | Loops bars a–b of the song, from the next bar line. |
| `drum kit` | — | — | Lists the kits; `>` marks the current one. |
| `drum kit <n\|name>` | 1–3 or a name | rock | Changes the kit at once. Hits still ringing fade out over 5 ms. |
| `drum crash <0\|2\|4\|8\|16>` | off, 2, 4, 8, 16 bars | 4 | A crash on beat 1 of every N bars (see [Crash accents](#crash-accents)). `drum crash` alone shows the setting. |
| `drum vol` | 0–100 | 60 | Drum volume: 100 = 0 dB, 60 ≈ −8 dB, 0 = silent. |
| `drum comp` | 0–100 | 40 | Drum-bus compression: 0 = natural, 100 = squashed (see [Drum bus](#drum-bus)). `drum comp` alone shows it. |
| `drum count [0-4]` | off, 1–4 bars | 2 bars | Bars of count-in clicks before the groove starts, and after every speed-trainer tempo step. `drum count` alone toggles between off and 2 bars. |
| `drum train` | toggle | off | Speed trainer on/off. |
| `drum train <step> <every> <target>` | 1–20 BPM, 1–16 bars, 40–240 BPM | 5, 4, 140 | Turns the trainer on with these settings. |

`s` shows the drums' state and settings, the current tempo (which rises with the trainer), the bar number and the drums' share of the CPU, for example:

```
Drums:     playing blues shuffle, 105 BPM, bar 9, kit rock, crash every 4 bars, vol 60%, count-in 2 bars, trainer +5 every 4 bars to 140
```

**On the OLED** (once the module is wired), the menu has a **Drums** page:
- Play, Style, **Song**, **Kit**, **Crash**, Tempo, Volume, **Comp**, Count-in, and **Trainer**, which opens its own page: On, Step, Every and Target. All four are always shown, so the trainer can be set up before it's switched on. The Trainer row on the Drums page shows ON/OFF.
- **Loop from** and **Loop to** appear under Song while a song is selected (from "off" = the whole song).
- Style, Song and Kit are chosen by turning the encoder and are shown by name.
- Each push on Crash steps it: off, 2, 4, 8, 16 bars.
- Each push on Count-in steps it: off, 1, 2, 3, 4 bars.
- While the drums play, the home screen shows them on row 5, for example `DRM rock 100   bar 3`.

See [DISPLAY.md](DISPLAY.md#menu-tree) and the renders `screenshots/12_menu_drums.png`, `13_edit_drum_style.png`, `14_menu_trainer.png`, `15_edit_trainer_target.png`, `16_edit_drum_kit.png` and `19_menu_drums_song.png`.

## Count-in and speed trainer

**Count-in:** two bars of clicks on the beats by default (`drum count 0`–`4` sets 0–4 bars), with a higher click on each bar's beat 1. Then the groove starts exactly on the next bar line. In 4/4 that's four clicks a bar, and in 6/8 two (on the dotted quarters).

**Speed trainer:** for working a riff up to speed.
- It starts at the set tempo (`bpm`). After every `every` bars it adds `step` BPM, until it reaches `target`, and then it holds there.
- The tempo always changes on a bar line, never mid-bar.
- `s` shows the tempo it has reached.
- Changing `bpm` restarts the climb from the new tempo.
- **Count-in at each step:** with the count-in on, every tempo step plays the count-in again (2 bars by default) at the **new** tempo before the groove goes on, so you hear the new speed before you play it. The count-in bars don't count toward `every`.
- Once the trainer reaches its target, it plays on with no more count-ins.

```
bpm 100
drum train 5 4 140     100 BPM for 4 bars, then 2 bars of clicks at 105,
drum                   4 bars at 105, 2 bars of clicks at 110, ... up to 140
```

## Crash accents

A crash on the first beat of every 2, 4, 8 or 16 bars (`drum crash`, default every 4), the way a drummer marks the start of each phrase:

- **Counting:** bars count from the first bar after a count-in, including the count-ins between trainer steps. So the groove always opens, and reopens, with a crash; with every 4 the crashes land on bars 1, 5, 9…
- **Replaces the hat:** the crash takes the place of that step's hi-hat or ride hit. The kick and snare stay.
- **Timing of changes:** a change takes effect from the next bar, in time with the bar count from the start.
- **Exceptions:** no crash during the count-in or in the metronome style.
- **Every kit:** the synth kit has a synthesized crash, the rock and pearl kits recorded ones. Each crash rings out under the next one; at most two ring at once.

## Styles

Each grid is one bar. Steps are 16ths, or triplet 8ths for the triplets, the shuffle and the 6/8 ballad. `1`–`9` = a hit's velocity (9 hardest), `.` = rest. The numbers above the grid are the beats. The velocities follow a drummer's hands; see [Dynamics, humanize and panning](#dynamics-humanize-and-panning).

### 1. metronome (`metronome`)

A plain click, higher on beat 1.

```
         1    2    3    4
Click    9... 6... 6... 6...
```

### 2. rock (`rock`)

Straight 8ths on the hat, with a pushed kick on the "&" of 3.

```
         1    2    3    4
Kick     9... .... 8.7. ....
Snare    .... 8... .... 9...
Hat      9.6. 5.6. 8.6. 5.6.
```

### 3. offbeat 8ths (`offbeat8`)

A straight beat with the hat only on the "&" of each beat.

```
         1    2    3    4
Kick     9... .... 8... ....
Snare    .... 8... .... 9...
Hat      ..7. ..6. ..7. ..6.
```

### 4. offbeat 16ths (`offbeat16`)

The same beat with the hat on "e & a", leaving the beat itself to the kick and snare. The "&" is the strongest of the three.

```
         1    2    3    4
Kick     9... .... 8... ....
Snare    .... 8... .... 9...
Hat      .474 .474 .474 .474
```

### 5. triplets (`triplets`)

The same beat in triplets: kick or snare, hat, hat on every beat.

```
         1   2   3   4
Kick     9.. ... 8.. ...
Snare    ... 8.. ... 9..
Hat      .65 .65 .65 .65
```

### 6. pop (`pop`)

Straight 8ths with an open hat at the end of the bar.

```
         1    2    3    4
Kick     9... ..7. 8... ....
Snare    .... 8... .... 9...
Hat      9.6. 5.6. 8.6. 5...
Open hat .... .... .... ..7.
```

### 7. blues shuffle (`shuffle`)

Swung 8ths on the triplet grid.

```
         1   2   3   4
Kick     9.. ... 8.. ...
Snare    ... 8.. ... 9..
Hat      9.6 5.6 8.6 5.6
```

### 8. 6/8 ballad (`ballad`)

6/8: two dotted-quarter beats per bar, so 60 BPM is a slow ballad.

```
         1   2
Kick     9.. ...
Snare    ... 9..
Hat      966 566
```

To add or change a style, edit the table in `lib/irdsp/src/DrumPatterns.cpp`. Each lane is one string per voice, with `.` for a rest and `1`–`9` for a hit (`g`, `x` and `X` still work, as 3, 6 and 9). `test_drums` checks that every lane fits its style.

## Songs

A song is a whole drum part, many bars long, instead of one bar repeating: verses, choruses, fills and endings, and meter changes. It plays with the kit, velocities, humanizing, panning and drum bus of the styles.

```
drum song               list them
drum song sandmanish    pick one (by name or number): the tempo becomes the song's
drum                    start: the count-in, then the song from bar 1
drum loop 9 16          practice bars 9-16 over and over (from the next bar line)
drum loop off           the whole song again
drum song off           back to the style
```

**How a song plays:**
- It starts after the count-in (in the first bar's meter) and wraps to bar 1 at the end.
- With a loop, it starts at the loop's first bar and jumps back to it after the loop's last bar.
- `s` shows the bar, for example `bar 12/32, loop 9-16`.
- Changing the song while the drums play stops them, then starts the new song with the count-in.
- `bpm` still changes the tempo.
- Crash accents and the speed trainer apply to the styles only; songs have their own crashes.
- The chosen song is saved and comes back after a power cycle. The loop isn't saved.

### Built-in songs

Five 32-bar arrangements *in the style of* classic rock songs. They capture the grooves, the shape of the song and typical fills, but they aren't transcriptions:

| # | Name | Title | BPM | What's in it |
|---|---|---|---|---|
| 1 | `blackish` | Back in Black-ish | 94 | Hi-hat count-in, a laid-back straight-8th groove with pickups, crash choruses |
| 2 | `childish` | Sweet Child-ish | 125 | Driving 8ths, tom fills down the kit, ride choruses |
| 3 | `prayerish` | Livin on a Prayer-ish | 122 | 16th hats, a build-up with snare 8ths into big crash choruses |
| 4 | `sandmanish` | Enter Sandman-ish | 123 | Hi-hat and floor-tom intro, heavy 8th groove, 16th tom fills, double-kick choruses |
| 5 | `smokeish` | Smoke on Water-ish | 112 | The hi-hat alone for 4 bars, then the band; kick on every beat in the chorus |

They're plain text files in `songs/`, compiled into the firmware on every build. [songs/README.md](songs/README.md) explains the format, so you can change them or write your own.

### MIDI files from the SD card

For a song's real drum part, use a MIDI file. Drum tracks of famous songs are easy to find as `.mid` files.

1. Copy the files to a folder **`/DRUMS`** on the SD card (next to `/IR`). Up to 64 are listed, sorted by name.
2. `r` rescans the card, or restart. `drum song` lists them after the built-in songs, by file name.
3. `drum song <number or file name>` loads one, in a few tens of milliseconds, and sets the tempo from the file.

**What's read:**
- Standard MIDI files, format 0 or 1.
- The drum notes on MIDI channel 10, the General MIDI drum channel. If a file has none there, the track with the most notes in the drum range is used.
- The first tempo, and the time signatures, so meter changes come through.
- **Timing** is rounded to 24 steps per quarter note, which holds 16ths, 16th triplets and 32nds exactly.
- **Velocity** 1–127 becomes 1–9, which picks the recorded velocity layer as usual.
- **Limits:** up to 4,096 hits and 512 bars, roughly 150 bars of a busy song. A longer file is refused with a message.

**The General MIDI drum map onto the kit:**

| MIDI notes | Plays |
|---|---|
| 35, 36 | kick |
| 38, 40 (snare, rim shot), 39 (clap) | snare |
| 37 (side stick) | rim |
| 42, 44 (closed, pedal hi-hat) | closed hat |
| 46 | open hat |
| 49, 52, 55, 57 (crashes, china, splash) | crash |
| 51, 53, 59 (rides, ride bell) | ride |
| 47, 48, 50 (mid and high toms) | rack tom |
| 41, 43, 45 (floor and low toms) | floor tom |

Other notes (cowbell, tambourine, percussion) are skipped, and the load message says how many.

**Copyright:** a note-for-note MIDI file of a famous song is that song's arrangement. Keep such files on your own SD card for practice; don't add them to the repository. The built-in songs are original arrangements in the style of the originals.

## Dynamics, humanize and panning

**Velocity.** Every hit has a velocity from 1 to 9, written in the style's lanes. A hit's loudness is proportional to its velocity (9 = full, 5 = −5 dB, 3 = −10 dB). The built-in styles follow how a drummer plays:

- **Hi-hat:**
  - strongest on 1 and 3
  - a bit softer on the "&"
  - softer still under the snare on 2 and 4, where the hand lightens as the other one hits the snare
  - softest on the 16th "e" and "a"
- **Kick:** strongest on 1, a little less on 3 and on pushed beats.
- **Snare:** a touch harder on 4 than on 2.

**Velocity layers.** In the recorded kits, a hit plays the recording made nearest its velocity (2 for the kick, 3 for the snare, 4 for the closed hat). So a soft hat is a softer stroke, darker and with less attack, not just the loud one turned down. Between layers the level is scaled to the exact velocity.

**Humanize.** On top of that, every hit's velocity varies at random, like a real drummer's hands. Small changes are common and big ones rare (a triangular spread):

| Lanes | Spread |
|---|---|
| Hi-hats, ride | ±15 % |
| Kick, snare, rim, crash | ±8 % |
| Click (metronome, count-in) | none |

As the velocity moves, a hit sometimes crosses into the next layer, so repeated hats also vary a little in tone. Timing is never humanized: every hit stays sample-exact.

**Panning** is from the drummer's seat, kept moderate like a real kit in a mix:

| Lane | Pan |
|---|---|
| Kick, click | center |
| Snare, rim | 10 % left |
| Hi-hats | 35 % left |
| Crash | 30 % left |
| Ride | 40 % right |

The near side stays at full level and the far side is turned down: a hat 35 % left plays at 65 % (−3.7 dB) on the right. Centered drums are as loud as before, so panning never adds clipping. The pans apply to every kit, including synth.

## Kits and sounds

Every style plays with any kit (`drum kit`). The count-in and metronome click are always synthesized.

| # | Kit | What it is | Flash |
|---|---|---|---|
| 1 | synth | Synthesized voices | — |
| 2 | rock (default) | AVL "Red Zeppelin": 26" kick, Porkpie snare, Ludwig toms, Zildjian A ride, Zildjian K crash | 938 KB |
| 3 | pearl | AVL "Black Pearl": 22" kick, tight Pearl snare, Pearl toms, Sabian AAX ride | 543 KB (+ the hi-hats and crash it shares with rock) |

### rock (default)

A recorded acoustic kit: the **AVL Drumkits "Red Zeppelin"** by Glen MacArthur (a 26" Ludwig kick, a Porkpie snare, Sabian Rock hats, a Zildjian A ride and a Zildjian K crash). It's licensed CC BY-SA 3.0; see [kits/rock/CREDITS.md](kits/rock/CREDITS.md).

The source has 5 velocity layers per drum; layers 1–5 are velocities 1, 3, 5, 7 and 9 here.

| Lane | Sample (source layers → velocities) | Length kept | Size |
|---|---|---|---|
| Kick | Ludwig 26" kick (4, 5 → 7, 9) | 0.8 s | 138 KB |
| Snare | Porkpie snare (3, 4, 5 → 5, 7, 9) | 0.6 s | 155 KB |
| Rim | sidestick (4 → 7) | 0.3 s | 26 KB |
| Closed hat | Sabian Rock hat (2, 3, 4, 5 → 3, 5, 7, 9) | 0.3 s | 103 KB |
| Open hat | Sabian Rock hat, semi-open (4 → 7) | 0.6 s | 50 KB |
| Ride | Zildjian A 24" ride (4 → 7) | 2.0 s | 172 KB |
| Crash | Zildjian K 17" crash (4 → 7), dark and smooth, levelled 12 dB below the kick | 2.5 s | 215 KB |
| Rack tom | Ludwig 14" tom (4 → 7) | 0.45 s | 39 KB |
| Floor tom | Ludwig 16" floor tom (4 → 7) | 0.45 s | 39 KB |

All samples are 16-bit mono 44.1 kHz: **938 KB** of flash in all.

### pearl

The **AVL Drumkits "Black Pearl"**, by the same author and under the same license; see [kits/pearl/CREDITS.md](kits/pearl/CREDITS.md). It's smaller and tighter than rock, with a jazz-style kick and snare. It uses the same hi-hat and crash recordings as rock, and the firmware stores those only once.

| Lane | Sample (source layers → velocities) | Length kept | Size |
|---|---|---|---|
| Kick | Pearl 22" kick (4, 5 → 7, 9) | 0.8 s | 138 KB |
| Snare | Pearl snare (3, 4, 5 → 5, 7, 9) | 0.5 s | 129 KB |
| Rim | sidestick (4 → 7) | 0.3 s | 26 KB |
| Closed hat | Sabian Rock hat (2, 3, 4, 5 → 3, 5, 7, 9), shared with rock | 0.3 s | — |
| Open hat | Sabian Rock hat, semi-open (4 → 7), shared with rock | 0.6 s | — |
| Ride | Sabian AAX 20" ride (4 → 7) | 2.0 s | 172 KB |
| Crash | Zildjian K 17" crash (4 → 7), shared with rock | 2.5 s | — |
| Rack tom | Pearl 12" tom (4 → 7) | 0.45 s | 39 KB |
| Floor tom | Pearl 16" floor tom (4 → 7) | 0.45 s | 39 KB |

That's **543 KB** of flash beyond the shared hi-hats and crash.

### How the sampled kits play

- **Velocity:** a hit plays the layer nearest its velocity, scaled to the exact velocity (see [Dynamics, humanize and panning](#dynamics-humanize-and-panning)). Each sample was levelled to its velocity: at velocity 9 a drum peaks at its `peak` in `kit.txt`.
- **Balance:** the hi-hats sit about 18 dB below the kick and snare in the rock groove (measured on the energy of each).
- **Re-struck drums:** a kick, snare, rim or closed hat cuts its previous hit with a 2 ms fade.
- **Cymbals:** the open hat, ride and crash ring on under a new hit, up to two of each at once; the oldest fades out over 5 ms. A closed hat chokes a ringing open hat in 5 ms, like a real hi-hat.
- **Tails:** the long cymbal tails were cut and faded to save flash, the crash at 2.5 s and the rides at 2 s.
- **Loudness:** both recorded kits are within about 2 dB of the synth kit, so switching kits barely changes the balance with your guitar.

### synth

All voices synthesized, in the spirit of the classic analog drum machines:

| Voice | How it's made | Decay (to −60 dB) |
|---|---|---|
| Kick | A sine whose pitch falls from 160 to 50 Hz, plus a 12 ms noise click for the beater | 0.6 s |
| Snare | A 185 Hz body plus noise band-limited to 1–9 kHz (the snares) | body 0.2 s, snares 0.3 s |
| Rim (not in the built-in styles) | Cross-stick: 1.7 kHz and 830 Hz sines plus a high-passed noise tick | 60 ms |
| Closed / open hat | TR-808-style metal (six square waves at 205–800 Hz, inharmonic) mixed with noise, then high-passed at 7 kHz | 90 ms / 0.7 s; a closed hat cuts off a ringing open one, like a real hi-hat |
| Ride (not in the built-in styles) | The same metal and noise, band-passed around 4.5 kHz | 2.5 s |
| Crash (crash accents) | The same metal with more noise, high-passed at 4 kHz, kept well below the drums | 1.8 s |
| Rack / floor tom (songs) | Sines falling from 1.5× to 150 Hz / 92 Hz, plus a short noise attack | 0.45 s / 0.6 s |
| Click | Metronome and count-in: 2 kHz on accents, 1.2 kHz otherwise | 50 ms |

- **Bus:** in every kit the voices are summed into the drum bus (see [Drum bus](#drum-bus)), so even every voice at once can't exceed full scale.
- The rim and ride are there for your own styles: add a lane for them in `DrumPatterns.cpp`.

## Adding a kit

A kit is a folder in `kits/` with a `kit.txt` and its WAVs. Every kit there is compiled into the firmware and listed by `drum kit`.

1. **Write `kits/<name>/kit.txt`.** Use [kits/rock/kit.txt](kits/rock/kit.txt) as the template. It has:
   - `name` (at most 9 characters) and `title` (at most 13, shown on the display).
   - `order`: its place in the kit list. Kit numbers are saved in the settings, so give a new kit the next number to keep the existing ones in place.
   - `source`: the base URL of the original samples.
   - One line per sample:
     - the lane (`kick snare rim hatc hato ride crash`)
     - the velocity it stands for (1–9)
     - the WAV to write and the source file
     - the length to keep and the fade at its end
     - the lane's peak level at velocity 9, in dBFS
   - At most 4 samples per lane. A lane with no samples uses the synthesized voice.
2. **Build the WAVs from the originals:**

   ```
   brew install flac                          # once, for FLAC sources
   python3 tools/import_kit.py kits/<name>
   ```

   For each sample this:
   - Downloads it.
   - Trims the silence before the hit, keeping 0.5 ms, faded in.
   - Cuts the tail at −60 dB or at the length given, with a raised-cosine fade.
   - Levels it to the lane's peak × velocity / 9.
   - Writes 16-bit mono 44.1 kHz WAVs.

   Sources must be 44.1 kHz (16 or 24-bit, mono or stereo).
3. **Build and listen:**
   - `pio run` compiles the kit in: `tools/make_kits.py` regenerates `lib/irdsp/src/DrumKitData.cpp` whenever a kit changes. A sample that is identical in two kits (same recording, same settings) is stored once.
   - `make -C test/host render` writes every style with every kit to `test/host/out/drums_*_<kit>.wav`.
   - `test_drums` checks that each sample starts and ends at zero, that its hit starts within 2 ms, and that the loudest style stays below clipping.

**Flash budget:** the kits use 1,480 KB (rock 938 KB, pearl 543 KB beyond what it shares), and about 310 KB of flash is left. Keep some of it for future code such as the OLED display. A new kit would mean trimming tails or layers elsewhere (88 KB per second of sound). Cymbal tails are the expensive part; sharing samples between kits is free.

## Drum bus

Recorded drums are spiky: a hit is loud for a few milliseconds and then decays, so raw drums average 17–19 dB below their peaks. A distorted guitar is dense and loud all the time, so raw drums disappear next to it. The drum bus gives them level and lets you choose how compressed they sound:

```
voices (stereo) -> bus gain -> natural ----------+
                            -> compressor -------+-- Comp blend --> peak limiter -> drum volume -> added to the outputs
```

- **Bus gain:** brings the drums up to an average of about −17 to −18 dBFS RMS at volume 100, with their natural dynamics.
- **Comp** (`drum comp 0`–`100`, default 40) blends in a compressed copy of the bus. This is parallel, or "New York", compression, as engineers use on drums.
  - **0:** natural drums. The hits keep all their snap, and the limiter alone keeps the peaks in check.
  - **40** (default): the hits stay sharp, with more body and sustain between them.
  - **100:** fully compressed: flat, dense and sustained, the decays and cymbals pulled up.

  Comp changes the character, not the level: all three settings come out within about 2 dB of each other. Use the drum volume for level.
- **Compressor** (the compressed copy):
  - Threshold −24 dBFS, ratio 4:1, soft 6 dB knee.
  - Attack 1 ms.
  - Release 200 ms, slow enough not to pump.
  - +16 dB makeup.
  - Both channels share one detector, so the stereo image doesn't shift.
- **Peak limiter:** keeps the bus at −0.5 dBFS. It's smooth rather than clipping: it sees each peak coming and turns down over 1.5 ms before it arrives. At the default Comp it hardly ever acts.
- **Lookahead:** both look 1.5 ms ahead, because the drums are delayed by 1.5 ms. That doesn't matter for a drum machine, since there's nothing to play in time with but the drums themselves.
- **Measured** (rock kit, every style, volume 100):

  | Comp | Average (RMS) |
  |---|---|
  | 0 | −18.1 dBFS |
  | 40 | −17.1 dBFS |
  | 100 | −16.7 dBFS |

  Peaks stay at −0.5 dBFS.
- **The volume comes after the bus**, so the drums sound the same at any volume.
- **Idle:** when the drums go idle the bus resets, and the output is exactly the guitar again.

The compressor settings are fixed voicing (constants in `DrumMachine.cpp`), like the delay's. To hear the difference on a computer, `make -C test/host render` writes `test/host/out/drums_02_rock_rock_comp0.wav`, `comp40.wav` and `comp100.wav`.

## Timing, levels, CPU and memory

- **Timing is sample-accurate.** Steps fire at their exact sample inside an audio block. A fractional sample counter keeps odd tempos such as 97 BPM from drifting: after 16 bars the beats are still within one sample (23 µs) of the ideal grid.
- **Changes are musical:**
  - a tempo change applies from the next step
  - a style change and each trainer step from the next bar line
  - stopping cuts the sequencer at once, but the last hits ring out
- **Levels:** see [Drum bus](#drum-bus). At volume 100 the drums peak at −0.5 dBFS with an average (RMS) of about −17 dBFS for the recorded kits at the default Comp; at the default 60 everything is about 7.6 dB lower. Set the drum volume against your guitar, and watch `s` for clipped samples, because the drums add to the guitar signal at the output.
- **CPU** while playing, measured on the Teensy 4.0 with 32-sample blocks:
  - rock and pearl kits: about 2.5 % at 100 BPM, including the drum bus (stereo, humanized, compressed); songs about 3 %.
  - synth kit: up to 3.3 % at 240 BPM.
  - Once stopped and silent it uses almost nothing (0.1 %), and the output is the guitar alone, bit for bit.
- **Sample reads:** samples are read straight from flash in the audio interrupt, which needs only a few percent of the flash's bandwidth. Switching IRs while the drums play peaks at the same 55 % CPU as without them, with no dropouts.
- **Memory:**
  - RAM1: about 3 KB of data (two pattern buffers, the synth voices' state, 16 sample voices and the bus's 1.5 ms lookahead) and about 6.8 KB of code (the real-time part).
  - RAM2: 22 KB for a song loaded from the SD card (hits and bar table) and the `/DRUMS` file list.
  - Flash: the kits' samples (1,480 KB for rock and pearl), the built-in songs (about 9 KB), setup, the MIDI reader, and the style and kit tables.

## Defaults (`config.h`)

| Define | Default | Meaning |
|---|---|---|
| `DEFAULT_DRUM_STYLE` | 2 | Style number, 1-based (2 = rock) |
| `DEFAULT_DRUM_TEMPO` | 100 | BPM |
| `DEFAULT_DRUM_VOLUME` | 60 | 0–100 |
| `DEFAULT_DRUM_COUNT_IN` | 2 | Count-in bars (0 = off, up to 4) |
| `DEFAULT_DRUM_KIT` | 2 | Kit number, 1-based (1 = synth, 2 = rock, 3 = pearl) |
| `DEFAULT_DRUM_CRASH` | 4 | Crash every 4 bars (0 = off, or 2, 4, 8, 16) |
| `DEFAULT_DRUM_COMP` | 40 | Drum-bus compression, 0 = natural .. 100 |
| `DEFAULT_TRAINER_ON` | 0 | Trainer off |
| `DEFAULT_TRAINER_STEP` / `_EVERY` / `_TARGET` | 5 / 4 / 140 | +5 BPM every 4 bars, up to 140 BPM |

These are used when the EEPROM is blank, and when settings saved by older firmware are upgraded (the kit and crash settings arrived with `"IRL6"`).

## Code and tests

| File | Contents |
|---|---|
| `lib/irdsp/src/DrumMachine.{h,cpp}` | Synth and sample voices, sequencer, count-in, trainer, crash accents. Portable C++, no Arduino. |
| `lib/irdsp/src/DrumPatterns.cpp` | The style table (in flash on the Teensy). |
| `lib/irdsp/src/DrumKit.h` | The kit format: per lane, up to 3 samples by hit level. |
| `lib/irdsp/src/DrumKitData.cpp` | Generated from `kits/*/` by `tools/make_kits.py` (not in git): the samples and kit table, in flash. |
| `kits/rock/`, `kits/pearl/` | The recorded kits: WAVs, `kit.txt`, credits. |
| `lib/irdsp/src/DrumSong.h` | The song format: hits on a 24-per-quarter grid and a bar table. |
| `songs/*.txt`, `tools/make_songs.py`, `lib/irdsp/src/DrumSongData.cpp` | The built-in songs, compiled (not in git) on every build. |
| `lib/irdsp/src/MidiDrums.{h,cpp}` | Reads a MIDI file's drum part into a song (in flash on the Teensy). |
| `src/DrumSongs.{h,cpp}` | The song list (built-in + `/DRUMS`), loading from the SD card, selecting, the loop. |
| `tools/import_kit.py`, `tools/make_kits.py`, `tools/pio_kits.py` | Build a kit's WAVs from its sources; compile the kits in (run by PlatformIO and the host Makefile). |
| `src/AudioConvolverIR.h` | Runs the drums each block and adds them to both outputs. |
| `src/AudioGraph.cpp`, `src/SerialUI.cpp`, `src/Settings.cpp` | Setters, the `drum` and `bpm` commands, EEPROM. |
| `lib/oledui/src/Menu.cpp`, `Screens.cpp` | The Drums menu page and the home-screen row. |

`make -C test/host test` runs `test_drums` and `test_midi`. They check:
- **Timing:** the beat grid at 120 and 97 BPM over 16 bars.
- **Count-in:**
  - With 1 bar: four clicks, then the first kick exactly one bar later.
  - With 2 bars: eight clicks, then the groove at bar 3.
  - With the trainer: 2 count-in bars at each new tempo, the groove resuming right after, none once at the target, and crash accents restarting after each count-in.
- **Speed trainer:** bar lengths step 100, 100, 105, 105, 110 BPM and then hold.
- **Changes:** a style change lands on the next bar and a tempo change on the next step.
- **Shuffle:** the hits land on the triplet grid.
- **Practice beats:** in the offbeat 8ths, offbeat 16ths and triplets styles the hat lands only on its offbeat positions, never on the beat, with the kick on 1 and 3 and the snare on 2 and 4.
- **Stop:** no hits after stopping, and exact silence and idle once the sound has died away.
- **Every style:** its lanes fit, it stays below full scale and it is clearly audible.
- **Drum bus:**
  - At Comp 0, 40 and 100 the level stays within 2 dB.
  - At the default it's in range: −20 to −14 dBFS for the recorded kits, −22 to −15 for synth.
  - Peaks stay at the limiter's −0.5 dBFS ceiling, and Comp 0 keeps the natural crest factor (at least 15.5 dB).
  - After stopping, the output is exactly silent and the machine idle.
- **Sampled kits** (with a test kit made of impulses and steady values):
  - Every hit starts at its exact step sample, at 120 and 97 BPM.
  - Each hit plays the velocity layer nearest its velocity, scaled by hit / layer velocity.
  - A re-struck snare's old hit fades within 2 ms.
  - A closed hat chokes the open hat smoothly within 5 ms.
  - At most two cymbal hits ring at once.
  - A kit change fades the old samples out.
  - The machine goes idle once the last sample has ended.
- **Crash accents:**
  - On beat 1 of bars 1, 5, 9 with every 4; every 2 and every 16 likewise; none when off.
  - In place of the hat, with the kick kept.
  - Not in the count-in or the metronome style.
  - A change waits for the next bar.
  - The synth crash rings and dies away.
- **Dynamics, humanize and panning:**
  - The rock hats play 9, 6, 5, 6, 8, 6, 5, 6, the kick 9/8/7 and the snare 8/9.
  - The humanize spread measures about ±8 % on the kick and ±15 % on the hats, and none when turned off.
  - The hats and crash are panned left and the kick centered, by the exact amounts.
- **The real kits:**
  - Each sample starts and ends at zero with its hit within 2 ms.
  - Layers: 2 kick, 3 snare, 4 closed hat, each levelled to its velocity.
  - The hats sit more than 6 dB below the kick and snare.
  - Every style with each recorded kit stays below clipping at volume 100, and within 3 dB of the synth kit's loudness.

- **Songs:**
  - Hits land on their exact sample on the 24-per-quarter grid, including a 32nd and a 16th triplet.
  - Bars in 4/4 and 3/4, wrap at the end.
  - A loop starts at its first bar and jumps back after its last.
  - The count-in plays before the song.
  - No crash accents in song mode, and the style comes back after `drum song off`.
  - The built-in songs are 32 bars with valid hits and bar tables, and stay below the drum bus's ceiling.
- **Toms:** every recorded kit has a rack and a floor tom.
- **MIDI files** (`test_midi`, files built in the test):
  - Format 0 and 1, 96 and 480 PPQ, tempo, running status.
  - Note-on with velocity 0 counts as a note-off.
  - Meter changes become the bar table.
  - The GM map and velocities; a 32nd and off-grid notes rounded to the grid.
  - Without channel 10, the busiest drum track is used.
  - Duplicate hits merge.
  - Too many notes, no drums and garbage give errors, and a truncated file gives the hits before the cut.
  - 200 randomly corrupted files never crash or overrun.

To listen on your computer, `make -C test/host render` writes, in stereo:
- 8 bars of every style with every kit, with a 2-bar count-in and a crash every 4 bars, to `test/host/out/drums_<n>_<style>_<kit>.wav`
- every built-in song once through with the rock kit, to `test/host/out/song_<name>.wav`

## Ideas for later

None of these are built yet. They're practice features worth adding next:

1. **Swing amount:** a 0–66 % swing control, so offbeat 8ths or rock can go from straight, to a light swing, to a full shuffle. The sequencer would delay every second subdivision by the swing amount.
2. **Gap trainer:** the drums drop out for 1–2 bars out of every 4 (or another chosen ratio), then come back. If you're still on the grid when they return, your timing is solid.
3. **Fills for the styles:** a short fill at the end of every 4 or 8 bars, before the crash accent. The toms now exist (the songs use them); the styles would need fill bars.
4. **12-bar blues mode:** the shuffle with a bar counter (1–12) in `s` and on the display, plus a fill on the turnaround (bars 11–12), for practicing blues changes.
5. **Tap tempo:** tap `t` over serial, or later a key on the display, a few times to set the tempo from a song.
6. **More meters:** a 3/4 waltz and a 12/8 slow blues, a very common practice groove. Both fit the existing style format (3 beats x 4 steps, and 4 beats x 3 steps).
