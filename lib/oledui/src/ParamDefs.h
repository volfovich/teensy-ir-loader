// ParamDefs.h - every setting in one table: its serial command, its names, its
// range, step, default and how it is shown. The menu, the serial commands,
// the help text, the EEPROM record and its validation all read it, so a
// setting is added in one place (plus what it does, in the firmware's
// applyParam()).
//
// Plain data in flash and Arduino-free, so the host tests use it too.
#pragma once

#include <stdint.h>

namespace ui {

// The settings. 0 .. kParamCount-1 are saved in the EEPROM record in this
// order, so new ones go at the end of that block. After it come values that
// are not saved but can be read and set like the others.
enum Param : uint8_t {
  kDelayOn, kDelayMix, kDelayTime, kDelayRepeats, kDelayTone, kDelayMod, kDelayDuck, kDelayDrive, kDelayEra,
  kReverbOn, kReverbMix, kReverbSize, kReverbTone,
  kReverbMode,  // 0 hall, 1 gated
  kReverbPreDelay, kReverbMod, kReverbEra, kReverbGate,
  kOutGain,  // half-dB steps
  kLineIn, kLineOut, kHeadphones, kBypass,
  kDrumStyle, kDrumKit,  // 0-based
  kDrumSong,             // 0 = off (the style), else 1-based
  kDrumCrash,            // 0 off, 1..4 = every 2, 4, 8, 16 bars
  kDrumTempo, kDrumVolume, kDrumComp,
  kDrumCountIn,  // bars, 0 = off
  kTrainerOn, kTrainerStep, kTrainerEvery, kTrainerTarget,
  kBassOn, kBassVolume,
  kBassKey,  // the styles' key, 0 = C .. 11 = B
  kBassTone,
  kAmpOn,
  kAmpIn, kAmpOut,  // half-dB steps
  kAmpCal,          // use the model's input calibration
  kParamCount,
  // Not saved.
  kDrumsPlay = kParamCount,
  kDrumLoopFrom, kDrumLoopTo,  // bars of the song; from 0 = no loop
  kValueCount,
};

enum class Unit : uint8_t {
  kNone,
  kOnOff,  // a toggle: 0 / 1, shown OFF / ON
  kPercent, kMs, kHalfDb, kBpm, kBars,
  kName,   // shown by name: `choices`, or MenuHost::valueText when it has none
};

struct ParamDef {
  const char* key;    // serial command ("dmix"); nullptr: none
  const char* name;   // in messages and the help ("Delay mix")
  const char* label;  // the menu row ("Mix")
  int16_t min, max, def;
  uint8_t step;       // per detent in the menu editor
  Unit unit;
  const char* const* choices;  // max + 1 names, or nullptr
};

// id < kValueCount.
const ParamDef& paramDef(int id);

// Formats v in a setting's unit: compact for lists ("380ms"), spaced for the
// editor ("380 ms"). Toggles are ON / OFF, choices their name. For kName
// without choices it prints the number from 1.
void formatValue(const ParamDef& d, int v, bool spaced, char* buf, int n);

// Parses a typed value: "on" / "off" for toggles, a choice's name (or the
// first word of it: "8" for "8 bars") or its index, dB with a decimal point
// for kHalfDb, otherwise an integer. Not range-checked. False if it can't be
// read.
bool parseValue(const ParamDef& d, const char* text, int* v);

}  // namespace ui
