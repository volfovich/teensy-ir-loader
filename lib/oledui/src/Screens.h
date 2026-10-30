// Screens.h - the screens other than the menu, drawn from a plain model so
// they can be rendered on a PC. Layouts: DISPLAY.md, "Screens".
#pragma once

#include <stdint.h>

#include "Canvas.h"
#include "Meter.h"

namespace ui {

struct UiModel {
  uint32_t nowMs = 0;
  bool booting = false;  // the SD card not read yet (the splash shows no count)

  // IR files.
  bool sdOk = false;
  int irCount = 0, irCurrent = -1;
  const char* (*irPath)(int idx) = nullptr;

  // Amp models (.nam files in /AMP).
  int ampCount = 0, ampCurrent = -1;
  const char* (*ampPath)(int idx) = nullptr;

  // The blocks' on/off.
  bool ampOn = false, bypass = false, delayOn = false, reverbOn = false, drumsPlaying = false;

  // Drums.
  const char* drumStyle = "";        // the style's or song's short name
  int drumTempo = 0;                 // the tempo now (trainer)
  int drumBeat = -1, drumBeats = 4;  // the beat sounding (0-based, -1 stopped), beats in the bar

  // Meter screen.
  MeterState in, out;
  int lineIn = 0;
  uint32_t clips = 0;

  // Status.
  float cpu = 0, cpuMax = 0;
  int blocksUsed = 0, blocksMax = 0;

  // Tuner: the note (0 = C .. 11 = B), its octave and how far off, while
  // tunerValid.
  bool tunerValid = false;
  int tunerNote = 0, tunerOctave = 0;
  float tunerCents = 0, tunerHz = 0;
};

// The file name without the folder and the extension (.wav, .nam).
void irDisplayName(const char* path, char* out, int n);
// The same for an amp model, with '_' shown as a space.
void ampDisplayName(const char* path, char* out, int n);

// The home screen's items: the blocks in signal order (the drums are mixed
// in after the effects), then the tempo.
enum HomeBlock : uint8_t { kBlockAmp, kBlockIR, kBlockDelay, kBlockReverb, kBlockDrums, kBlockTempo, kHomeBlocks };

// The file lists (IR, amp models): which one, for drawFileList.
enum class FileList : uint8_t { kIR, kAmp };

// Screens in rows of 16 px (the 8x16 font): row r is pages 2r and 2r+1.
void drawSplash(Canvas& c, const UiModel& m);
// Row 0 the IR name (the amp model's with the cursor on AMP, scrolled by
// `marquee` characters when it is long); row 1 the blocks, filled when on;
// rows 2-3 the tempo big, the style and a box per beat. The cursor is a bar
// under `block`; editingTempo draws the tempo inverted instead.
void drawHome(Canvas& c, const UiModel& m, int block, bool editingTempo = false, int marquee = 0);
// The name row 0 shows for `block` ("" when there is none), and its length
// limit on the row.
void homeName(const UiModel& m, int block, char* out, int n);
static constexpr int kHomeNameChars = 16;
// A file list: entries 0..count-1 plus "Rescan SD card" (index count).
// marquee: characters the cursor row's name is scrolled by.
void drawFileList(Canvas& c, const UiModel& m, FileList list, int cursor, int top, int marquee);
inline void drawIrBrowser(Canvas& c, const UiModel& m, int cursor, int top, int marquee) {
  drawFileList(c, m, FileList::kIR, cursor, top, marquee);
}
// A list's entries, the loaded one (-1: none) and the display name of entry i.
int fileCount(const UiModel& m, FileList list);
int fileCurrent(const UiModel& m, FileList list);
void fileName(const UiModel& m, FileList list, int i, char* out, int n);
// Entry i's name as the list shows it: without the whole words every name in
// the list starts with ("JCM 800 2203 " for a set of captures of one amp).
void listName(const UiModel& m, FileList list, int i, char* out, int n);
static constexpr int kIrBrowserRows = 3;
static constexpr int kIrBrowserNameChars = 12;  // of a name, per row
void drawStatus(Canvas& c, const UiModel& m);
void drawMeterScreen(Canvas& c, const UiModel& m);
// The note big, the cents as a needle on a +-50 bar below it; the note
// inverted within kTunerInTuneCents.
void drawTuner(Canvas& c, const UiModel& m);
static constexpr float kTunerInTuneCents = 3.0f;
// The whole screen while the panel waits: a title ("Loading") and the
// model's name on up to two rows. Drawn once, before a slow job.
void drawBusy(Canvas& c, const char* what, const char* name);

}  // namespace ui
