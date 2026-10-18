// Settings.h - persistent settings (last IR, levels, bypass, reverb) stored in EEPROM.
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
  // Added in "IRL2"; an "IRL1" record is upgraded with the reverb defaults.
  uint8_t reverbOn;
  uint8_t reverbMix;   // 0 .. 100
  uint8_t reverbSize;  // 0 .. 100
  uint8_t reverbTone;  // 0 .. 100
};

extern Settings g_settings;

// Reads the EEPROM; falls back to the config.h defaults if it is blank or invalid.
void loadSettings();
// Debounced: flash-emulated EEPROM should not be written on every knob turn.
void scheduleSave();
// Call from loop(): writes the settings once a scheduled save is due.
void serviceSave();
