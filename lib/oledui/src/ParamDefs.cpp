#include "ParamDefs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace ui {

// ---- The table -------------------------------------------------------------------
//
// Defaults are the values a blank EEPROM starts with. Ranges whose end depends
// on the firmware (the delay's longest time, how many styles, kits, songs and
// bars there are) are narrowed by MenuHost::limits / the firmware's limits.

static const char* const kReverbModes[] = {"hall", "gated"};
static const char* const kCrashNames[] = {"off", "2 bars", "4 bars", "8 bars", "16 bars"};
static const char* const kCountNames[] = {"off", "1 bar", "2 bars", "3 bars", "4 bars"};
static const char* const kKeyNames[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

using U = Unit;

// In Param order.
static const ParamDef kParams[kValueCount] = {
    // key           name                  label           min   max  def step unit        choices
    {"dl",           "Delay",              "On",             0,    1,   0, 1, U::kOnOff,   nullptr},
    {"dmix",         "Delay mix",          "Mix",            0,  100,  30, 1, U::kPercent, nullptr},
    {"dtime",        "Delay time",         "Time",          20,  750, 380, 5, U::kMs,      nullptr},
    {"dfb",          "Delay repeats",      "Repeats",        0,  100,  35, 1, U::kPercent, nullptr},
    {"dtone",        "Delay tone",         "Tone",           0,  100,  60, 1, U::kPercent, nullptr},
    {"dmod",         "Delay modulation",   "Mod",            0,  100,  15, 1, U::kPercent, nullptr},
    {"dduck",        "Delay ducking",      "Duck",           0,  100,  40, 1, U::kPercent, nullptr},
    {"ddrive",       "Delay drive",        "Drive",          0,  100,  20, 1, U::kPercent, nullptr},
    {"dera",         "Delay era",          "Era",            0,  100,  40, 1, U::kPercent, nullptr},
    {"rv",           "Reverb",             "On",             0,    1,   0, 1, U::kOnOff,   nullptr},
    {"mix",          "Reverb mix",         "Mix",            0,  100,  25, 1, U::kPercent, nullptr},
    {"size",         "Reverb size",        "Size",           0,  100,  50, 1, U::kPercent, nullptr},
    {"tone",         "Reverb tone",        "Tone",           0,  100,  50, 1, U::kPercent, nullptr},
    {"rmode",        "Reverb mode",        "Mode",           0,    1,   0, 1, U::kNone,    kReverbModes},
    {"rpre",         "Reverb pre-delay",   "Pre-delay",      0,  150,  40, 1, U::kMs,      nullptr},
    {"rmod",         "Reverb modulation",  "Mod",            0,  100,  40, 1, U::kPercent, nullptr},
    {"rera",         "Reverb era",         "Era",            0,  100,  40, 1, U::kPercent, nullptr},
    {"rgate",        "Reverb gate",        "Gate",         100,  600, 300, 10, U::kMs,     nullptr},
    {"g",            "Output gain",        "Out gain",    -120,   48, -12, 1, U::kHalfDb,  nullptr},
    {"in",           "Line-in level",      "Line-in sens",   0,   15,   0, 1, U::kNone,    nullptr},
    {"out",          "Line-out level",     "Line-out",      13,   31,  29, 1, U::kNone,    nullptr},
    {"hp",           "Headphones",         "Headphones",     0,  100,  50, 1, U::kPercent, nullptr},
    {"b",            "Bypass",             "Bypass",         0,    1,   0, 1, U::kOnOff,   nullptr},
    {"drum style",   "Drum style",         "Style",          0,   63,   1, 1, U::kName,    nullptr},
    {"drum kit",     "Drum kit",           "Kit",            0,   63,   1, 1, U::kName,    nullptr},
    {"drum song",    "Drum song",          "Song",           0,  255,   0, 1, U::kName,    nullptr},
    {"drum crash",   "Crash",              "Crash",          0,    4,   2, 1, U::kNone,    kCrashNames},
    {"bpm",          "Tempo",              "Tempo",         40,  240, 100, 1, U::kBpm,     nullptr},
    {"drum vol",     "Drum volume",        "Volume",         0,  100,  60, 1, U::kPercent, nullptr},
    {"drum comp",    "Drum compression",   "Comp",           0,  100,  40, 1, U::kPercent, nullptr},
    {"drum count",   "Count-in",           "Count-in",       0,    4,   2, 1, U::kNone,    kCountNames},
    {"drum train",   "Speed trainer",      "On",             0,    1,   0, 1, U::kOnOff,   nullptr},
    {"train step",   "Trainer step",       "Step",           1,   20,   5, 1, U::kBpm,     nullptr},
    {"train every",  "Trainer every",      "Every",          1,   16,   4, 1, U::kBars,    nullptr},
    {"train target", "Trainer target",     "Target",        40,  240, 140, 1, U::kBpm,     nullptr},
    {"bass",         "Bass",               "On",             0,    1,   1, 1, U::kOnOff,   nullptr},
    {"bass vol",     "Bass volume",        "Volume",         0,  100,  70, 1, U::kPercent, nullptr},
    {"bass key",     "Bass key",           "Key",            0,   11,   4, 1, U::kName,    kKeyNames},
    {"bass tone",    "Bass tone",          "Tone",           0,  100,  50, 1, U::kPercent, nullptr},
    {"amp",          "Amp",                "On",             0,    1,   0, 1, U::kOnOff,   nullptr},
    {"amp in",       "Amp input",          "Input",        -48,   48,   0, 1, U::kHalfDb,  nullptr},
    {"amp out",      "Amp output",         "Output",       -48,   48,   0, 1, U::kHalfDb,  nullptr},
    {"amp cal",      "Amp calibration",    "Calibrate",      0,    1,   1, 1, U::kOnOff,   nullptr},
    {"amp gate",     "Amp noise gate",     "Gate",         -91,  -30, -64, 1, U::kDbOff,   nullptr},
    {"amp bass",     "Amp bass",           "Bass",         -24,   24,   0, 1, U::kHalfDb,  nullptr},
    {"amp mid",      "Amp middle",         "Middle",       -20,   20,   0, 1, U::kHalfDb,  nullptr},
    {"amp treble",   "Amp treble",         "Treble",       -24,   24,   0, 1, U::kHalfDb,  nullptr},
    {"amp presence", "Amp presence",       "Presence",     -20,   20,   0, 1, U::kHalfDb,  nullptr},
    // Not saved.
    {"drum",         "Drums",              "Play",           0,    1,   0, 1, U::kOnOff,   nullptr},
    {nullptr,        "Loop from",          "Loop from",      0,  512,   0, 1, U::kNone,    nullptr},
    {nullptr,        "Loop to",            "Loop to",        1,  512,   1, 1, U::kNone,    nullptr},
};

const ParamDef& paramDef(int id) { return kParams[id < 0 || id >= kValueCount ? 0 : id]; }

// ---- Formatting ------------------------------------------------------------------

// Half-dB steps as "-6.0", "+3.5", "0.0".
static void halfDb(int v, char* buf, int n) {
  const int a = v < 0 ? -v : v;
  snprintf(buf, n, "%s%d.%d", v < 0 ? "-" : (v > 0 ? "+" : ""), a / 2, (a & 1) * 5);
}

void formatValue(const ParamDef& d, int v, bool spaced, char* buf, int n) {
  const char* sp = spaced ? " " : "";
  if (d.choices) {
    snprintf(buf, n, "%s", (v >= d.min && v <= d.max) ? d.choices[v] : "?");
    return;
  }
  switch (d.unit) {
    case Unit::kOnOff: snprintf(buf, n, "%s", v ? "ON" : "OFF"); break;
    case Unit::kPercent: snprintf(buf, n, "%d%s%%", v, sp); break;
    case Unit::kMs: snprintf(buf, n, "%d%sms", v, sp); break;
    case Unit::kBpm: snprintf(buf, n, "%d BPM", v); break;
    case Unit::kBars: snprintf(buf, n, "%d bar%s", v, v == 1 ? "" : "s"); break;
    case Unit::kName: snprintf(buf, n, "%d", v + 1); break;
    case Unit::kDbOff:
      if (v <= d.min) snprintf(buf, n, "OFF");
      else snprintf(buf, n, "%d%sdB", v, sp);
      break;
    case Unit::kHalfDb: {
      char num[16];
      halfDb(v, num, sizeof(num));
      snprintf(buf, n, "%s%sdB", num, sp);
      break;
    }
    default: snprintf(buf, n, "%d", v); break;
  }
}

// ---- Parsing ---------------------------------------------------------------------

// The whole of text is an integer.
static bool parseInt(const char* text, int* v) {
  char* end;
  const long x = strtol(text, &end, 10);
  if (end == text || *end) return false;
  *v = (int)x;
  return true;
}

bool parseValue(const ParamDef& d, const char* text, int* v) {
  if (!text || !*text) return false;
  if (d.unit == Unit::kDbOff && !strcasecmp(text, "off")) return *v = d.min, true;
  if (d.unit == Unit::kOnOff) {
    if (!strcasecmp(text, "on")) return *v = 1, true;
    if (!strcasecmp(text, "off")) return *v = 0, true;
  }
  if (d.choices) {
    const size_t len = strlen(text);
    for (int i = d.min; i <= d.max; i++) {
      const char* c = d.choices[i];
      if (!strcasecmp(c, text) || (!strncasecmp(c, text, len) && c[len] == ' ')) return *v = i, true;
    }
  }
  if (d.unit == Unit::kHalfDb) {
    char* end;
    const double db = strtod(text, &end);
    if (end == text || *end) return false;
    *v = (int)lround(db * 2.0);
    return true;
  }
  return parseInt(text, v);
}

}  // namespace ui
