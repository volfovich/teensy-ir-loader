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
  int irSaved = -1;  // the IR in the settings (loaded at startup); irCurrent may be one being tried
  const char* (*irPath)(int idx) = nullptr;

  // Amp models (.nam files in /AMP).
  int ampCount = 0, ampCurrent = -1;
  const char* (*ampPath)(int idx) = nullptr;

  // Presets (/PRESETS, PRESETS.md): the recalled one, and whether the sound
  // differs from it. presetName gives "03 Lead".
  int presetCount = 0, presetCurrent = -1;
  int presetSaved = -1;  // the recalled one before the list's tries (the list's mark)
  bool presetEdited = false;
  const char* (*presetName)(int idx) = nullptr;

  // The blocks' on/off.
  bool ampOn = false, gateOn = false, bypass = false, delayOn = false, reverbOn = false;
  bool drumsPlaying = false, trainerOn = false;

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

// The home screen's items in the encoder's order: the preset (row 0), the
// blocks in signal order (row 1), then the drums' tempo, style and speed
// trainer (rows 2-3).
enum HomeBlock : uint8_t {
  kBlockPreset, kBlockAmp, kBlockGate, kBlockIR, kBlockDelay, kBlockReverb, kBlockTempo, kBlockStyle, kBlockTrainer,
  kHomeBlocks
};

// The lists (IR, amp models, presets): which one, for drawFileList.
enum class FileList : uint8_t { kIR, kAmp, kPreset };

// Screens in rows of 16 px (the 8x16 font): row r is pages 2r and 2r+1.
void drawSplash(Canvas& c, const UiModel& m);
// Row 0 the preset ("03 Lead*") while one is recalled, else the IR name;
// the amp model's with the cursor on AMP, the IR's on IR; inverted with the
// cursor on it (PRE), scrolled by `marquee` characters when it is long on
// AMP or PRE; row 1 the blocks, filled when on;
// rows 2-3 the tempo big, the style, a box per beat and the trainer's block.
// The cursor is a bar under `block`; `editing` (kBlockTempo or kBlockStyle,
// -1 = none) draws that item inverted instead.
void drawHome(Canvas& c, const UiModel& m, int block, int editing = -1, int marquee = 0);
// The name row 0 shows for `block` ("" when there is none), and its length
// limit on the row.
void homeName(const UiModel& m, int block, char* out, int n);
static constexpr int kHomeNameChars = 16;
// A list: entries 0..count-1, then its extra rows: "Rescan SD card" (index
// count), and for presets "Save as new" (count) before it.
// marquee: characters the cursor row's name is scrolled by.
void drawFileList(Canvas& c, const UiModel& m, FileList list, int cursor, int top, int marquee);
inline void drawIrBrowser(Canvas& c, const UiModel& m, int cursor, int top, int marquee) {
  drawFileList(c, m, FileList::kIR, cursor, top, marquee);
}
// A list's entries, the loaded one (-1: none) and the display name of entry i.
int fileCount(const UiModel& m, FileList list);
int fileExtraRows(FileList list);
// The characters of a name a row shows (its number prefix aside).
int listNameRoom(FileList list);
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
// A yes/no question: the title ("SAVE?") and the name, push / CONFIRM = yes,
// BACK = no.
void drawConfirm(Canvas& c, const char* title, const char* name);
// The name editor, titled `title`: `name` with the character at `pos`
// inverted, scrolled so it shows; the keys on the last two rows.
void drawRename(Canvas& c, const char* title, const char* name, int pos);
static constexpr int kRenameMax = 24;  // characters (PresetLibrary's limit)
// The editor's characters, in turning order.
extern const char kRenameChars[];

// The whole screen while the panel waits: a title ("Loading") and the
// model's name on up to two rows. Drawn once, before a slow job.
void drawBusy(Canvas& c, const char* what, const char* name);

}  // namespace ui
