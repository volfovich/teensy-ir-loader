# Drum machine

A small drum machine to practice along with. It plays one of eight built-in one-bar styles, or a whole **song**, at 40–240 BPM. A count-in, a speed trainer and crash accents are built in.

- **Songs:** multi-bar drum parts with verses, choruses and fills:
  - five built-in 32-bar songs in the style of classic rock songs
  - any General MIDI drum file (`.mid`) you copy to `/DRUMS` on the SD card
  - loop any section (bars A–B) to practice it

- **Two kits:**
  - **rock** (default): a recorded acoustic kit: our own kick, snare and closed hi-hat, with the cymbals, rim and toms from the AVL Drumkits "Red Zeppelin". Its samples are compiled into the firmware and played straight from flash.
  - **synth**: every sound generated in code, in the spirit of the classic analog drum machines.
- **Bass:** a plucked-string electric bass plays along: the songs' bass lines, the bass part of MIDI files, and a line for each style in a key you choose. See [Bass](#bass).
- **Crash accents:** a crash cymbal on the first beat of every 2, 4, 8 or 16 bars, so you always know where the phrase starts.
- **Like a drummer:** hits have velocities 1–9 that follow a drummer's hands, vary at random a little (more on the hats), and are panned like a real kit.
- **Kept apart from the guitar:** the drums and bass are mixed into the outputs (line out and headphones), in stereo, **after** the IR, delay and reverb. They never go through your guitar effects, and they have their own volumes.

## Contents

