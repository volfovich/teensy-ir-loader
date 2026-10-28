// Screens.h - the screens other than the menu, drawn from a plain model so
// they can be rendered on a PC. Layouts: DISPLAY.md, "Screens".
#pragma once

#include <stdint.h>

#include "Canvas.h"
#include "Meter.h"

namespace ui {

struct UiModel {
  uint32_t nowMs = 0;

  // IR files.
  bool sdOk = false;
  int irCount = 0, irCurrent = -1;
  const char* (*irPath)(int idx) = nullptr;

  // The blocks' on/off.
  bool bypass = false, delayOn = false, reverbOn = false, drumsPlaying = false;

  // Drums, shown while they play.
  int drumTempo = 0;                 // the tempo now (trainer)
  int drumBeat = -1, drumBeats = 4;  // the beat sounding (0-based, -1 stopped), beats in the bar

  // Meter screen.
  MeterState in, out;
  int lineIn = 0;
  uint32_t clips = 0;

  // Status.
  float cpu = 0, cpuMax = 0;
  int blocksUsed = 0, blocksMax = 0;
};

// The file name without the folder and the .wav extension.
void irDisplayName(const char* path, char* out, int n);

// The home screen's blocks, in signal order (the drums are mixed in after
// the effects).
enum HomeBlock : uint8_t { kBlockIR, kBlockDelay, kBlockReverb, kBlockDrums, kHomeBlocks };

// Screens in rows of 16 px (the 8x16 font): row r is pages 2r and 2r+1.
void drawSplash(Canvas& c, const UiModel& m);
// Row 0 the IR name; row 1 the blocks, filled when on, the cursor under
// `block`; rows 2-3 the drums' tempo, and the beat while they play.
void drawHome(Canvas& c, const UiModel& m, int block);
// The IR list: entries 0..irCount-1 plus "Rescan SD card" (index irCount).
// marquee: characters the cursor row's name is scrolled by.
void drawIrBrowser(Canvas& c, const UiModel& m, int cursor, int top, int marquee);
static constexpr int kIrBrowserRows = 3;
static constexpr int kIrBrowserNameChars = 12;  // of a name, per row
void drawStatus(Canvas& c, const UiModel& m);
void drawMeterScreen(Canvas& c, const UiModel& m);

}  // namespace ui
