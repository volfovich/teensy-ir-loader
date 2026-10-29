# OLED + Encoder Control

A 128x64 OLED module (1.3" SH1106 or 0.96" SSD1306) with a rotary encoder (with push switch) and two buttons. It replaced the five optional buttons and makes every serial-terminal function available from the front panel. The serial UI keeps working alongside it.

The UI is written and tested on the PC: `lib/oledui` draws every screen and runs the menu and the panel's controls (`test/host/test_ui.cpp`, and `render_ui` renders the screens to PNG). The firmware adds the display driver, the encoder and keys, and the glue to the settings ([Files](#files)).

## Summary

- **Display:** 128x64 SH1106 (1.3") or SSD1306 (0.96") on its own I2C bus, `Wire1` (pins 16/17, 1 MHz), apart from the audio codec's (pins 18/19). `OLED_SH1106` in `config.h` picks the controller.
- **Inputs:** the encoder and the two buttons reuse the five wires of today's buttons (pins 2, 3, 4, 5, 9).
- **Software:**
  - A small SH1106 / SSD1306 driver of our own, running from flash. It sends only the 128-byte pages that changed.
  - An interrupt-driven encoder.
  - A menu driven by the settings table (`ParamDefs.cpp`), changing settings through the same `setParam()` as the serial commands.
