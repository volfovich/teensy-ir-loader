# OLED + Encoder Control (plan)

This plan adds a 0.96" SSD1306 OLED module with a rotary encoder (with push switch) and two buttons. It replaces the five optional buttons and makes every serial-terminal function available from the front panel. The serial UI keeps working alongside it.

The UI itself is written and tested on the PC: `lib/oledui` draws every screen and runs the menu and the panel's controls (`test/host/test_ui.cpp`, and `render_ui` renders the screens to PNG). The display driver, the encoder and keys, and the wiring into the firmware are not done yet ([Implementation steps](#implementation-steps)).

## Summary

- **Display:** 128x64 SSD1306 on I2C, sharing the bus with the audio codec (pins 18/19). No extra bus is needed.
- **Inputs:** the encoder and the two buttons reuse the five wires of today's buttons (pins 2, 3, 4, 5, 9).
- **Software:**
  - A small SSD1306 driver of our own, running from flash. It sends only the 128-byte pages that changed.
  - An interrupt-driven encoder.
  - A table-driven menu that calls the existing setters in `AudioGraph`.
- **Memory:** the UI costs about 0.5 KB of RAM1. **No current feature has to be removed.** See [RAM1 strategy](#ram1-strategy).

## Hardware

### Connections

Module pin labels vary between sellers. Common names are given in brackets.

| Module pin | Teensy 4.0 pin | Replaces | Notes |
|---|---|---|---|
| VCC | **3.3 V** | | **Never 5 V.** The module's pull-ups would put 5 V on the Teensy pins, which are not 5 V tolerant. |
| GND | GND | | |
| SDA | **18** (SDA0) | | Shared with the SGTL5000 codec (address 0x0A). |
| SCL | **19** (SCL0) | | Shared with the codec. |
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
  │ SDA ─────────────────────┼──────────────► 18  (shared with codec)
  │ SCL ─────────────────────┼──────────────► 19  (shared with codec)
  │ TRA  (encoder A) ────────┼──────────────► 2
  │ TRB  (encoder B) ────────┼──────────────► 3
  │ PSH  (encoder push) ─────┼──────────────► 4
  │ CON  (confirm) ──────────┼──────────────► 5
  │ BAK  (back) ─────────────┼──────────────► 9
  └──────────────────────────┘
```

Notes:

- **I2C address:** usually 0x3C. Some modules are set to 0x3D with a resistor on the back. It is set in `config.h`.
- **Pull-ups:** the OLED module and the Audio Shield both have I2C pull-ups. In parallel they are still within spec, so nothing needs removing.
- **Encoder and buttons:** the firmware uses the Teensy's internal pull-ups (`INPUT_PULLUP`), with the contacts switching to GND. If the module has its own pull-ups to VCC, that is fine at 3.3 V.
- **Encoder bounce:** if the module has no RC filter on the encoder, a 100 nF capacitor from A and from B to GND helps. The state-table decoder tolerates bounce anyway, so try without first.
- **Physical access:** with the shield stacked, pins 18/19 are reachable on the shield's header or on the Teensy's top header pins.
- **Other free pins**, if needed later: 0, 1, 14, 16, 17, 22. 15 is the shield's volume-pot pad and 6 its memory-chip CS.

### Why share the codec's I2C bus

- The codec uses address 0x0A and the OLED 0x3C, so they don't collide.
- All codec writes (`setLineInLevel` etc.) and all display writes run from `loop()`, never from an interrupt. They can't interrupt each other.
- The SGTL5000 is rated for 400 kHz, so the bus runs at 400 kHz. `Wire` defaults to 100 kHz, so the display driver calls `Wire.setClock(400000)` after `audioBegin()`.
- **Alternative:** `Wire1` on pins 16/17 gives the display its own bus, which can run at 1 MHz. This is only worth doing if the shared bus ever causes trouble.

## Controls

### Home screen

The home screen shows the chain as blocks, IR › DLY › REV + DRM, with a cursor under one of them.

| Input | Action |
|---|---|
| Turn encoder | Move the cursor between the blocks |
| Push encoder | Open the block: the [IR list](#ir-list) for IR, the Delay, Reverb or Drums page of the menu for the others. BACK from there comes back home, with the cursor where it was. |
| Hold encoder (≥ 0.6 s) | The block under the cursor on/off: IR = bypass, DRM = drums start/stop |
| CONFIRM | Drums start/stop, wherever the cursor is |
| BACK | Open the menu (Levels, Meter, Status, Bypass …) |

### Menus

| Input | In a list | While editing a value |
|---|---|---|
| Turn | Move the cursor | Change the value. It applies live, so you hear it while turning. |
| Push / CONFIRM | Enter the submenu, start editing, or run the action | Keep the value and stop editing |
| BACK | Up one level (from the top level, back home) | Cancel: restore the value from before editing |

### IR list, Meter, Status

| Input | IR list | Meter | Status |
|---|---|---|---|
| Turn | Move the cursor | Line-in sensitivity | — |
| Push / CONFIRM | Load the IR under the cursor (or rescan); the list stays open | CONFIRM: reset the clip counter and peak holds | — |
| BACK | Back to where it was opened from (home or the menu) | Back to the menu | Back to the menu |

After 30 s without input, the display returns to the home screen. The meter screen stays, because it is used while playing.

**Acceleration:** when detents come less than 40 ms apart, each step counts ×5. This matters for the delay time (20–750 ms in 5 ms steps) and the output gain.

Short presses act on release, so a short press and a hold on the same button can be told apart. CONFIRM and BACK act on press, because they have no hold function.

## Screens

The small font is 6x8 px, giving 21 characters x 8 rows. The 2x font is 12x16 px, giving 10 characters x 4 rows. The mockups are character grids.

### Home

```
┌─────────────────────┐
│ IR → DLY → REV + DRM│  row 0: the blocks; inverted = on, dotted underline = off
│ ▀▀                  │  row 1: the cursor, a bar under the selected block
│3/12 V30 SM57 Mesa 4x│  row 2: the selected block in a line
│100  rock            │  rows 3-4: the tempo in the 2x font; the style or song,
│     BPM   bar 3     │    and where it is
│■□□□       to 140 BPM│  row 5: beat dots; the song's progress, or the trainer
│IN  ########|...:..:.│  row 6: input meter
│OUT ######.....|.CLIP│  row 7: output meter
└─────────────────────┘
```

- **The blocks** follow the signal: the IR, then the delay, then the reverb. The `+` before DRM shows that the drums are mixed in after the effects, never through them. IR is "on" when not bypassed.
- **Row 2** follows the cursor:
  - IR: `3/12 V30 SM57 Mesa 4x`, or `BYPASS V30 SM57 …` when bypassed. The file name is shown without the folder or the `.wav` extension. With no SD card or no IRs: `No SD card` or `No IRs in / or /IR`.
  - DLY: `380ms 80s mix 30%` (`PP` for ping-pong), with `off` in front when it is off.
  - REV: `80s hall mix 25%`, `80s gated …` or `plate …`, with `off` in front when it is off.
  - DRM: `kit rock  vol 60%`.
- **Rows 3–4:** the tempo now (with the speed trainer, the tempo it has reached), the style's name or the song's title (14 characters), and `stopped`, `count-in`, `bar 3` (a style) or `bar 12/32` (a song).
- **Row 5:**
  - one box per beat of the bar (up to 7), the beat sounding filled; they also count the count-in and are all hollow when stopped.
  - for a song: a progress bar over its bars, with ticks at the loop's first and last bar.
  - for a style with the speed trainer on: its target, `to 140 BPM`.

### Menu list

```
┌─────────────────────┐
│MENU                 │
│> IR                 │   cursor row is drawn inverted
│  Delay          ON  │
│  Reverb        OFF  │
│  Levels             │
│  Bypass        OFF  │
│  Meter              │
│  Status             │
└─────────────────────┘
```

### Value editor

```
┌─────────────────────┐
│DELAY > Time         │
│                     │
│      380 ms         │   2x font
│                     │
│[#########.........] │   position within min..max
│20               750 │
│                     │
│push=ok   back=cancel│
└─────────────────────┘
```

### IR list

Opened by pushing the IR block, or from Menu → IR.

```
┌─────────────────────┐
│IR  3/12             │
│  01 Greenback       │
│  02 V30 Room        │
│>*03 V30 SM57 close  │   * marks the loaded IR, > the cursor
│  04 V30 R121        │
│  05 Alnico Blue     │
│  06 G12H 2x12       │
│  Rescan SD card     │   last entry
└─────────────────────┘
```

It opens on the loaded IR. Push loads the IR under the cursor and the list stays open, so IRs can be compared one after the other. Long names scroll sideways while selected.

### Status (Menu → Status)

```
┌─────────────────────┐
│CPU  31.5% / 52.0%max│
│ dly 3.1%  rev 6.0%  │
│Blocks  12/20        │
│Latency 1.45ms+codec │
│Parts  250 x 32      │
│Clips  0             │
│Gain -6.0dB  HP 50%  │
│In 0   Out 29        │
└─────────────────────┘
```

This screen shows the same numbers as the serial `s` command. It refreshes once a second and resets the max values like `s` does.

## Level meter

### Where the peaks come from

`convolver.takeInputPeak()` and `takeOutputPeak()` reset on read, and the serial meter (`m`) and `s` already use them. If the display read them too, each reader would steal peaks from the other.

The fix is to add a second peak pair for the display to `AudioConvolverIR`: `takeDisplayPeaks(float& in, float& out)`. The audio interrupt updates both pairs. This costs two extra `max` operations per block, which is negligible.

### Home-screen meter (rows 6 and 7)

```
IN  #########|...:..:.
OUT #######.....|.CLIP
    ^        ^  ^  ^ ^
  -48 dB   peak -12 -6 0
```

- **Layout:** each meter is one 8-px row. There are 24 px for the `IN`/`OUT` label and 100 px for the bar.
- **Scale:** linear in dB from −48 to 0 dBFS, about 0.5 dB per pixel. That is plenty for setting levels.
- **Ticks:** 1-px tick marks at −24, −12, −6, −3 and 0 dBFS, drawn in the empty part of the bar.
- **Ballistics:**
  - The bar rises instantly and falls at about 20 dB/s, which is easy to read.
  - A peak-hold marker (a 1-px vertical line) stays for 1.5 s and then falls.
  - All of this is computed on the display side from the raw peaks.
- **Clipping:**
  - `CLIP` appears inverted at the right end of the bar for at least 2 s.
  - It shows when the input exceeds −0.5 dBFS or the output exceeds −0.1 dBFS, the same rule as the serial meter.
  - The clip counter from `convolver.clipCount()` also triggers it, so short clips aren't missed between screen updates.
- **Update rate:** 20–25 Hz, and only rows 6 and 7 are sent. Two 128-byte pages take about 7 ms of I2C at 400 kHz, sent one page per `loop()` pass. This doesn't affect the audio, which runs in its own interrupt.

### Full-screen meter (Menu → Meter)

This screen is for setting the input level (the procedure in the README's *Level and safety* section) without a computer.

```
┌─────────────────────┐
│IN   -7.5 dB  pk -4.2│
│#############|..[ ]..│   [ ] = target zone -6..-3 dBFS
│                     │
│OUT -12.0 dB  pk -9.8│
│##########|..........│
│                     │
│Line-in sens  0      │   turn encoder = change "in 0..15"
│Clips 0     back=exit│
└─────────────────────┘
```

- The bars are 2 rows (16 px) tall, with the numeric dBFS and peak-hold value above each one.
- **Turning the encoder changes the line-in sensitivity** (`setLineInLevel`, 0–15), so you can play and adjust at the same time.
- CONFIRM resets the clip counter and peak holds. BACK exits.

## Menu tree

Every serial command has a place:

| Menu | Item | Range / step | Setter (`AudioGraph.h`) | Serial |
|---|---|---|---|---|
| **IR** | file list (push = load) | — | `loadIRIndex` | `l`, `<n>` |
| | Rescan SD card | action | `scanCard` + `loadStartupIR` | `r` |
| **Delay** | On | toggle | `setDelayOn` | `dl` |
| | Mix | 0–100 %, 1 | `setDelayMix` | `dmix` |
| | Time | 20–750 ms, 5 | `setDelayTimeMs` | `dtime` |
| | Repeats | 0–100 %, 1 | `setDelayFeedback` | `dfb` |
| | Tone | 0–100 %, 1 | `setDelayTone` | `dtone` |
| | Mod | 0–100 %, 1 | `setDelayMod` | `dmod` |
| | Type | ping-pong / studio 80s | `setDelayType` | `dtype` |
| | Duck | 0–100 %, 1 (studio 80s) | `setDelayDuck` | `dduck` |
| | Drive | 0–100 %, 1 (studio 80s) | `setDelayDrive` | `ddrive` |
| | Era | 0–100 %, 1 (studio 80s) | `setDelayEra` | `dera` |
| **Reverb** | On | toggle | `setReverbOn` | `rv` |
| | Type | choice: plate / 80s | `setReverbType` | `rtype` |
| | Mode (80s) | choice: hall / gated | `setReverbMode` | `rmode` |
| | Mix | 0–100 %, 1 | `setReverbMix` | `mix` |
| | Size | 0–100 %, 1 | `setReverbSize` | `size` |
| | Tone | 0–100 %, 1 | `setReverbTone` | `tone` |
| | Pre-delay (80s) | 0–150 ms, 1 | `setReverbPreDelay` | `rpre` |
| | Mod (80s hall) | 0–100 %, 1 | `setReverbMod` | `rmod` |
| | Era (80s) | 0–100 %, 1 | `setReverbEra` | `rera` |
| | Gate (80s gated) | 100–600 ms, 10 | `setReverbGate` | `rgate` |
| **Drums** | Play | toggle | `setDrumsPlaying` | `drum` |
| | Style | 1–8, shown by name | `setDrumStyle` | `drum style` |
| | Song | off or 1–n, shown by name | `selectDrumSong` | `drum song` |
| | Loop from / Loop to (with a song) | off or 1–bars / 1–bars | `setDrumLoop` | `drum loop` |
| | Kit | 1–2, shown by name | `setDrumKit` | `drum kit` |
| | Crash | push steps off / 2 / 4 / 8 / 16 bars | `setDrumCrash` | `drum crash` |
| | Tempo | 40–240 BPM, 1 | `setDrumTempo` | `bpm` |
| | Volume | 0–100 %, 1 | `setDrumVolume` | `drum vol` |
| | Comp | 0–100 %, 1 | `setDrumComp` | `drum comp` |
| | Count-in | push steps off / 1 / 2 / 3 / 4 bars | `setDrumCountIn` | `drum count` |
| | Trainer | page (shows ON/OFF) | | |
| **Drums > Trainer** | On | toggle | `setDrumTrainer` | `drum train` |
| | Step / Every / Target | 1–20 BPM / 1–16 bars / 40–240 BPM | `setDrumTrainer` | `drum train s e t` |
| **Levels** | Output gain | −60 to +24 dB, 0.5 | `setOutputGainDb` | `g` |
| | Line-in sens | 0–15 | `setLineInLevel` | `in` |
| | Line-out | 13–31 (13 = loudest) | `setLineOutLevel` | `out` |
| | Headphones | 0–100 %, 1 | `setHeadphoneVolume` | `hp` |
| **Bypass** | toggle | | `setBypass` | `b` |
| **Meter** | full-screen meter | | — | `m` |
| **Status** | status page | | — | `s` |

`n` / `p` are covered by the IR list (push on the IR block).

## Software architecture

### Files

```
src/Controls.{h,cpp}    replaces Buttons.{h,cpp}: encoder ISR, 3 debounced keys, events
src/Oled.{h,cpp}        minimal SSD1306 driver: init, 1 KB frame buffer, text, bars, dirty pages
src/DisplayUI.{h,cpp}   fills the UiModel and implements MenuHost (g_settings + the setters); feeds
                        the control events to ui::Panel and flushes the pages it draws
lib/oledui/             the UI, Arduino-free (done): Canvas, font, Menu (the table and its engine),
                        Screens (drawing from a UiModel), Meter, Key, Panel (screens and controls)
include/config.h        pin/address defines (replace BUTTON_*)
src/AudioConvolverIR.h  + display peak pair
src/main.cpp            controlsBegin()/displayBegin() in setup(); service calls in loop()
```

### config.h

```cpp
// OLED + encoder module (see DISPLAY.md). OLED_I2C_ADDR -1 disables the display.
#define OLED_I2C_ADDR 0x3C
#define ENC_A_PIN 2
#define ENC_B_PIN 3
#define ENC_PUSH_PIN 4
#define KEY_CONFIRM_PIN 5
#define KEY_BACK_PIN 9
#define ENC_COUNTS_PER_DETENT 4  // most EC11 encoders; try 2 if one click moves two steps
#define ENC_REVERSE 0            // 1 if clockwise moves the wrong way
```

### Controls

- **Encoder:**
  - `attachInterrupt(..., CHANGE)` on A and B feeds a 16-entry quadrature state table into a `volatile int32_t` count.
  - Every Teensy 4 pin supports interrupts.
  - Because it is interrupt-driven, no steps are lost while `loop()` is blocked, for example during an IR load (about 29 ms).
  - The ISR is a few instructions long and stays in ITCM.
- **Keys:** the same 15 ms debounce as today's `Button` struct, extended with press, release and hold (≥ 600 ms) detection.
- **API:** `serviceControls()` turns all of this into a small event queue for the UI: `kTurn(±n)`, `kPush`, `kHold`, `kConfirm`, `kBack`.

### Oled driver

Why our own driver: see [RAM1 strategy](#ram1-strategy). It is about 150 lines.

- **Init:** the standard SSD1306 128x64 init sequence (charge pump on, page addressing mode).
- **Frame buffer:** `DMAMEM uint8_t fb[8][128]` (1 KB in RAM2) and a `uint8_t dirty` bitmask with one bit per page.
- **Drawing:** `clear`, `text(col, row, str, big)`, `invert(row range)`, `hbar(...)` and `vline(...)`. They all draw into the buffer and mark pages dirty.
- **Flushing:**
  - `oledFlushOne()` sends **one dirty page per call**: a 3-byte address command plus 129 bytes of data.
  - The Teensy 4 `Wire` buffer is 136 bytes, so a whole page fits in one transaction.
  - A page takes about 3.3 ms at 400 kHz. A full-screen redraw spreads over 8 `loop()` passes instead of blocking for 25 ms.
- **Font:** a 5x7 ASCII font (95 glyphs x 5 bytes = 475 bytes) in `PROGMEM`. The 2x font is drawn by scaling the same glyphs, so it costs nothing extra.

### DisplayUI

- **Panel:** `ui::Panel` (`lib/oledui/src/Panel.h`) owns which screen is showing and what the controls do on it. `DisplayUI` only passes it the events and calls `draw()` when it returns true. It reads the IR count and the loaded IR from the `UiModel`, and sends everything else through `MenuHost::set`, including its actions (`kIrLoad`, `kIrRescan`, `kMeterReset`).
- **Beat dots:** the model's beat comes from `DrumMachine::beatPos()`, which `AudioConvolverIR` passes through.
- **Table-driven menu.** Each item is one row:

  ```cpp
  struct MenuItem {
    const char* label;          // PSTR(): in flash
    uint8_t kind;               // kSubmenu, kToggle, kValue, kAction, kScreen
    int16_t min, max, step;
    int (*get)();               // reads g_settings
    void (*set)(int);           // calls the AudioGraph setter
    const char* unit;
  };
  ```

  - The table itself is `PROGMEM`.
  - Output gain is stored as half-dB steps (`get` returns `lroundf(g * 2)`, `set` calls `setOutputGainDb(v * 0.5f)`), so every item is an `int`.
- **Setters:** all changes go through the existing setters. They already clamp, apply, and call `scheduleSave()`, which is debounced, so turning the knob doesn't wear the EEPROM.
- **Sync with serial:** the UI keeps a copy of the `Settings` it last drew plus `irCurrent()`. Each pass it compares them (`memcmp`, a few hundred bytes) and redraws when anything changed. Changes made over serial therefore show on the screen with no coupling between the two UIs.
- **Serial echo:** the setters print a confirmation on Serial at every encoder step, for example `Delay time 385 ms`. That is harmless. If it gets noisy, add a `quiet` flag later.
- **Loop budget:** `serviceDisplay()` does at most one page flush (about 3 ms) per call, and meter updates are limited to 25 Hz.

### main.cpp

```cpp
void setup() {
  controlsBegin();
  loadSettings();
  audioBegin();
  displayBegin();      // after audioBegin(): Wire is up; sets 400 kHz, shows a splash
  ...
}

void loop() {
  serviceSerial();
  serviceControls();
  serviceDisplay();    // UI events, redraws, one page flush
  serviceMeter();
  serviceSave();
}
```

## RAM1 strategy

### The problem

These numbers are measured with `teensy_size` and `arm-none-eabi-nm` from the build with the studio 80s delay and option B (below) done:

| | Size |
|---|---|
| ITCM (code in RAM1) | 114,808 B code + 16,264 B padding = **4 x 32 KB banks** |
| RAM1 variables | 351,936 B (`convolver` 200 KB, `g_delayA` 132 KB, …) |
| RAM1 free for the stack | **41,280 B** |
| RAM2 free for malloc | 147,072 B |

On the Teensy 4, RAM1 is shared between code (ITCM) and data (DTCM) in 32 KB banks.

- **More code can push ITCM into a 5th bank.** If it grows by more than about 16 KB, it takes a 5th bank, which comes out of DTCM. The stack would shrink from 41 KB to about 9 KB, which is not safe.
- **Constant data also costs RAM1.** The linker copies `.rodata` into DTCM (`imxrt1062.ld`), so plain `const` tables and string literals use RAM1 unless they are marked `PROGMEM`. On the Teensy 4, `F("...")` is only a cast and does not move the string. `PSTR("...")` does.

So the display must not be added the "normal" way:

- **U8g2:** its fonts are plain `const` on ARM, so they end up in DTCM.
- **Adafruit SSD1306 + GFX:** the library code lands in ITCM and could push ITCM into a 5th bank.

### The options

**No current feature has to be removed.** Option A keeps the UI out of RAM1, and option B is already done. C and D are only for the future.

#### A. Keep the UI out of RAM1 (recommended, part of this plan)

- All UI functions are marked `FLASHMEM`. They run from flash through the 32 KB instruction cache, which is far faster than an I2C display needs.
- The font, the menu table and the strings go in `PROGMEM` / `PSTR()`.
- The frame buffer is `DMAMEM` (RAM2).
- Our own small driver instead of a library, so everything above is under our control.
- **RAM1 cost:** the encoder ISR plus a few state variables, about 0.5 KB. ITCM stays at 4 banks and the stack keeps about 41 KB.

#### B. Move existing non-real-time code to flash (done)

- The code that never runs in the audio interrupt is marked `FLASHMEM`: the serial UI, the SD scan and IR loading, `audioBegin` and the setters, the settings and `setup()`.
- In `lib/irdsp`, which must stay Arduino-free for the host tests, `IRDSP_COLD` (`irdsp_port.h`) does the same. It means `__attribute__((section(".flashmem")))` on the Teensy and nothing on the host. It is on `prepareIR`, the WAV parser and decoder, and the resampler.
- **Exceptions:** GCC can't put constructors (`AudioConvolverIR()`, 1.7 KB) or member functions of templates (`installIR`, 0.5 KB) in a named section, so those two stay in ITCM.
- **Result:** ITCM code went from 128,600 to 114,808 B (−13.8 KB), so there are about 16 KB of headroom inside the 4 banks.
- **Cost:** none measured. IR load time stays at 29–31 ms, and CPU is unchanged. The resampler, which now also runs from flash, only runs for IRs that aren't 44.1 kHz.

#### C. Gain a whole 32 KB bank of RAM1 (only if a future feature needs it)

- Use a custom linker script (`board_build.ldscript` in `platformio.ini`, a copy of `imxrt1062.ld`) that places the `.text` of SdFat/ExFat (about 25 KB) and newlib's printf/strtod (about 20 KB) in flash.
- With B done, ITCM would fit in 3 banks, which gives **+32 KB of DTCM**.
- This is more work to maintain across Teensyduino updates, so don't do it until it is needed.

#### D. Options that do cost something (not needed)

- `DELAY_MAX_MS` 750 → 600 frees about 26 KB of RAM1. Each 100 ms costs about 17.6 KB per delay line.
- Moving `g_delayA` to `DMAMEM` frees 132 KB of RAM1 but leaves only about 15 KB free in RAM2. It is also a bigger change to the delay's memory layout.

## Implementation steps

1. **Driver:** `Oled.{h,cpp}`, init plus "hello" text, sharing `Wire` with the codec. Check that the codec still works by changing `in`/`out` over serial while the display updates.
2. **Controls:** `Controls.{h,cpp}` with the encoder ISR and keys. Print events on serial to check the direction, the counts per detent and the hold timing.
3. **Display peaks:** add the display peak pair to `AudioConvolverIR`.
4. **Home screen:** the blocks, the drums, the meters, and the block controls (`ui::Panel`, done on the PC).
5. **Menus:** menu table, list, value editor, IR list, Status, full-screen Meter (done on the PC).
6. **Remove `Buttons.{h,cpp}`** and the `BUTTON_*` defines. Update the README (hardware table, controls, project layout, *Ideas for next steps*).

Each step builds and runs on its own, so steps can be tested on the hardware one at a time.

## Testing

**Memory and performance**

- Run `teensy_size` after each step. RAM1 free should stay at about 40 KB or more, ITCM should stay within 4 banks (check the `padding` figure), and RAM2 should still have well over 100 KB free.
- Take a baseline `s` before flashing (about 31.5 % average and 52 % max during a crossfade). CPU should be unchanged with the display running, because the UI works in `loop()`, not in the audio interrupt.
- Spin the encoder fast and switch IRs during heavy page flushing. There should be no clicks or dropouts, and the `s` max should not rise.

**Controls**

- Turn the encoder fast while an IR is loading. No steps should be lost.
- Every menu item should match what serial `s` reports after the change.
- Changes made over serial (`dtime 500`, `rv`, …) should appear on the screen within a frame.

**Meter**

- Play hard and compare the OLED meter with the serial `m` meter. They should agree within about 1 dB.
- Drive the input into clipping. `CLIP` should latch on the screen.

**Persistence**

- Settings changed from the panel should survive a power cycle.

## Open choices

These are defaults. Each one is easy to change.

1. **Bus:** shared `Wire` (18/19, 400 kHz) or a separate `Wire1` (16/17, 1 MHz).
2. **Option B:** done before the display work.
