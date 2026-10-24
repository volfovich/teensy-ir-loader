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
  uint8_t drumCountIn;    // count-in bars, 0 (off) .. 4 (before "IRL7": 0 / 1)
  uint8_t trainerOn;      // 0 / 1
  uint8_t trainerStep;    // 1 .. 20 BPM
  uint8_t trainerEvery;   // 1 .. 16 bars
  uint16_t drumTempo;     // 40 .. 240 BPM
  uint16_t trainerTarget; // 40 .. 240 BPM
  // Added in "IRL6"; older records are upgraded with the defaults.
  uint8_t drumKit;        // 0-based kit index (0 = synth)
  uint8_t drumCrash;      // crash every 2/4/8/16 bars, 0 = off
  // Added in "IRL8"; older records are upgraded with the defaults.
  uint8_t reverbType;     // 0 = plate, 1 = 80s
  uint8_t reverbMode;     // 80s: 0 = hall, 1 = gated
  uint8_t reverbPreDelay; // 80s: ms, 0 .. 150
  uint8_t reverbMod;      // 80s hall: 0 .. 100
  uint8_t reverbEra;      // 80s: 0 .. 100
  uint16_t reverbGateMs;  // 80s gated: 100 .. 600
  // Added in "IRL9"; older records are upgraded with no song.
  uint8_t drumSong;       // 0 = none (the style), else 1-based (built-in songs, then /DRUMS)
  // Added in "IRLA"; older records are upgraded with the default.
  uint8_t drumComp;       // drum-bus parallel compression, 0 .. 100
};

extern Settings g_settings;

// Reads the EEPROM; falls back to the config.h defaults if it is blank or invalid.
void loadSettings();
// Debounced: flash-emulated EEPROM should not be written on every knob turn.
void scheduleSave();
// Call from loop(): writes the settings once a scheduled save is due.
void serviceSave();
