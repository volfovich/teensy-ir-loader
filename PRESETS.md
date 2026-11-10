# Presets: plan

**Status (2026-11-10): P1 and P2 done: presets are saved, recalled, renamed and deleted over serial ([P1 results](#p1-results)), and a preset with another model switches in about 0.1 s ([P2 results](#p2-results)). Next: P3 (the panel).** A preset is a whole sound (the amp and its model, the gate, the IR, the doubler, the delay and the reverb) saved as a text file on the SD card. It is recalled from the panel or over serial.

```
/PRESETS/01 Clean.txt      ─┐
/PRESETS/02 Crunch.txt      ├─ pre 3, or push on the home screen's preset row ─► amp, gate, IR, doubler, delay, reverb
/PRESETS/03 Lead.txt       ─┘
```

- **The sound only.** Levels (line-in, line-out, headphones, the master output gain), the drums, the bass and the speed trainer stay as they are, so a preset sounds the same with any rig and doesn't stop the band.
- **Text files on the card,** in the serial commands' own words (`amp in +3.5`, `dtime 380`), so they can be read, edited, copied and backed up on a computer.
- **Fast enough to switch while playing.** Values change like turned knobs, the IR crossfades, and a new amp model loads from a cache in about 10 ms instead of 0.9 s. The model's 170 ms warm-up gap stays.

## Contents

- [What a preset holds](#what-a-preset-holds)
- [The file](#the-file)
- [Recalling a preset](#recalling-a-preset)
- [The weights cache](#the-weights-cache)
- [Saving, and the edited mark](#saving-and-the-edited-mark)
- [Serial commands](#serial-commands)
- [Panel](#panel)
- [Code](#code)
- [Memory and CPU](#memory-and-cpu)
- [Milestones](#milestones)
- [P1 results](#p1-results)
- [P2 results](#p2-results)
- [Tests](#tests)
- [Open questions](#open-questions)

## What a preset holds

| Block | Settings |
|---|---|
| Amp | `amp` (on/off), the model (its path), `amp in`, `amp out`, `amp cal`, `amp bass`, `amp mid`, `amp treble`, `amp presence` |
| Gate | `gate`, `gate type`, `gate thr`, `gate rel` |
| IR | the IR (its path), `b` (bypass) |
| Doubler | `dbl`, `dbl time`, `dbl width` |
| Delay | `dl`, `dmix`, `dtime`, `dfb`, `dtone`, `dmod`, `dduck`, `ddrive`, `dera` |
| Reverb | `rv`, `rmode`, `mix`, `size`, `tone`, `rpre`, `rmod`, `rera`, `rgate` |
| Level | **`pvol`, new:** the preset's level, −24 … +12 dB in half-dB steps, default 0, added to the output gain. It evens out a clean and a lead sound without touching the master. |

**Not in a preset:** `g` (the master output gain), `in`, `out`, `hp`, the drums (style, song, kit, tempo, crash, volume, compression, count-in), the bass and the trainer.

**The table decides.** `ui::ParamDef` (`lib/oledui/src/ParamDefs.h`) gets a `flags` field with a `kInPreset` bit, set on the rows above. Writing, reading and the edited mark all walk the table. A setting added later goes into presets, or stays out, by one flag in one place.

## The file

`/PRESETS/NN Name.txt`: a two-digit number (01–99), a space, the name. The name is the file's name; there is no second copy inside it, so renaming the file on a computer renames the preset.

```
# Teensy IR Loader preset
amp on
amp model /AMP/JCM_800_2203_OD8008_Boosted_Amp_Gain_5.nam
amp in +3.5
amp out 0.0
amp cal on
amp bass +1.0
...
gate on
gate type nam
gate thr -60
ir /IR/V30 SM57 Mesa 4x12.wav
b off
dl on
dtime 380
...
pvol -1.5
```

- **One line per setting:** the serial key, a space, the value as `formatTyped()` writes it (`on`, `+3.5`, `nam`). Read back with `parseValue()`: exactly what a serial command accepts. Every `kInPreset` setting is written, the defaults too, so a file is complete.
- **Two paths:** `amp model` and `ir`, written as the firmware knows them (`/AMP/…`, `/IR/…`).
- **Reading is forgiving:**
  - `#` comments, blank lines, CRLF line ends and trailing spaces are fine.
  - An unknown key is skipped with a warning (a file from newer firmware).
  - A missing key gets the table's default (a file from older firmware).
  - A value out of range is clamped, as a serial command would be.
  - A missing model or IR file keeps the current one, says so, and marks the preset edited.
- **Size:** about 700 bytes.
- **Why not the EEPROM:** the Teensy 4.0's emulated EEPROM holds 1,080 bytes. The settings record already takes about 310 and would take 400 with the preset's path. One preset is about 270 bytes in binary. Two presets would fill it, and nobody could read them.

## Recalling a preset

1. **Read the file** into a staging record (the values and the two paths), in the background. A file that can't be read changes nothing.
2. **The amp model,** if the path differs from the loaded one: from the [weights cache](#the-weights-cache), else the `.nam` file (and the cache is written). `AmpStage` fades out, swaps the weights in, warms up muted, and fades in: about 170 ms with the amp on, nothing heard with it off. The same model: no gap at all.
3. **The IR,** if it differs: loaded and crossfaded as from the IR list (about 14 ms to load, 25 ms of crossfade).
4. **The values:** copied into `g_settings.v` and applied through `applyParam()`, the same path as a turned knob, so every one is smoothed as it is today. One `scheduleSave()` for the whole lot, and one line on serial instead of one per setting.
5. **Remember it:** the preset's path in the settings record, and its values as the reference for the [edited mark](#saving-and-the-edited-mark).

**Tails:** the delay and the reverb keep their tails (spill-over), the way they do when switched off. Their new settings act on the tail at once, like turning their knobs. If that sounds wrong on a big change (a long delay to a short one), the fallback is a 20 ms dip of the effects' input around the switch. Decide by ear in P1.

## The weights cache

A `.nam` file is about 670 KB of JSON (both networks); reading it takes about 0.9 s, and `loop()` is blocked meanwhile. The firmware needs 7.5 KB of it.

- **`/AMP/.cache/<file name>.bin`:**
  - a header: magic, version, the source's size and modified date;
  - the `NamInfo`;
  - the 1,871 weights as raw floats.

  The scan already skips dot-names, so the folder never shows up as a model.
- **Written** after every full load of a `.nam` file (from the list, a preset, or startup). It takes a few ms.
- **Used** when its header matches the source's size and date. Otherwise it is rewritten. A damaged cache costs one slow load, never a wrong sound.
- **Read time:** 7.5 KB from the SD card, about 10 ms. The panel's `Loading` screen only shows for a slow load.
- **Everyone gains:** the model list and startup get faster too, after the first time.

## Saving, and the edited mark

- **The edited mark:** `03 Lead*` while any `kInPreset` value, the model or the IR differs from the loaded preset's. It is computed against the reference kept at recall, so turning a knob back clears it.
- **Save** writes the current values over the loaded preset (with a confirm on the panel).
- **Save as new** takes the lowest free number, and a name: the panel offers `Preset NN`; over serial it's the command's text.
- **Rename** renames the file. **Delete** removes it (with a confirm); the numbers of the others don't move.
- **At power-up** the settings record holds the live values as now, edits and all, plus the preset's path. So the last sound comes back exactly, and the file is read once to restore the mark.
- **The EEPROM record** gets `presetPath` (IR_PATH_MAX) after `ampPath`, with a new magic `IRLR`. The `IRLQ` record is read once and carried over, as `loadSettings()` does for the records before it.

## Serial commands

| Command | What it does |
|---|---|
| `pre` | The presets in `/PRESETS`; `>` marks the loaded one, `*` if edited. |
| `pre <n>` / `pre <name>` | Recalls preset n, or the first whose name contains the text. |
| `pre save` | Writes the current sound over the loaded preset. |
| `pre new <name>` | Saves the current sound as a new preset, at the lowest free number. |
| `pre name <text>` | Renames the loaded preset. |
| `pre del <n>` | Deletes preset n (no confirm over serial: type the number). |
| `pre show` | The settings that differ from the loaded preset: `amp in +3.5 (preset +2.0)`. |
| `pvol <-24..+12>` | The preset's level in dB (a normal setting, saved in the preset). |

`r` (rescan) also rescans `/PRESETS`. `s` gets a `Preset:` line.

## Panel

Renders come first, as always (`make -C test/host render`); nothing is flashed before they're seen.

**Home.** Row 1 is full (`AMP NG IR DLY RE`), so the preset gets row 0:

```
┌────────────────┐
│03 Lead*        │  row 0: the preset (inverted while the cursor is on it)
│AMP NG IR DLY RE│  row 1: the blocks, as now
│      ▀▀        │
│100  offbeat8   │  rows 2-3: tempo, style, TRN, as now
└────────────────┘
```

- **The cursor's first stop is row 0, PRE**, then AMP, NG, IR and on as now.
- **Row 0** shows the preset (`NN Name`, `*` when edited, scrolling when long) wherever the cursor is, except on AMP (the model's name) and IR (the IR's name), as now. With no preset loaded, or no `/PRESETS` folder, row 0 shows the IR's name, exactly as today.
- **Push on PRE:** the preset list.
- **Hold on PRE:** Save, after a confirm screen (`Save 03 Lead?`, push = yes, BACK = no).

**The preset list** works like the IR list: turn moves, push recalls (the list stays open, to try the next one), BACK returns. There is no preview on resting: a preset may load a model (a gap) and would be a surprise. The last rows are `Save as new` and `Rescan`.

**The Presets page** (in the menu, after Gate): Preset (`3/12`, opens the list), Level (`pvol`), Save, Save as new, Rename, Delete.
- **Rename:** a character editor. Turn picks the character (A–Z, a–z, 0–9, space, `-`), push moves to the next, hold keeps the name, BACK cancels. Up to 24 characters.

**Footswitches** (P4, later): two inputs on free pins (0, 1, 14 or 22, to GND with the internal pull-ups) for preset up and down, the way the keys work now.

## Code

| File | What |
|---|---|
| `lib/oledui/src/PresetFile.{h,cpp}` | **New**, Arduino-free: writes a preset from the values (`formatValue`) and parses one (`parseValue`); the forgiving rules above. Host-tested. |
| `lib/oledui/src/ParamDefs.{h,cpp}` | The `flags` field and `kInPreset`; the `pvol` row at the end of the saved block. |
| `src/PresetLibrary.{h,cpp}` | **New**: scans `/PRESETS`, recalls, saves, renames, deletes; the edited mark. Background only: on the cold list in `tools/imxrt1062_cold.ld` (both `COLD_` blocks). |
| `src/AmpLibrary.cpp` | The weights cache (read and write); `loadAmpPath()` for a preset's model. |
| `src/Params.cpp` | A quiet bulk apply for a recall; `pvol` added to the output gain. |
| `src/Settings.{h,cpp}` | `presetPath`, the `IRLR` record and the carry-over. |
| `src/SerialUI.cpp` | The `pre` commands, the `Preset:` status line. |
| `lib/oledui/src/Menu.cpp`, `Screens.cpp`, `Panel.cpp`, `src/DisplayUI.cpp` | PRE on the home screen, the list, the page, the rename editor, the confirm screens. |

## Memory and CPU

- **CPU:** none in the audio path. A recall is background work; the only audio-side work is what a knob turn, an IR switch or a model change already does.
- **RAM2 (26 KB free):** the list's names, 99 × 32 bytes (3.2 KB, DMAMEM), the staging record and the reference values (under 1 KB).
- **RAM1 and ITCM:** unchanged; the new code runs from flash.
- **The SD card:** about 700 bytes a preset, 7.6 KB a cached model.

## Milestones

| | What | Done when |
|---|---|---|
| **P1** | The file format, the recall, `pvol`, the serial commands, the edited mark, the settings record. | **Done** ([results](#p1-results)); still to hear: switching while playing, by ear. |
| **P2** | The weights cache. | **Done** ([results](#p2-results)). |
| **P3** | The panel: row 0, PRE, the list, the page, rename, the confirms. | Renders approved, then on the device. |
| **P4** | Footswitches (later). | Up and down from the floor. |

## P1 results

Built and checked on the Teensy on 2026-11-10:

- **The code:**
  - `lib/oledui/src/PresetFile.{h,cpp}`: the format, one line at a time.
  - `formatTyped()` in `ParamDefs`: a value as typed.
  - The `flags` column with `kInPreset` on 35 rows, and `pvol`.
  - `src/PresetLibrary.{h,cpp}`, the `pre` commands, the `Preset:` line in `s`.
  - `setParamQuiet()`, and the `IRLR` settings record with `presetPath` (the `IRLQ` record carries over: the user's settings came through untouched).
- **Recall time:**
  - Only values changing: **55 ms**.
  - Another model and IR: **about 1.07 s**, of which 946 ms is reading the model's JSON (the cache in P2 removes that) and 70–80 ms the IR.
- **CPU:** 75.3 % max while a recall switched the model and the IR (amp, IR and reverb on); 57–59 % otherwise; no clipped samples.
- **Checked:**
  - Save as new (the lowest free number), list, recall by number and by name, rename (the number kept), save over, delete.
  - The edited mark: set by turning a knob, cleared by turning it back; `pre show` lists the differences.
  - The mark comes back after a restart (`presetPath` in the EEPROM, the file read again at boot).
- **Memory:** RAM2 −5.3 KB (20.8 KB left): the list's 99 names (4.7 KB) and two preset records. ITCM and RAM1 unchanged: all of it is in flash, checked with `nm`.
- **Saving** goes through `/PRESETS/.saving.tmp`, then replaces the old file, because the SD library's `FILE_WRITE` appends.
- **Choices made:**
  - `pre <n>` means the preset's own number (`05 X.txt` is `pre 5`), not its place in the list.
  - A file without a number is listed last and recalled by name.

## P2 results

Built and checked on the Teensy on 2026-11-10:

- **The format:** `lib/irdsp/src/NamCache.h` (portable). A header (magic, version, the `NamInfo`'s size, the weight count, the source's size and packed modified time, an FNV-1a checksum), then the `NamInfo` and the 1,871 weights: **7,688 bytes**.
- **In `AmpLibrary.cpp`:** `loadAmpIndex()` tries `/AMP/.cache/<file>.bin` first. If it is missing or doesn't match, it parses the `.nam` (with the `Loading` screen) and writes the cache through a temporary file. Every model load gains, not only presets: the list, `amp <n>` and startup.
- **Loading a model:** **18–28 ms** from the cache, against 950–970 ms from the `.nam` file. The first load of each model is the slow one.
- **A preset with another model and IR:** **105–160 ms** in all (was 1.07 s). About 70 ms of it is the IR, resampled from 44.1 kHz on every load. The model's 170 ms warm-up gap in the sound stays.
- **The same sound:** `amp_selftest.py` with the model loaded from its cache: 3.87e-7, exactly as from the `.nam`.
- **CPU:** 70 % max while switching (amp, IR, reverb on), no clipped samples.
- **Memory:** none: the weights are read straight into the existing staging buffer.
- **Tests** (`test_namfile`): the real model through a cache gives the same weights and info. Another size or date, any cut-off and any flipped byte are refused, and the info is left untouched.
- **Never wrong, at worst slow:** a cache whose size or date doesn't match its `.nam` file is rebuilt (copying a new version of a model over the old one changes both), and so is a damaged one. Deleting `/AMP/.cache` is always safe.

## Tests

**On the PC** (`make -C test/host test`):
- **`test_presets`** (built in P1, also clean under AddressSanitizer and UBSan):
  - A round trip of every `kInPreset` setting, at its minimum, maximum and default.
  - Keys in any order; comments, CRLF and blank lines.
  - Unknown keys skipped; missing keys defaulted; values clamped.
  - Long names and names with spaces; cut-off and randomly damaged files: an error or defaults, never a crash (also under AddressSanitizer).
- **`test_ui`:** PRE on the home screen and the row-0 rules, the list, the page, rename (edit, keep, cancel), the confirms, the edited mark.

**On the Teensy:**
- Recall time with the same model, with a cached model, and with an uncached one.
- CPU max while switching presets during the worst case (drums, bass, everything on): no clips.
- Power off and on: the same sound, the same mark.

## Open questions

- **Tails on a switch:** spill-over (the plan) or a short dip of the effects' input. Decide by ear in P1.
- **Numbering:** whether to renumber when a preset is deleted, or leave the gap (the plan).
- **MIDI:** program change over USB MIDI to recall presets, if the firmware gets USB MIDI.
- **Tempo:** left out on purpose (the drums are global). A delay synced to a song's tempo would need it; revisit if the delay gets tap or sync.
