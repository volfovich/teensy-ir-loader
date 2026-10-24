// Screens.h - the screens other than the menu, drawn from a plain model so
// they can be rendered on a PC. Layouts: DISPLAY.md, "Screens".
#pragma once

#include <stdint.h>

#include "Canvas.h"
#include "Meter.h"

namespace ui {

struct UiModel {
  uint32_t nowMs = 0;

  // IR files. irShown is the IR the home screen names: the loaded one, or
  // the one being browsed to (loads when the encoder stops).
  bool sdOk = false;
  int irCount = 0, irCurrent = -1, irShown = -1;
  const char* (*irPath)(int idx) = nullptr;

  // Effects.
  bool bypass = false, delayOn = false, reverbOn = false, studioDelay = true;
  int delayTimeMs = 0, reverbMix = 0;

  // Drums (row 5 of the home screen while playing).
  bool drumsPlaying = false, drumsCountingIn = false;
  const char* drumStyle = "";
  int drumTempo = 0, drumBar = 0;  // the tempo now (trainer), bar from 1

  // Meters.
  MeterState in, out;

  // Status.
  float cpu = 0, cpuMax = 0, cpuDelay = 0, cpuReverb = 0, latencyMs = 0, gainDb = 0;
  int blocksUsed = 0, blocksMax = 0, partitions = 0, blockSize = 0;
  int hpPercent = 0, lineIn = 0, lineOut = 0;
  uint32_t clips = 0;

  // Splash.
  int irMaxLength = 0;
};

// The file name without the folder and the .wav extension.
void irDisplayName(const char* path, char* out, int n);

void drawSplash(Canvas& c, const UiModel& m);
// Pages 0-5 of the home screen (row 5: the drums while they play). blink:
// the blink phase for the IR number while browsing.
void drawHome(Canvas& c, const UiModel& m, bool blink);
// Pages 6-7 of the home screen (redrawn at the meter rate).
void drawHomeMeters(Canvas& c, const UiModel& m);
// The IR list: entries 0..irCount-1 plus "Rescan SD card" (index irCount).
// marquee: characters the cursor row's name is scrolled by.
void drawIrBrowser(Canvas& c, const UiModel& m, int cursor, int top, int marquee);
static constexpr int kIrBrowserRows = 7;
void drawStatus(Canvas& c, const UiModel& m);
void drawMeterScreen(Canvas& c, const UiModel& m);

}  // namespace ui
