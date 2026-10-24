// Settings.h - persistent settings (last IR, levels, bypass, effects) stored in EEPROM.
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
  // Added in "IRL3"; older records are upgraded with the delay defaults.
  uint8_t delayOn;
  uint8_t delayMix;       // 0 .. 100
  uint8_t delayFeedback;  // 0 .. 100
  uint8_t delayTone;      // 0 .. 100
  uint8_t delayMod;       // 0 .. 100
  uint16_t delayTimeMs;   // 20 .. DELAY_MAX_MS
  // Added in "IRL4"; older records are upgraded with the defaults.
  uint8_t delayType;   // 0 = ping-pong, 1 = studio 80s
  uint8_t delayDuck;   // 0 .. 100 (studio 80s only)
  uint8_t delayDrive;  // 0 .. 100 (studio 80s only)
  uint8_t delayEra;    // 0 .. 100 (studio 80s only)
  // Added in "IRL5"; older records are upgraded with the defaults.
  uint8_t drumStyle;      // 0-based style index
  uint8_t drumVolume;     // 0 .. 100
  uint8_t drumCountIn;    // 0 / 1
  uint8_t trainerOn;      // 0 / 1
  uint8_t trainerStep;    // 1 .. 20 BPM
  uint8_t trainerEvery;   // 1 .. 16 bars
  uint16_t drumTempo;     // 40 .. 240 BPM
  uint16_t trainerTarget; // 40 .. 240 BPM
};

extern Settings g_settings;

// Reads the EEPROM; falls back to the config.h defaults if it is blank or invalid.
void loadSettings();
// Debounced: flash-emulated EEPROM should not be written on every knob turn.
void scheduleSave();
// Call from loop(): writes the settings once a scheduled save is due.
void serviceSave();
