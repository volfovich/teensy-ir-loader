#include "Settings.h"

#include <Arduino.h>
#include <EEPROM.h>

Settings g_settings;

static const uint32_t kSettingsMagic = 0x49524C32;  // "IRL2"
static const uint32_t kSettingsMagicV1 = 0x49524C31;  // "IRL1": no reverb fields
static uint32_t g_saveAt = 0;  // millis() when a pending save is due (0 = none)

static void defaultReverbSettings() {
  g_settings.reverbOn = DEFAULT_REVERB_ON;
  g_settings.reverbMix = DEFAULT_REVERB_MIX;
  g_settings.reverbSize = DEFAULT_REVERB_SIZE;
  g_settings.reverbTone = DEFAULT_REVERB_TONE;
}

static void defaultSettings() {
  memset(&g_settings, 0, sizeof(g_settings));
  g_settings.magic = kSettingsMagic;
  g_settings.outGainDb = DEFAULT_OUTPUT_GAIN_DB;
  g_settings.hpVolume = DEFAULT_HP_VOLUME;
  g_settings.lineIn = DEFAULT_LINE_IN_LEVEL;
  g_settings.lineOut = DEFAULT_LINE_OUT_LEVEL;
  defaultReverbSettings();
}

void loadSettings() {
  EEPROM.get(0, g_settings);
  // Older firmware: keep the IR and levels, add the reverb defaults.
  if (g_settings.magic == kSettingsMagicV1) {
    g_settings.magic = kSettingsMagic;
    defaultReverbSettings();
  }
  if (g_settings.magic != kSettingsMagic || g_settings.lineIn > 15 ||
      g_settings.lineOut < 13 || g_settings.lineOut > 31 ||
      !(g_settings.outGainDb >= -60.0f && g_settings.outGainDb <= 24.0f) ||
      !(g_settings.hpVolume >= 0.0f && g_settings.hpVolume <= 1.0f) ||
      g_settings.reverbOn > 1 || g_settings.reverbMix > 100 ||
      g_settings.reverbSize > 100 || g_settings.reverbTone > 100) {
    defaultSettings();
  }
  g_settings.irPath[IR_PATH_MAX - 1] = 0;
}

void scheduleSave() { g_saveAt = millis() + 3000; if (!g_saveAt) g_saveAt = 1; }

void serviceSave() {
  if (g_saveAt && (int32_t)(millis() - g_saveAt) >= 0) {
    EEPROM.put(0, g_settings);
    g_saveAt = 0;
  }
}
