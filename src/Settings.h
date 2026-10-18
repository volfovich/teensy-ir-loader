// Settings.h - persistent settings (last IR, levels, bypass) stored in EEPROM.
#pragma once

#include <stdint.h>

#include "config.h"

struct Settings {
  uint32_t magic;
  char irPath[IR_PATH_MAX];
  float outGainDb;
  float hpVolume;
  uint8_t lineIn;
  uint8_t lineOut;
  uint8_t bypass;
};

extern Settings g_settings;

// Reads the EEPROM; falls back to the config.h defaults if it is blank or invalid.
void loadSettings();
// Debounced: flash-emulated EEPROM should not be written on every knob turn.
void scheduleSave();
// Call from loop(): writes the settings once a scheduled save is due.
void serviceSave();
