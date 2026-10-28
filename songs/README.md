# Built-in drum songs

Each `.txt` file here is one multi-bar drum song. `tools/make_songs.py` compiles them into the firmware on every build.

These are approximate arrangements *in the style of* well-known rock songs: their grooves, section lengths and typical fills, written for practice, with simple bass lines on the chord roots. They are not transcriptions. For a song's real drum part, copy a General MIDI drum file (`.mid`) to `/DRUMS` on the SD card (see [DRUMS.md](../DRUMS.md#songs)).

## Format

```
name   sandmanish            # command name (drum song sandmanish), up to 15 characters
title  Enter Sandman-ish     # shown in lists and on the display, up to 23 characters
bpm    123                   # the song's tempo (quarter notes per minute)
meter  4/4                   # default meter for the bars below
grid   4                     # default steps per quarter note: 4 = 16ths, 3 = 8th
                             # triplets, 6 = 16th triplets, 8 = 32nds

bar V                        # a bar called V; can override: meter 3/4  grid 6
  kick   9.....8.9.......    # one indented line per lane
  snare  ....9.......9...
  hatc   9.7.8.7.9.7.8.7.
  bass   E1:9 - E1 - . . E1:8 - E1:9 - . . E1 - E1 -

bar V2 like V                # V's drums, meter and grid, with its own bass line
  bass   A1:9 - A1 - . . A1:8 - G1:9 - . . G1 - G1 -

song   V*3 F  V V V2 F ...   # the bars in order; ID*n repeats it n times
```

- **Lanes:** `kick snare rim hatc hato ride crash tomh toml` (rack and floor tom).
- **Steps:** `.` is a rest, `1`–`9` a hit's velocity (9 hardest). `g`, `x` and `X` mean 3, 6 and 9.
- **Bar length:** each lane line has exactly beats × grid × 4 / note-value steps, e.g. 16 for 4/4 at grid 4, and 12 for 6/8 at grid 4. A lane a bar leaves out is silent in that bar.
- **Bass:** one space-separated token per step, played on the plucked-string bass (see [DRUMS.md](../DRUMS.md)):
  - a note: `E1`, `F#1`, `Bb0` (C-based octaves, E1 = MIDI 28; from B0 to G3), velocity 7, or with one: `E1:9`;
  - `-` holds the note (it rings until the next token that isn't `-`);
  - `.` mutes it, like lifting the finger;
  - `x` (or `x:5`) is a dead note, a damped thump on the last pitch.

  A bar without a `bass` line is silent for the bass. `#` starts a comment only at the start of a line or after a space, so `F#1` is a note.
- **`like`:** `bar V2 like V` copies V's drums, meter and grid but not its bass line, so a section can change chords while the drums repeat.
