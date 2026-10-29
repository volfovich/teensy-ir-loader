// Settings.h - the persistent settings in EEPROM: the last IR and the value
// of every saved setting (ui::Param 0 .. kParamCount-1, see ParamDefs.h).
// Read and change them through Params.h.
#pragma once

#include <stdint.h>

#include "ParamDefs.h"
#include "config.h"

struct Settings {
  uint32_t magic;
  uint16_t count;  // values in the record: kParamCount of the firmware that wrote it
  uint16_t reserved;
  char irPath[IR_PATH_MAX];
  int16_t v[ui::kParamCount];
};

extern Settings g_settings;

// Reads the EEPROM. A value that is missing (written by older firmware) or
// out of range gets its default, without touching the others; a blank or
// foreign record gets all defaults.
void loadSettings();
// Debounced: flash-emulated EEPROM should not be written on every knob turn.
void scheduleSave();
// Call from loop(): writes the settings once a scheduled save is due.
void serviceSave();