- **Memory:** the panel's code and fonts run from flash. It costs about 0.8 KB of RAM1 (the encoder interrupt and the core's pin-interrupt code) and 2 KB of RAM2. See [RAM1 strategy](#ram1-strategy).

## Hardware

### Connections

Module pin labels vary between sellers. Common names are given in brackets.

| Module pin | Teensy 4.0 pin | Replaces | Notes |
|---|---|---|---|
| VCC | **3.3 V** | | **Never 5 V.** The module's pull-ups would put 5 V on the Teensy pins, which are not 5 V tolerant. |
| GND | GND | | |
| SDA | **17** (SDA1) | | The display's own bus, `Wire1`. |
| SCL | **16** (SCL1) | | |
| Encoder A (TRA, ENCA, CLK) | **2** | next button | |
| Encoder B (TRB, ENCB, DT) | **3** | prev button | |
| Encoder push (PSH, SW, ENCSW) | **4** | bypass button | |
| CONFIRM (CON, K1) | **5** | reverb button | |
| BACK (BAK, K2) | **9** | delay button | |

```
      OLED + encoder module                    Teensy 4.0 (+ Audio Shield)
  ┌──────────────────────────┐
  │ VCC ─────────────────────┼──────────────► 3.3V
  │ GND ─────────────────────┼──────────────► GND
  │ SDA ─────────────────────┼──────────────► 17  (SDA1)
  │ SCL ─────────────────────┼──────────────► 16  (SCL1)
  │ TRA  (encoder A) ────────┼──────────────► 2
  │ TRB  (encoder B) ────────┼──────────────► 3
  │ PSH  (encoder push) ─────┼──────────────► 4
  │ CON  (confirm) ──────────┼──────────────► 5
  │ BAK  (back) ─────────────┼──────────────► 9
  └──────────────────────────┘
```

Notes:

- **I2C address:** usually 0x3C. Some modules are set to 0x3D with a resistor on the back. It is set in `config.h`.
- **Pull-ups:** the module's own I2C pull-ups serve `Wire1`. If the picture glitches at 1 MHz (weak pull-ups round the edges), set `OLED_I2C_HZ` to 400000.
- **Encoder direction:** on our module, clockwise counts down, so `config.h` has `ENC_REVERSE 1`. Set it to 0 if clockwise moves the cursor left on yours.
- **Encoder and buttons:** the firmware uses the Teensy's internal pull-ups (`INPUT_PULLUP`), with the contacts switching to GND. If the module has its own pull-ups to VCC, that is fine at 3.3 V.
- **Encoder bounce:** if the module has no RC filter on the encoder, a 100 nF capacitor from A and from B to GND helps. The state-table decoder tolerates bounce anyway, so try without first.
- **Physical access:** with the shield stacked, pins 16/17 are reachable on the shield's header or on the Teensy's top header pins.
- **Other free pins**, if needed later: 0, 1, 14, 22. 15 is the shield's volume-pot pad and 6 its memory-chip CS.

### Why the display has its own bus

Until 2026-10-29 the display shared the codec's bus (`Wire`, pins 18/19, 400 kHz). That worked: all codec and display writes run from `loop()`, never from an interrupt. Its own bus (`OLED_WIRE` in `config.h`) brings:

- **A faster screen:** 1 MHz instead of the codec's 400 kHz, so a page takes about 1.3 ms instead of 3.3 ms, and a full redraw about 11 ms instead of 26.
- **A faster boot:** the display needs nothing else, so the splash shows first, before the codec's 0.4 s power-up.
- **A codec bus that's never shared:** codec writes don't wait behind page sends, and the codec stays at its library's own clock.

To go back to the shared bus, set `OLED_WIRE` to `Wire` and `OLED_I2C_HZ` to 400000, and wire SDA/SCL to 18/19.

## Controls

### Home screen

The home screen shows the chain as blocks, AMP › IR › DLY › REV › DRM, then the tempo, with a cursor under one of them.

| Input | Action |
|---|---|
| Turn encoder | Move the cursor: AMP, IR, DLY, REV, DRM, then the tempo |
| Push encoder | Open the block: the [model list](#ir-list-and-amp-list) for AMP, the [IR list](#ir-list-and-amp-list) for IR, the Delay, Reverb or Drums page of the menu for the others. BACK from there comes back home, with the cursor where it was. On the tempo: edit it in place (below). |
| Hold encoder (≥ 0.6 s) | The block under the cursor on/off: AMP = the amp block ([NAM.md](NAM.md)), IR = bypass, DRM and the tempo = drums start/stop |
| CONFIRM | Drums start/stop, wherever the cursor is |
| BACK | Open the menu (Levels, Meter, Status, Bypass …) |

**Editing the tempo:** the tempo is drawn inverted. Turning changes it live, 1 BPM per click and ×5 when the clicks come less than 40 ms apart, from 40 to 240 BPM. Push keeps it, BACK restores the tempo from before, and 30 s without input keeps it. It is saved like any other setting.

### Menus

| Input | In a list | While editing a value |
|---|---|---|
| Turn | Move the cursor | Change the value. It applies live, so you hear it while turning. |
| Push / CONFIRM | Enter the submenu, start editing, or run the action | Keep the value and stop editing |
| BACK | Up one level (from the top level, back home) | Cancel: restore the value from before editing |

### IR and amp lists, Meter, Status

| Input | IR or amp list | Meter | Status |
|---|---|---|---|
| Turn | Move the cursor | Line-in sensitivity | — |
| Push / CONFIRM | Load the IR or model under the cursor (or rescan); the list stays open | CONFIRM: reset the clip counter and peak holds | — |
| BACK | Back to where it was opened from (home or the menu) | Back to the menu | Back to the menu |

After 30 s without input, the display returns to the home screen. The meter screen stays, because it is used while playing.

**Acceleration:** when detents come less than 40 ms apart, each step counts ×5. This matters for the delay time (20–750 ms in 5 ms steps) and the output gain.

Short presses act on release, so a short press and a hold on the same button can be told apart. CONFIRM and BACK act on press, because they have no hold function.

## Screens

All text is in the Spleen 8x16 font: 16 characters x 4 rows of 16 px. Big numbers (the tempo, the value being edited) are in Spleen 16x32. The mockups are character grids; `screenshots/` has the real screens, rendered from the UI code (`make -C test/host screenshots`).

### Splash

`IR LOADER` and a rule while `setup()` runs, then the IR count (`12 IRs on SD`, or `No SD card`) once the card has been read. It stays up 1.5 s; see [DisplayUI](#displayui). Renders: `01_splash_boot.png`, `01_splash.png`.

### Home

```
┌────────────────┐
│V30 SM57 Mesa 4x│  row 0: the IR (BYPASS + name when bypassed);
│AMP IR DLY REV D│    the amp model's name with the cursor on AMP
│   ▀▀           │  row 1: the blocks, filled = on, top and bottom
│100   offbeat8  │    lines = off, a bar under the selected one
│▀▀▀   ■□□□      │  rows 2-3: the tempo in the big font; the style's
└────────────────┘    short name and a box per beat (cursor on the tempo)
```

- **The blocks** follow the signal: the amp, the IR, then the delay, then the reverb, then the drums (mixed in after the effects, never through them). IR is "on" when not bypassed.
- **Five blocks fit with one pixel to spare each side of the letters,** so an off block has only its box's top and bottom lines: its sides would touch the letters.
- **Row 0:** the IR's file name without the folder or the `.wav` extension (16 characters). With no SD card or no IRs: `No SD card` or `No IRs on SD`.
- **Row 0 on AMP:** the amp model's name, with `_` shown as a space (`JCM 800 2203 OD8008 Boosted Amp Gain 5`). A longer name scrolls, as in the lists. `No amps on SD` or `No amp loaded` when there's none. Renders: `02g_home_amp.png`, `02h_home_amp_scrolled.png`.
- **Rows 2–3:** the tempo now (with the speed trainer, the tempo it has reached), selectable and editable from here. Beside it, the style's short name (at most 9 characters: `metronome`, `offbeat16`, `shuffle` …) or the selected song's, and one box per beat of the bar (up to 7). The beat sounding is filled; all are hollow while stopped, so nothing appears or moves when the drums start. They also count the count-in.
- Everything else (delay time, mixes, the song's bar, the kit, the meters) is in the menus and on the Meter screen, to keep the home screen readable.

### Menu list

```
┌────────────────┐
│DELAY        3/9│  title, inverted; the cursor's place when the page scrolls
│On            ON│
│Mix          30%│
│Time       380ms│  cursor row, inverted
└────────────────┘
```

Three items show at a time. A value too long for its row is cut with `~`; the editor shows it in full.

### Value editor

```
┌────────────────┐
│Time            │  the item, inverted
│     380 ms     │  the value
│                │
│[#######.......]│  position within min..max (a mark at 0 for the gain)
└────────────────┘
```

The font is fixed per item, so it never changes while turning (`Menu::bigValue`): numbers are always in the 16x32 font (every value of every item fits, the widest being `-60.0 dB`; a test checks them all), and names (Style, Kit, Song: `Unit::kName`) always in 8x16, wrapped onto two rows at a space when longer than 16 characters.

### IR list and amp list

The IR list opens from the IR block or Menu → IR; the amp list (the models in `/AMP`) from the AMP block or Menu → Amp → Model.

```
┌────────────────┐     ┌────────────────┐
│IR          3/12│     │AMP          7/8│
│01 Greenback    │     │06 OD800~ain 39 │
│02 V30 Room     │     │07 OD8008 Boost*│  cursor row inverted, * = loaded
│03 V30 SM57 Mes*│     │08 OD800~Gain 7 │
└────────────────┘     └────────────────┘
```

- The last entry is `Rescan SD card`. A list opens on the loaded file. Push loads the one under the cursor and the list stays open, so IRs or models can be compared one after the other.
- **Long names:** the cursor row scrolls sideways; the other rows keep both ends with a `~` between, since names often differ only at the end (`… Gain 5`).
- **Words every name shares are left out** of the rows (`JCM 800 2203` for a set of captures of one amp), so the rows show what differs. Home row 0 shows the whole name.
- Loading a model takes about 0.9 s (the screen waits); an IR about 15 ms. Render: `23_amp_list.png`.

### Status (Menu → Status)

```
┌────────────────┐
│STATUS          │
│CPU 33% max 35% │
│Clips 0         │
│Blocks 4/20     │
└────────────────┘
```

It refreshes once a second and resets the max values like serial `s` does. Serial `s` has the full details.

## Level meter

### Where the peaks come from

`convolver.takeInputPeak()` and `takeOutputPeak()` reset on read, and the serial meter (`m`) and `s` use them. If the display read them too, each reader would steal peaks from the other. So `AudioConvolverIR` keeps a second peak pair for the display, `takeDisplayPeaks(float& in, float& out)`. The audio interrupt updates both pairs, two extra `max` operations per block.

### The Meter screen (Menu → Meter)

This screen is for setting the input level (the procedure in the README's *Level and safety* section) without a computer.

```
┌────────────────┐
│METER    clips 0│  CLIP instead of the count while clipping
│IN  ######|.[ ].│  [ ] = target zone -6..-3 dBFS
│OUT ####|.......│
│Sens 0   pk -7.5│  line-in sensitivity; input peak hold
└────────────────┘
```

- **Scale:** linear in dB from −48 to 0 dBFS, with tick marks at −24, −12, −6 and −3 dBFS in the empty part of the bar.
- **Ballistics:** the bar rises instantly and falls at about 20 dB/s. A peak-hold marker stays for 1.5 s and then falls. All of this is computed on the display side from the raw peaks, 25 times a second.
- **Clipping:** `CLIP` shows in the title for at least 2 s when the input exceeds −0.5 dBFS or the output −0.1 dBFS (the serial meter's rule), or when the clip counter moved, so short clips aren't missed between frames.
- **Turning the encoder changes the line-in sensitivity** (`setLineInLevel`, 0–15), so you can play and adjust at the same time.
- CONFIRM resets the clip counter and peak holds. BACK exits.

## Menu tree

Every setting is one row of the parameter table (`lib/oledui/src/ParamDefs.cpp`), which gives its menu row the label, range, step and unit, and its serial command the key. So every serial command has a place:

| Menu | Item | Range / step | Serial |
|---|---|---|---|
| **Amp** | On | toggle | `amp` |
| | Model | the model list (push = load) | `amp list`, `amp <n>` |
| | Input | −24 to +24 dB, 0.5 | `amp in` |
| | Output | −24 to +24 dB, 0.5 | `amp out` |
| | Calibrate | toggle | `amp cal` |
| **IR** | file list (push = load) | — | `l`, `<n>` |
| | Rescan SD card | action | `r` |
| **Delay** | On | toggle | `dl` |
| | Mix | 0–100 %, 1 | `dmix` |
| | Time | 20–750 ms, 5 | `dtime` |
| | Repeats | 0–100 %, 1 | `dfb` |
| | Tone | 0–100 %, 1 | `dtone` |
| | Mod | 0–100 %, 1 | `dmod` |
| | Duck | 0–100 %, 1 | `dduck` |
| | Drive | 0–100 %, 1 | `ddrive` |
| | Era | 0–100 %, 1 | `dera` |
| **Reverb** | On | toggle | `rv` |
| | Mode | choice: hall / gated | `rmode` |
| | Mix | 0–100 %, 1 | `mix` |
| | Size | 0–100 %, 1 | `size` |
| | Tone | 0–100 %, 1 | `tone` |
| | Pre-delay | 0–150 ms, 1 | `rpre` |
| | Mod (hall) | 0–100 %, 1 | `rmod` |
| | Era | 0–100 %, 1 | `rera` |
| | Gate (gated) | 100–600 ms, 10 | `rgate` |
| **Drums** | Play | toggle | `drum` |
| | Bass | page (shows ON/OFF) | |
| | Style | 1–8, shown by name | `drum style` |
| | Song | off or 1–n, shown by name | `drum song` |
| | Loop from / Loop to (with a song) | off or 1–bars / 1–bars | `drum loop` |
| | Kit | 1–2, shown by name | `drum kit` |
| | Crash | push steps off / 2 / 4 / 8 / 16 bars | `drum crash` |
| | Tempo | 40–240 BPM, 1 | `bpm` |
| | Volume | 0–100 %, 1 | `drum vol` |
| | Comp | 0–100 %, 1 | `drum comp` |
| | Count-in | push steps off / 1 / 2 / 3 / 4 bars | `drum count` |
| | Trainer | page (shows ON/OFF) | |
| **Drums > Bass** | On | toggle | `bass` |
| | Volume / Tone | 0–100 %, 1 | `bass vol` / `bass tone` |
| | Key | C..B, shown by name | `bass key` |
| **Drums > Trainer** | On | toggle | `drum train` |
| | Step / Every / Target | 1–20 BPM / 1–16 bars / 40–240 BPM | `train step` / `train every` / `train target`, or all three: `drum train s e t` |
| **Levels** | Output gain | −60 to +24 dB, 0.5 | `g` |
| | Line-in sens | 0–15 | `in` |
| | Line-out | 13–31 (13 = loudest) | `out` |
| | Headphones | 0–100 %, 1 | `hp` |
| **Bypass** | toggle | | `b` |
| **Meter** | full-screen meter | | `m` |
| **Status** | status page | | `s` |

`n` / `p` are covered by the IR list (push on the IR block).

## Software architecture

### Files

```
src/Controls.{h,cpp}    replaced Buttons.{h,cpp}: encoder ISR, 3 debounced keys (ui::Key), events
src/Oled.{h,cpp}        minimal SH1106 / SSD1306 driver: init, send one page
src/DisplayUI.{h,cpp}   fills the UiModel and implements MenuHost (getParam / setParam); feeds
                        the control events to ui::Panel and sends the pages that changed
src/Params.{h,cpp}      reading and changing any setting by number, shared with the serial UI
lib/oledui/             the UI, Arduino-free: ParamDefs (the settings table), Canvas, font, Menu
                        (the tree and its engine), Screens (drawing from a UiModel), Meter, Key,
                        Panel (screens and controls)
include/config.h        pin/address defines
src/AudioConvolverIR.h  + display peak pair
src/main.cpp            controlsBegin()/displayBegin() in setup(); service calls in loop()
```

### config.h

```cpp
// OLED + encoder module (see DISPLAY.md). OLED_I2C_ADDR -1 disables the display.
#define OLED_WIRE Wire1          // Wire to share the codec's bus (pins 18/19)
#define OLED_I2C_HZ 1000000      // 400000 if the picture glitches (the SH1106 is rated for 400 kHz)
#define OLED_I2C_ADDR 0x3C       // 0x3D on some modules (tried too)
#define OLED_SH1106 1            // 1 = SH1106 (1.3" modules), 0 = SSD1306 (0.96")
#define OLED_COL_OFFSET 0        // SH1106: first visible RAM column (2 on many modules, 0 on ours)
#define ENC_A_PIN 2
#define ENC_B_PIN 3
#define ENC_PUSH_PIN 4
#define KEY_CONFIRM_PIN 5
#define KEY_BACK_PIN 9
#define ENC_COUNTS_PER_DETENT 4  // a full quadrature cycle per click (ours); 2 if one click moves two steps
#define ENC_REVERSE 1            // 1 if clockwise moves the wrong way (it does on our module)
```

### Controls

- **Encoder:**
  - `attachInterrupt(..., CHANGE)` on A and B feeds a 16-entry quadrature state table into a `volatile int32_t` count.
  - Every Teensy 4 pin supports interrupts.
  - Because it is interrupt-driven, no steps are lost while `loop()` is blocked, for example during an IR load (about 29 ms).
  - The ISR is a few instructions long and stays in ITCM.
- **Keys:** the same 15 ms debounce as today's `Button` struct, extended with press, release and hold (≥ 600 ms) detection.
- **API:** `readControls()`, called every `loop()` pass, returns the whole detents turned since the last call and one `ui::Key` event (press, release, hold) per key. `DisplayUI` maps them: push release → `Panel::push`, push hold → `hold`, CONFIRM and BACK press → `confirm` / `back`.

### Oled driver

Why our own driver: see [RAM1 strategy](#ram1-strategy). It is about 70 lines.

- **Init:** probes `OLED_I2C_ADDR`, then 0x3D, and sends the 128x64 init sequence for the controller `OLED_SH1106` selects: the SSD1306's charge pump and page addressing mode, or the SH1106's DC-DC converter (it only has page addressing). It then blanks the whole display RAM (132 columns on the SH1106), so no power-on garbage shows. With no display found, the panel stays off and the firmware runs as before.
- **SH1106 column offset:** its RAM is 132 columns wide and the glass shows 128 of them, starting at `OLED_COL_OFFSET`. That is 2 on many modules but 0 on ours. If it is wrong, the line ends are cut off on one side and stray pixels show on the other.
- **"SH1106" modules with an SSD1306-compatible chip:** ours is one (hence offset 0). It stayed black after power-up until the SSD1306 charge-pump command (`8D 14`) was added to the SH1106 init; a real SH1106 ignores it. This only shows from cold: the setting survives a warm restart (a reflash), so a module once started by the SSD1306 init works until it is unplugged. The init also waits 100 ms between the converter and display-on, as the SH1106 datasheet asks.
- **Sending:** `oledSendPage(p, data)` sends one page: a 3-byte address command, then 1 control byte + 128 data bytes. The Teensy 4 `Wire` buffer is 136 bytes, so a whole page fits in one transaction. A page takes about 1.3 ms at 1 MHz.
- **Drawing** is `ui::Canvas` (`lib/oledui`): the frame buffer, text in the two fonts, lines, boxes and inversion.
- **Fonts:** Spleen 8x16 and 16x32 by Frederic Cambus (BSD-2-Clause), 2.2.0. `tools/make_font.py` converts the BDF files in `fonts/spleen/` into `lib/oledui/src/Fonts.cpp` (7.6 KB in flash, ASCII 32..126) in the display's page layout, so a glyph's columns are copied straight into the frame buffer. Text rows start at x = 1: the glyphs' blank column is on the right, and this keeps inverted rows off the left edge.

### DisplayUI

- **Panel:** `ui::Panel` (`lib/oledui/src/Panel.h`) owns which screen is showing and what the controls do on it. `DisplayUI` only passes it the events and calls `draw()` when it returns true. It reads the IR count and the loaded IR from the `UiModel`, and sends everything else through `MenuHost::set`, including its actions (`kIrLoad`, `kIrRescan`, `kMeterReset`).
- **Beat dots:** the model's beat comes from `convolver.drums().beatPos()`.
- **Table-driven settings.** Every setting is one row of `ParamDefs.cpp`:

  ```cpp
  struct ParamDef {
    const char* key;    // serial command ("dmix")
    const char* name;   // in messages and the help ("Delay mix")
    const char* label;  // the menu row ("Mix")
    int16_t min, max, def;
    uint8_t step;       // per detent in the editor
    Unit unit;          // on/off, %, ms, half-dB, BPM, bars, name
    const char* const* choices;
  };
  ```

  - Every value is an `int16_t`: the output gain is in half-dB steps and the headphone volume in percent.
  - A menu row is just `{label, kind, id, page, visibleIf}`. A setting's row (`Kind::kSetting`) takes everything else from the table, and how it edits follows from the unit: a toggle flips on push, a choice steps through its names, a value opens the editor.
  - The ranges that depend on the firmware (the delay's longest time, how many styles, kits, songs and bars) come from `MenuHost::limits`; the names of styles, kits and songs from `MenuHost::valueText`.
- **Changing a setting:** `setParam()` (`src/Params.cpp`) clamps, applies (one `switch` in `applyParam()` knows what each setting does), and calls `scheduleSave()`, which is debounced, so turning the knob doesn't wear the EEPROM. The serial commands use the same function.
- **Redraw and send:**
  - The current screen is drawn into the canvas right after any control event, and otherwise every 40 ms (25 Hz), from a model filled fresh each time. The meters take their readings there.
  - A second 1 KB buffer (`DMAMEM`) holds what the OLED shows. `serviceDisplay()` sends **at most one page per call, and only a page whose bytes differ** from it, round-robin so busy rows (the Meter screen's bars) can't starve the others.
  - So a still home screen sends nothing (only the beat row while the drums play), and changes made over serial show on the screen within a frame, with no coupling between the two UIs.
- **Serial echo:** `setParam()` prints a confirmation on Serial at every encoder step, for example `Delay time 385ms`. That is harmless. If it gets noisy, add a `quiet` flag later.
- **Loop budget:** `serviceDisplay()` sends at most one page (about 1.3 ms) per call. Drawing a screen runs from flash and takes much less.
- **Splash:** the first thing `setup()` does, before the audio starts: the title, then the IR count once the SD card has been read (`displayBootDone()`). It stays up for 1.5 s from power-on, overlapping the rest of `setup()` (about 1 s from cold), and input is ignored meanwhile.

### main.cpp

```cpp
void setup() {
  controlsBegin();
  loadSettings();
  displayBegin();      // the splash at once (the display's own bus)
  audioBegin();        // codec, effects, the saved settings
  // no Serial.begin(): it waits up to 2 s for a serial monitor
  ...                  // SD scan, startup IR, drum song
  displayBootDone();   // the splash shows the IR count
}

void loop() {
  serviceSerial();
  serviceDisplay();    // controls, redraws, at most one page sent
  serviceMeter();
  serviceSave();
}
```

## RAM1 strategy

On the Teensy 4, RAM1 is shared between code (ITCM) and data plus the stack (DTCM), in 32 KB banks. The core's linker script copies every function into ITCM and every constant into DTCM, so a library's cold code costs RAM1 just like the audio path. Before 2026-10-29 that left only about 1 KB of headroom: ITCM sat at 4 banks, and one more bank would have cut the stack to under 10 KB.

**What is done: placement by file, in one linker script.** `tools/imxrt1062_cold.ld` (set with `board_build.ldscript` in `platformio.ini`) is the core's `imxrt1062.ld` with a list of files whose code and constants stay in flash. They run through the 32 KB instruction cache, which is plenty for code outside the audio interrupt. The list covers:

- newlib's printf, strtod, malloc and qsort (except `mem*` and `str*`);
- SdFat, SD, Wire, EEPROM and the codec control;
- the UI library (`lib/oledui`, fonts and menu tables included);
- the IR and MIDI loaders;
- this firmware's background-only files (`main`, `SerialUI`, `Settings`, `DisplayUI`, `IRLibrary`, `DrumSongs`, `Oled`).

The audio path stays in ITCM: `AudioGraph` with the convolver and effects, the drum machine's per-sample loops, libm, and the encoder interrupt in `Controls.cpp`. Files that mix both use attributes for their cold functions: `FLASHMEM` in `src`, and `IRDSP_COLD` in `lib/irdsp` (`DrumMachine`, `PluckBass`, `Reverb80`).

| Build | ITCM | RAM1 free for the stack | RAM2 free |
|---|---|---|---|
| Before (2026-10-28) | 130,024 B (4 banks, 1 KB padding) | 35,360 B | 71,712 B |
| Cold-file linker script | 61,736 B (2 banks) | 107,040 B | 71,712 B |
| + 4096-sample IR limit | 61,544 B (2 banks) | 205,344 B | 88,096 B |
| + 48 kHz (2026-10-29) | 52,472 B (2 banks) | 195,232 B | 64,384 B |

**Rules**

- Never add a file to the cold list if anything in it runs in the audio interrupt. After a change, check that the interrupt's functions stay below `0x20000000`: `arm-none-eabi-nm -C -S .pio/build/teensy40/firmware.elf`.
- A new background-only `.cpp` in `src` goes on the list in both the `COLD_TEXT` and `COLD_RODATA` blocks.
- Still avoid `sscanf` (its locale tables) and 64-bit division in the audio path.


## Bring-up (2026-10-28)

- **Bus:** the OLED answered at 0x3C on the shared bus, and the codec still takes `in` / `out` over serial with the display running.
- **Picture:** the right way round with the init as written (`A1`, `C8`).
- **Encoder:** a transition log showed one full, clean quadrature cycle per click (11 → 01 → 00 → 10 → 11, resting at 11), so `ENC_COUNTS_PER_DETENT` is 4. Clockwise counted down, hence `ENC_REVERSE 1`. `CONTROLS_DEBUG` in `DisplayUI.cpp` prints the events on serial for checking another module.
- **Keys:** push, CONFIRM and BACK all fire; push on the IR block opened the list and loaded an IR; CONFIRM started and stopped the drums. Holding the push toggles the block under the cursor.
- **Menus:** values apply live while turning, and BACK while editing restores the old value.
- **Meter:** the OLED meters agree roughly with the serial `m` meter, and `CLIP` shows on the hits where serial reports `CLIP!` (there the input was at 0.0 dBFS, clipping at the codec's converter).
- **Persistence:** settings changed from the panel survive a power cycle.
- **CPU:** unchanged with the meters running (28.7 % average and 31.0 % max with the reverb on, as before). Drums at 240 BPM with crashes and four IR switches: 32.0 % average, 51.8 % max, no clipped samples (the reference is about 52 %).
- **Noise:** none audible on headphones at high gain with the meters moving, compared with the display unplugged. No supply filtering was needed.

## Testing

**Memory and performance**

- Run `teensy_size` after each change and compare ITCM, the stack and RAM2 with the table in [RAM1 strategy](#ram1-strategy).
- Take a baseline `s` before flashing (about 52 % max during a crossfade while the drums play). CPU should be unchanged with the display running, because the UI works in `loop()`, not in the audio interrupt.
- Spin the encoder fast and switch IRs during heavy page flushing. There should be no clicks or dropouts, and the `s` max should not rise.

**Controls**

- Turn the encoder fast while an IR is loading. No steps should be lost.
- Every menu item should match what serial `s` reports after the change.
- Changes made over serial (`dtime 500`, `rv`, …) should appear on the screen within a frame.

**Meter**

- Play hard and compare the OLED meter with the serial `m` meter. They should agree within about 1 dB.
- Drive the input into clipping. `CLIP` should latch on the screen.

**Noise**

- Listen on headphones with no guitar, high gain, and the meters moving, then with the display blanked. There should be no whine that follows the display.
- If there is, add a 10 µF + 100 nF capacitor at the module's VCC pin, or feed the module from its own 3.3 V regulator.
- A firmware fallback: update the meters at 10 Hz, or show them only while a level is near clipping.

**Persistence**

- Settings changed from the panel should survive a power cycle.

## Open choices

- **Bus:** `Wire1` (16/17, 1 MHz) since 2026-10-29; see [Why the display has its own bus](#why-the-display-has-its-own-bus).