- [Quick start](#quick-start)
- [Controls](#controls)
- [Count-in and speed trainer](#count-in-and-speed-trainer)
- [Crash accents](#crash-accents)
- [Styles](#styles)
- [Songs](#songs)
- [Bass](#bass)
- [Dynamics, humanize and panning](#dynamics-humanize-and-panning)
- [Kits and sounds](#kits-and-sounds)
- [Adding a kit](#adding-a-kit)
- [Drum bus](#drum-bus)
- [Timing, levels, CPU and memory](#timing-levels-cpu-and-memory)
- [Defaults](#defaults)
- [Code and tests](#code-and-tests)
- [Ideas for later](#ideas-for-later)

## Quick start

Over USB serial at 115200:

```
drum style          list the styles
drum style shuffle  choose one (by name or number)
drum kit rock       the recorded kit (or: drum kit synth)
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
| `drum kit <n\|name>` | 1–2 or a name | rock | Changes the kit at once. Hits still ringing fade out over 5 ms. |
| `drum crash <0\|2\|4\|8\|16>` | off, 2, 4, 8, 16 bars | 4 | A crash on beat 1 of every N bars (see [Crash accents](#crash-accents)). `drum crash` alone steps to the next. |
| `drum vol` | 0–100 | 60 | Drum volume: 100 = 0 dB, 60 ≈ −8 dB, 0 = silent. |
| `drum comp` | 0–100 | 40 | Drum-bus compression: 0 = natural, 100 = squashed (see [Drum bus](#drum-bus)). `drum comp` alone shows it. |
| `drum count [0-4]` | off, 1–4 bars | 2 bars | Bars of count-in clicks before the groove starts, and after every speed-trainer tempo step. `drum count` alone steps to the next (off, 1 … 4, off). |
| `drum train` | toggle | off | Speed trainer on/off. |
| `drum train <step> <every> <target>` | 1–20 BPM, 1–16 bars, 40–240 BPM | 5, 4, 140 | Turns the trainer on with these settings. |
| `train step` / `train every` / `train target` | as above | 5, 4, 140 | Sets one of them, on or off. |
| `bass` / `bass on` / `bass off` | toggle | on | The bass with the drums (see [Bass](#bass)). |
| `bass vol` | 0–100 | 70 | Bass volume, on the drum volume's curve (100 = 0 dB). |
| `bass key <C..B>` | a note name: `E`, `F#`, `Bb` … | E | The styles' key. Songs keep their own notes. |
| `bass tone` | 0–100 | 50 | 0 = dark and round (flatwound), 100 = bright (roundwound, picked). |

`s` shows the drums' state and settings, the current tempo (which rises with the trainer), the bar number and the drums' share of the CPU, for example:

```
Drums:     playing blues shuffle, 105 BPM, bar 9, kit rock, crash 4 bars, volume 60%, comp 40%, count-in 2 bars, trainer +5 BPM every 4 bars to 140, bass in E, volume 70%
```

**On the OLED**, the menu has a **Drums** page:
- Play, **Bass**, Style, **Song**, **Kit**, **Crash**, Tempo, Volume, **Comp**, Count-in, and **Trainer**.
- **Bass** opens its own page: On (push toggles it), Volume, Key (by name) and Tone. The Bass row on the Drums page shows ON/OFF.
- **Trainer** opens its own page: On, Step, Every and Target. All four are always shown, so the trainer can be set up before it's switched on. The Trainer row shows ON/OFF.
- **Loop from** and **Loop to** appear under Song while a song is selected (from "off" = the whole song).
- Style, Song and Kit are chosen by turning the encoder and are shown by name.
- Each push on Crash steps it: off, 2, 4, 8, 16 bars.
- Each push on Count-in steps it: off, 1, 2, 3, 4 bars.
- The home screen shows the tempo big, the style's (or song's) short name and a box per beat. The tempo can be edited right there: turn past DRM, push, turn. **CONFIRM** starts and stops the drums from the home screen, and pushing the **DRM** block opens the Drums page.

See [DISPLAY.md](DISPLAY.md#home) and the renders `screenshots/02d_home_drums.png`, `02e_home_tempo.png`, `02f_home_tempo_edit.png`, `12_menu_drums.png`, `13_edit_drum_style.png`, `14_menu_trainer.png`, `15_edit_trainer_target.png`, `16_edit_drum_kit.png`, `19_menu_drums_song.png`, `20_menu_drums_bass.png`, `21_menu_bass.png` and `22_edit_bass_key.png`.

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
- **Every kit:** the synth kit has a synthesized crash, the rock kit a recorded one. Each crash rings out under the next one; at most two ring at once.

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
- **The bass part**, if the file has one (see [Bass](#bass)): the load message names its channel, or says there is none.

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

## Bass

A plucked-string electric bass plays with the drums: the bass line of the song, or a line for the style in the key you choose. It is synthesized, not sampled (`lib/irdsp/src/PluckBass.{h,cpp}`).

**The sound:**
- A Karplus-Strong string: a delay line one period long, with an allpass for exact tuning (within 0.1 cent from B0 to G3) and a low-pass in the loop that keeps the same number of harmonics on every note (Tone sets how many), so the higher ones die within a fraction of a second, as on a real bass.
- The pluck is a triangle (the string pulled at 1/5 of its length), band-limited to what the string keeps, fed into the string during its first period with a 1.5 ms attack. Harder notes are louder and a little brighter.
- An open E1 rings about 3 s, higher notes less. A mute fades the note out smoothly (−60 dB in 80 ms), like lifting the finger; a new note fades the old one in 30 ms. A dead note is a damped thump.
- A gentle saturation (1–2 % at normal levels) for warmth.
- Monophonic: one note at a time, like a bass line.

**What it plays:**
- **Songs:** each built-in song has a simple bass line on the chord roots (`bass` lines in `songs/*.txt`, see [songs/README.md](songs/README.md)).
- **MIDI files:** the bass part, if the file has one: the channel playing a General MIDI bass (programs 33–40) with the most notes; otherwise, when the drums are on channel 10, the channel with most of its notes below C3. It is reduced to one line: the lowest note of a chord, the sounding note's note-off as a mute, notes outside B0..G3 moved by octaves into range. Up to 4,096 bass events; a longer bass part is left out and the drums still play.
- **Styles:** a line for each, in the bass key (`bass key`, default E; the root sounds from E1 to D#2):

  | Style | Bass line |
  |---|---|
  | metronome | none |
  | rock | driving 8ths on the root, the fifth and the octave at the end of the bar |
  | offbeat 8ths | 8ths on the root, the fifth to turn around |
  | offbeat 16ths | a 16th feel with dead-note ghosts |
  | triplets | root, root, fifth, root on the beats |
  | pop | long roots with a push, the fifth at the end |
  | blues shuffle | a root-fifth-sixth-seventh boogie, swung |
  | 6/8 ballad | root and fifth, a dotted quarter each |

**When it plays:** on the drums' clock, sample-exact; not during count-ins (also the trainer's); it follows a song's loop; it stops (muted) when the drums stop, and a style without a line (the metronome) mutes it at the bar line.

**The mix:** mono, in the center, added to the drum bus **after** the compressor (the kick doesn't pump it) and before the limiter, which keeps drums + bass below −0.5 dBFS. With drums and bass both at 100, the built-in songs peak at −0.7 dBFS.

## Dynamics, humanize and panning

**Velocity.** Every hit has a velocity from 1 to 9, written in the style's lanes. A hit's loudness is proportional to its velocity (9 = full, 5 = −5 dB, 3 = −10 dB). The built-in styles follow how a drummer plays:

- **Hi-hat:**
  - strongest on 1 and 3
  - a bit softer on the "&"
  - softer still under the snare on 2 and 4, where the hand lightens as the other one hits the snare
  - softest on the 16th "e" and "a"
- **Kick:** strongest on 1, a little less on 3 and on pushed beats.
- **Snare:** a touch harder on 4 than on 2.

**Velocity layers.** A recorded lane can hold up to 4 recordings made at different velocities, and a hit plays the one nearest its velocity, so a soft hit can be a softer stroke, darker and with less attack, not just the loud one turned down. Between layers the level is scaled to the exact velocity. The rock kit has one recording per lane for now, scaled to each hit's velocity.

**Humanize.** On top of that, every hit's velocity varies at random, like a real drummer's hands. Small changes are common and big ones rare (a triangular spread):

| Lanes | Spread |
|---|---|
| Hi-hats, ride | ±15 % |
| Kick, snare, rim, crash | ±8 % |
| Click (metronome, count-in) | none |

With several layers, a hit sometimes crosses into the next layer as the velocity moves, so repeated hits also vary a little in tone. Timing is never humanized: every hit stays sample-exact.

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
| 2 | rock (default) | Our own kick, snare and closed hat; AVL "Red Zeppelin" rim, open hat, Ludwig toms, Zildjian A ride, Zildjian K crash | 666 KB |

### rock (default)

A recorded acoustic kit:
- **Kick, snare and closed hi-hat:** our own samples, in `kits/rock/originals/`. They are used as they are, at their own levels (`peak -` in `kit.txt`), with only the last few milliseconds below −60 dB cut and a 20 ms fade at the end.
- **The rest:** the **AVL Drumkits "Red Zeppelin"** by Glen MacArthur (a Sabian Rock hat, a Zildjian A ride, a Zildjian K crash, Ludwig toms). They're licensed CC BY-SA 3.0; see [kits/rock/CREDITS.md](kits/rock/CREDITS.md). The source has 5 velocity layers per drum; layers 1–5 are velocities 1, 3, 5, 7 and 9 here.

| Lane | Sample (source layers → velocities) | Length kept | Size |
|---|---|---|---|
| Kick | own sample `Kick1.wav` (→ 9), peaking at −4.9 dBFS | 0.27 s | 26 KB |
| Snare | own sample `Snare1.wav` (→ 9), peaking at −4.2 dBFS | 0.28 s | 26 KB |
| Rim | sidestick (4 → 7) | 0.3 s | 27 KB |
| Closed hat | own sample `Closed1.wav` (→ 9), peaking at −14.1 dBFS | 0.28 s | 26 KB |
| Open hat | Sabian Rock hat, semi-open (4 → 7) | 0.6 s | 55 KB |
| Ride | Zildjian A 24" ride (4 → 7) | 2.0 s | 188 KB |
| Crash | Zildjian K 17" crash (4 → 7), dark and smooth, peaking at −16 dBFS | 2.5 s | 234 KB |
| Rack tom | Ludwig 14" tom (4 → 7) | 0.45 s | 42 KB |
| Floor tom | Ludwig 16" floor tom (4 → 7) | 0.45 s | 42 KB |

All samples are 16-bit mono 48 kHz, the firmware's rate (resampled from 44.1 kHz sources): **666 KB** of flash in all.

### How the sampled kits play

- **Velocity:** a hit plays the layer nearest its velocity, scaled to the exact velocity (see [Dynamics, humanize and panning](#dynamics-humanize-and-panning)). The AVL samples were levelled to their velocity: at velocity 9 a drum peaks at its `peak` in `kit.txt`. Our own samples keep their level and play at it at velocity 9.
- **Balance:** the hi-hats sit about 13 dB below the kick and snare in the rock groove (measured on the energy of each).
- **Re-struck drums:** a kick, snare, rim or closed hat cuts its previous hit with a 2 ms fade.
- **Cymbals:** the open hat, ride and crash ring on under a new hit, up to two of each at once; the oldest fades out over 5 ms. A closed hat chokes a ringing open hat in 5 ms, like a real hi-hat.
- **Tails:** the long cymbal tails were cut and faded to save flash, the crash at 2.5 s and the rides at 2 s.
- **Loudness:** the rock kit is about 5 dB quieter than the synth kit (RMS −23 against −18 dBFS at volume 100), because our own kick and snare are short and kept at their own level. Turn the drum volume up to make up for it: at the default 60 (≈ −8 dB) there's 8 dB of headroom.

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

A kit's lanes can also be swapped one by one: rock's kick, snare and closed hat are our own WAVs this way.

1. **Write `kits/<name>/kit.txt`.** Use [kits/rock/kit.txt](kits/rock/kit.txt) as the template. It has:
   - `name` (at most 9 characters) and `title` (at most 13, shown on the display).
   - `order`: its place in the kit list. Kit numbers are saved in the settings, so give a new kit the next number to keep the existing ones in place.
   - `source`: the base URL of the original samples.
   - One line per sample:
     - the lane (`kick snare rim hatc hato ride crash`)
     - the velocity it stands for (1–9)
     - the WAV to write and the source file: a file name under `source`, or your own WAV in the kit folder (`originals/Kick1.wav`)
     - the length to keep and the fade at its end (keep the fade shorter than the sample: it fades the last `fade` seconds)
     - the lane's peak level at velocity 9, in dBFS, or `-` to keep the sample's own level
   - At most 4 samples per lane. A lane with no samples uses the synthesized voice.
2. **Build the WAVs from the originals:**

   ```
   brew install flac                          # once, for FLAC sources
   python3 tools/import_kit.py kits/<name>
   ```

   For each sample this:
   - Downloads it (unless it's a file in the kit folder).
   - Trims the silence before the hit, keeping 0.5 ms, faded in.
   - Cuts the tail at −60 dB or at the length given, with a raised-cosine fade.
   - Levels it to the lane's peak × velocity / 9 (unless the peak is `-`).
   - Writes 16-bit mono WAVs at the firmware's rate (`FS` in `tools/make_kits.py`, 48 kHz).

   Sources can be at any rate (16 or 24-bit, mono or stereo); other rates are resampled (Kaiser-windowed sinc). The build stops if the kits don't match the firmware's rate.
3. **Build and listen:**
   - `pio run` compiles the kit in: `tools/make_kits.py` regenerates `lib/irdsp/src/DrumKitData.cpp` whenever a kit changes.
   - `make -C test/host render` writes every style with every kit to `test/host/out/drums_*_<kit>.wav`.
   - `test_drums` checks that each sample starts and ends at zero, that its hit starts within 2 ms, and that the loudest style stays below clipping.

**Flash budget:** the rock kit uses 666 KB, and about 1,100 KB of flash is left (96 KB per second of sound). Keep some of it for future code. Cymbal tails are the expensive part.

## Drum bus

Recorded drums are spiky: a hit is loud for a few milliseconds and then decays, so raw drums average 17–19 dB below their peaks. A distorted guitar is dense and loud all the time, so raw drums disappear next to it. The drum bus gives them level and lets you choose how compressed they sound:

```
voices (stereo) -> bus gain -> natural ----------+
                            -> compressor -------+-- Comp blend -> x drum volume --+
                                            bass (mono) -> x bass volume ----------+-> peak limiter -> added to the outputs
```

- **Bus gain:** brings the drums up to an average of about −18 dBFS RMS at volume 100 for the synth kit (−23 for rock), with their natural dynamics.
- **Comp** (`drum comp 0`–`100`, default 40) blends in a compressed copy of the bus. This is parallel, or "New York", compression, as engineers use on drums.
  - **0:** natural drums. The hits keep all their snap, and the limiter alone keeps the peaks in check.
  - **40** (default): the hits stay sharp, with more body and sustain between them.
  - **100:** fully compressed: flat, dense and sustained, the decays and cymbals pulled up.

  Comp changes the character more than the level: all three settings come out within about 2 dB of each other for synth, 3 dB for rock. Use the drum volume for level.
- **Compressor** (the compressed copy):
  - Threshold −24 dBFS, ratio 4:1, soft 6 dB knee.
  - Attack 1 ms.
  - Release 200 ms, slow enough not to pump.
  - +16 dB makeup.
  - Both channels share one detector, so the stereo image doesn't shift.
- **Peak limiter:** keeps the bus at −0.5 dBFS. It's smooth rather than clipping: it sees each peak coming and turns down over 1.3 ms before it arrives. At the default Comp it hardly ever acts.
- **Lookahead:** both look 1.3 ms ahead (64 samples), because the drums are delayed by 1.3 ms. That doesn't matter for a drum machine, since there's nothing to play in time with but the drums themselves.
- **Measured** (every style, volume 100):

  | Comp | rock (RMS) | synth (RMS) |
  |---|---|---|
  | 0 | −24.7 dBFS | −17.2 dBFS |
  | 40 | −23.0 dBFS | −18.4 dBFS |
  | 100 | −21.7 dBFS | −19.1 dBFS |

  Peaks stay at or under −0.5 dBFS (about −1.4 dBFS for rock).
- **The drum volume comes after the compressor**, so the drums sound the same at any volume; the limiter after it protects drums and bass together and only acts at the loud end.
- **Idle:** when the drums go idle the bus resets, and the output is exactly the guitar again.

The compressor settings are fixed voicing (constants in `DrumMachine.cpp`), like the delay's. To hear the difference on a computer, `make -C test/host render` writes `test/host/out/drums_02_rock_rock_comp0.wav`, `comp40.wav` and `comp100.wav`.

## Timing, levels, CPU and memory

- **Timing is sample-accurate.** Steps fire at their exact sample inside an audio block. A fractional sample counter keeps odd tempos such as 97 BPM from drifting: after 16 bars the beats are still within one sample (23 µs) of the ideal grid.
- **Changes are musical:**
  - a tempo change applies from the next step
  - a style change and each trainer step from the next bar line
  - stopping cuts the sequencer at once, but the last hits ring out
- **Levels:** see [Drum bus](#drum-bus). At volume 100 the drums peak at −0.5 dBFS with an average (RMS) of about −18 dBFS for synth and −23 dBFS for rock at the default Comp; at the default 60 everything is about 7.6 dB lower. Set the drum volume against your guitar, and watch `s` for clipped samples, because the drums add to the guitar signal at the output.
- **CPU** while playing, measured on the Teensy 4.0 with 32-sample blocks:
  - rock kit: about 2.5 % at 100 BPM, including the drum bus (stereo, humanized, compressed); songs about 3 %.
  - synth kit: up to 3.3 % at 240 BPM.
  - the bass adds about 1.2 % (drums + bass 4.4 % at 240 BPM). Starting a note takes about 3 µs: the pluck is fed into the string as it plays, because building a whole period at once took up to 0.7 ms in the audio interrupt (a block is 0.73 ms). Drums and bass at 240 BPM with IR switches peak at 55–58 % CPU, no dropouts.
  - Once stopped and silent it uses almost nothing (0.1 %), and the output is the guitar alone, bit for bit.
- **Sample reads:** samples are read straight from flash in the audio interrupt, which needs only a few percent of the flash's bandwidth. Switching IRs while the drums play peaks at the same 55 % CPU as without them, with no dropouts.
- **Memory:**
  - RAM1: about 3 KB of data (two pattern buffers, the synth voices' state, 16 sample voices and the bus's 1.3 ms lookahead) and about 6.8 KB of code (the real-time part).
  - RAM2: 22 KB for a song loaded from the SD card (hits and bar table) and the `/DRUMS` file list, 16 KB for its bass part, and 13.6 KB for the bass's strings and buffers.
  - The bass's real-time code is about 1 KB of RAM1; the drum machine's once-per-hit work (starting a hit, a bar line, a bass note) runs from flash to make room for it.
  - Flash: the rock kit's samples (666 KB), the built-in songs (about 9 KB), setup, the MIDI reader, and the style and kit tables.

## Defaults

The defaults a blank EEPROM starts with are the `def` column of the parameter table, `lib/oledui/src/ParamDefs.cpp`: style rock, 100 BPM, volume 60, count-in 2 bars, kit rock, a crash every 4 bars, compression 40, trainer off (+5 BPM every 4 bars, up to 140), bass on, volume 70, key E, tone 50. A setting that firmware before it didn't store (or an invalid one) gets its default without resetting the others.

## Code and tests

| File | Contents |
|---|---|
| `lib/irdsp/src/DrumMachine.{h,cpp}` | Synth and sample voices, sequencer, count-in, trainer, crash accents. Portable C++, no Arduino. |
| `lib/irdsp/src/DrumPatterns.cpp` | The style table (in flash on the Teensy). |
| `lib/irdsp/src/DrumKit.h` | The kit format: per lane, up to 3 samples by hit level. |
| `lib/irdsp/src/DrumKitData.cpp` | Generated from `kits/*/` by `tools/make_kits.py` (not in git): the samples and kit table, in flash. |
| `kits/rock/` | The recorded kit: WAVs, `kit.txt`, credits, and our own originals in `originals/`. |
| `lib/irdsp/src/DrumSong.h` | The song format: hits and bass events on a 24-per-quarter grid, and a bar table. |
| `lib/irdsp/src/PluckBass.{h,cpp}` | The plucked-string bass. |
| `songs/*.txt`, `tools/make_songs.py`, `lib/irdsp/src/DrumSongData.cpp` | The built-in songs, compiled (not in git) on every build. |
| `lib/irdsp/src/MidiDrums.{h,cpp}` | Reads a MIDI file's drum and bass parts into a song (in flash on the Teensy). |
| `src/DrumSongs.{h,cpp}` | The song list (built-in + `/DRUMS`), loading from the SD card, selecting, the loop. |
| `tools/import_kit.py`, `tools/make_kits.py`, `tools/pio_kits.py` | Build a kit's WAVs from its sources; compile the kits in (run by PlatformIO and the host Makefile). |
| `src/AudioConvolverIR.h` | Runs the drums each block and adds them to both outputs. |
| `src/Params.cpp`, `src/SerialUI.cpp`, `src/Settings.cpp` | What each drum setting does, the `drum` and `bpm` commands, EEPROM. |
| `lib/oledui/src/Menu.cpp`, `Screens.cpp` | The Drums menu page and the home-screen row. |

`make -C test/host test` runs `test_drums`, `test_bass` and `test_midi`. They check:
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
  - At Comp 0, 40 and 100 the level stays within 2 dB for synth, 3.5 dB for the recorded kit.
  - At the default it's in range: −25 to −14 dBFS for the recorded kit, −22 to −15 for synth.
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
  - Layers: 1–4 per lane, with a kick, snare and closed hat; several layers in a lane follow their velocities.
  - The hats sit more than 6 dB below the kick and snare.

- **Songs:**
  - Hits land on their exact sample on the 24-per-quarter grid, including a 32nd and a 16th triplet.
  - Bars in 4/4 and 3/4, wrap at the end.
  - A loop starts at its first bar and jumps back after its last.
  - The count-in plays before the song.
  - No crash accents in song mode, and the style comes back after `drum song off`.
  - The built-in songs are 32 bars with valid hits and bar tables, and stay below the drum bus's ceiling.
- **Toms:** the recorded kit has a rack and a floor tom.
- **Bass** (`test_bass`, `test_drums`):
  - Tuning within 3 cents (measured 0.1) from B0 to G3; every note and tone stable; E1 falls 25–50 dB in 2 s.
  - A mute is −60 dB within 100 ms, then the voice idles; dead notes die within 200 ms; louder with velocity.
  - No clicks: slope jumps after a mute stay below 1 % of the note's level, and a new note or dead note adds no step.
  - Song bass events on their exact samples, through loops, none in the count-in, muted by stop, none when off; the built-in bass lines are valid; drums + bass at full volume stay below the ceiling.
  - Style lines in the key (rock in E: roots, fifth, octave on their 8ths; key A), dead notes, silent count-in, muted at the switch to the metronome, every line fits its bar.
  - MIDI (`test_midi`): the GM bass program's channel (not the piano), the low channel without one, none with only a piano; note-offs as mutes, the lowest note of a chord, a re-struck pitch without a mute, octave folding, bar indexes; a too-long part is left out.
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
- every built-in song once through with the rock kit and its bass, to `test/host/out/song_<name>.wav` (the styles have their bass line in E)
- the bass alone (`render_bass`): a scale, a rock line at three tones and long notes, to `test/host/out/bass_*.wav`

## Ideas for later

None of these are built yet. They're practice features worth adding next:

1. **Swing amount:** a 0–66 % swing control, so offbeat 8ths or rock can go from straight, to a light swing, to a full shuffle. The sequencer would delay every second subdivision by the swing amount.
2. **Gap trainer:** the drums drop out for 1–2 bars out of every 4 (or another chosen ratio), then come back. If you're still on the grid when they return, your timing is solid.
3. **Fills for the styles:** a short fill at the end of every 4 or 8 bars, before the crash accent. The toms now exist (the songs use them); the styles would need fill bars.
4. **12-bar blues mode:** the shuffle with a bar counter (1–12) in `s` and on the display, plus a fill on the turnaround (bars 11–12), for practicing blues changes.
5. **Tap tempo:** tap `t` over serial, or later a key on the display, a few times to set the tempo from a song.
6. **More meters:** a 3/4 waltz and a 12/8 slow blues, a very common practice groove. Both fit the existing style format (3 beats x 4 steps, and 4 beats x 3 steps).
