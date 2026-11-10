// PresetFile.h - a preset's text file (PRESETS.md): one line per setting, the
// serial command's key and its value as typed ("amp in +3.5", "dtime 380",
// "gate type hard"), plus the amp model's and the IR's paths ("amp model
// /AMP/x.nam", "ir /IR/y.wav"). The settings in it are the table's
// kInPreset rows (ParamDefs.h).
//
// Writing and reading go a line at a time, so the firmware streams the SD
// file and the host tests use strings. Plain C++, no Arduino.
#pragma once

#include <stdint.h>

#include "ParamDefs.h"

namespace ui {

constexpr int kPresetPathMax = 96;  // = IR_PATH_MAX (checked in the firmware)

struct PresetData {
  int16_t v[kParamCount];         // the kInPreset ones are meaningful
  char ampPath[kPresetPathMax];  // "" = none
  char irPath[kPresetPathMax];   // "" = none
};

inline bool inPreset(int id) { return id >= 0 && id < kParamCount && (paramDef(id).flags & kInPreset); }

// The table's defaults and no paths: what a missing line leaves.
void presetDefaults(PresetData* p);

// Calls sink once per line (no line end), in table order: a comment, the
// amp model, the IR, then every kInPreset setting.
using PresetLineSink = void (*)(void* ctx, const char* line);
void writePreset(const PresetData& p, PresetLineSink sink, void* ctx);

enum class PresetLine : uint8_t {
  kOk,       // a setting or a path, stored
  kSkip,     // blank or a # comment
  kUnknown,  // a key that isn't a preset's (a newer firmware's): ignored
  kBad,      // a known key whose value can't be read: ignored
};
// Parses one line (a trailing CR or spaces are fine) into p. Values are
// clamped to the table's range, as a serial command would be.
PresetLine parsePresetLine(const char* line, PresetData* p);

// The id whose value differs between a and b, from `from` on, or -1; for
// "pre show" and the edited mark. Paths are compared by the caller.
int presetDiff(const PresetData& a, const PresetData& b, int from);

}  // namespace ui
