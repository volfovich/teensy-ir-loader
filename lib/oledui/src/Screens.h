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

  // Effects.
  bool bypass = false, delayOn = false, reverbOn = false, studioDelay = true;
  bool reverb80 = false, reverbGated = false;
  int delayTimeMs = 0, delayMix = 0, reverbMix = 0;

  // Drums.
  bool drumsPlaying = false, drumsCountingIn = false;
  const char* drumStyle = "";      // the style's name
  const char* drumSong = nullptr;  // the song's title; nullptr: the style plays
  const char* drumKit = "";
  int drumTempo = 0, drumVolume = 0;  // the tempo now (trainer)
  int drumBar = 0;                    // groove bars, from 1 (styles)
  int drumSongBar = 0, drumSongBars = 0;   // songs: the bar from 1, of n
  int drumLoopFrom = 0, drumLoopTo = 0;    // songs: loop bars, 0 = no loop
  int drumBeat = -1, drumBeats = 4;  // the beat sounding (0-based, -1 stopped), beats in the bar
  bool trainerOn = false;
  int trainerTarget = 0;

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

// The home screen's blocks, in signal order (the drums are mixed in after
// the effects).
enum HomeBlock : uint8_t { kBlockIR, kBlockDelay, kBlockReverb, kBlockDrums, kHomeBlocks };

void drawSplash(Canvas& c, const UiModel& m);
// Pages 0-5 of the home screen: the blocks with the cursor under `block`,
// the selected block's details, and the drums (tempo, style or song, beat).
void drawHome(Canvas& c, const UiModel& m, int block);
// Pages 6-7 of the home screen (redrawn at the meter rate).
void drawHomeMeters(Canvas& c, const UiModel& m);
// The IR list: entries 0..irCount-1 plus "Rescan SD card" (index irCount).
// marquee: characters the cursor row's name is scrolled by.
void drawIrBrowser(Canvas& c, const UiModel& m, int cursor, int top, int marquee);
static constexpr int kIrBrowserRows = 7;
static constexpr int kIrBrowserNameChars = 15;  // of a name, per row
void drawStatus(Canvas& c, const UiModel& m);
void drawMeterScreen(Canvas& c, const UiModel& m);

}  // namespace ui
