#include "Settings.h"

#include <Arduino.h>
#include <EEPROM.h>

#include "Params.h"

Settings g_settings;

static const uint32_t kSettingsMagic = 0x49524C51;  // "IRLQ": the amp model, values by ui::Param
static uint32_t g_saveAt = 0;  // millis() when a pending save is due (0 = none)

// The record before the amp model ("IRLP", up to 2026-10-29): the same
// without ampPath, read once to carry its settings over.
static const uint32_t kNoAmpMagic = 0x49524C50;
struct NoAmpSettings {
  uint32_t magic;
  uint16_t count;
  uint16_t reserved;
  char irPath[IR_PATH_MAX];
  int16_t v[ui::kParamCount];  // count of them were written
};

// The record of the firmware before the parameter table ("IRLB", up to
// 2026-10-29), read once to carry its settings over.
static const uint32_t kLegacyMagic = 0x49524C42;
struct LegacySettings {
  uint32_t magic;
  char irPath[IR_PATH_MAX];
  float outGainDb, hpVolume;
  uint8_t lineIn, lineOut, bypass;
  uint8_t reverbOn, reverbMix, reverbSize, reverbTone;
  uint8_t delayOn, delayMix, delayFeedback, delayTone, delayMod;
  uint16_t delayTimeMs;
  uint8_t delayType, delayDuck, delayDrive, delayEra;
  uint8_t drumStyle, drumVolume, drumCountIn, trainerOn, trainerStep, trainerEvery;
  uint16_t drumTempo, trainerTarget;
  uint8_t drumKit, drumCrash;
  uint8_t reverbType, reverbMode, reverbPreDelay, reverbMod, reverbEra;
  uint16_t reverbGateMs;
  uint8_t drumSong, drumComp;
  uint8_t bassOn, bassVolume, bassKey, bassTone;
};

static void fromLegacy(const LegacySettings& o) {
  using namespace ui;
  int16_t* v = g_settings.v;
  memcpy(g_settings.irPath, o.irPath, IR_PATH_MAX);
  memset(g_settings.ampPath, 0, IR_PATH_MAX);
  v[kDelayOn] = o.delayOn;
  v[kDelayMix] = o.delayMix;
  v[kDelayTime] = o.delayTimeMs;
  v[kDelayRepeats] = o.delayFeedback;
  v[kDelayTone] = o.delayTone;
  v[kDelayMod] = o.delayMod;
  v[kDelayDuck] = o.delayDuck;
  v[kDelayDrive] = o.delayDrive;
  v[kDelayEra] = o.delayEra;
  v[kReverbOn] = o.reverbOn;
  v[kReverbMix] = o.reverbMix;
  v[kReverbSize] = o.reverbSize;
  v[kReverbTone] = o.reverbTone;
  v[kReverbMode] = o.reverbMode;
  v[kReverbPreDelay] = o.reverbPreDelay;
  v[kReverbMod] = o.reverbMod;
  v[kReverbEra] = o.reverbEra;
  v[kReverbGate] = o.reverbGateMs;
  v[kOutGain] = (int16_t)lroundf(o.outGainDb * 2.0f);
  v[kLineIn] = o.lineIn;
  v[kLineOut] = o.lineOut;
  v[kHeadphones] = (int16_t)lroundf(o.hpVolume * 100.0f);
  v[kBypass] = o.bypass;
  v[kDrumStyle] = o.drumStyle;
  v[kDrumKit] = o.drumKit;
  v[kDrumSong] = o.drumSong;
  v[kDrumCrash] = o.drumCrash >= 16 ? 4 : (o.drumCrash >= 8 ? 3 : (o.drumCrash >= 4 ? 2 : (o.drumCrash ? 1 : 0)));
  v[kDrumTempo] = o.drumTempo;
  v[kDrumVolume] = o.drumVolume;
  v[kDrumComp] = o.drumComp;
  v[kDrumCountIn] = o.drumCountIn;
  v[kTrainerOn] = o.trainerOn;
  v[kTrainerStep] = o.trainerStep;
  v[kTrainerEvery] = o.trainerEvery;
  v[kTrainerTarget] = o.trainerTarget;
  v[kBassOn] = o.bassOn;
  v[kBassVolume] = o.bassVolume;
  v[kBassKey] = o.bassKey;
  v[kBassTone] = o.bassTone;
  g_settings.count = kAmpOn;  // the settings it had; the amp's get their defaults
}

void loadSettings() {
  EEPROM.get(0, g_settings);
  if (g_settings.magic == kNoAmpMagic) {
    NoAmpSettings old;
    EEPROM.get(0, old);
    memset(&g_settings, 0, sizeof(g_settings));
    memcpy(g_settings.irPath, old.irPath, IR_PATH_MAX);
    g_settings.count = old.count < ui::kParamCount ? old.count : ui::kParamCount;
    memcpy(g_settings.v, old.v, sizeof(int16_t) * g_settings.count);
    g_settings.magic = kSettingsMagic;
    scheduleSave();
  } else if (g_settings.magic == kLegacyMagic) {
    LegacySettings old;
    EEPROM.get(0, old);
    fromLegacy(old);
    g_settings.magic = kSettingsMagic;
    scheduleSave();
  }
  if (g_settings.magic != kSettingsMagic) {
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.magic = kSettingsMagic;
  }
  // Values the record doesn't have, or that are out of range, get their
  // defaults. (The song list isn't read yet: a song that has gone from the
  // card is dropped when it is selected at startup.)
  for (int i = 0; i < ui::kParamCount; i++) {
    int lo = ui::paramDef(i).min, hi = ui::paramDef(i).max;
    if (i != ui::kDrumSong) paramLimits(i, &lo, &hi);
    const int v = g_settings.v[i];
    if (i >= g_settings.count || v < lo || v > hi) g_settings.v[i] = ui::paramDef(i).def;
  }
  if (g_settings.count != ui::kParamCount) {
    g_settings.count = ui::kParamCount;
    scheduleSave();
  }
  g_settings.irPath[IR_PATH_MAX - 1] = 0;
  g_settings.ampPath[IR_PATH_MAX - 1] = 0;
}

void scheduleSave() { g_saveAt = millis() + 3000; if (!g_saveAt) g_saveAt = 1; }

void serviceSave() {
  if (g_saveAt && (int32_t)(millis() - g_saveAt) >= 0) {
    EEPROM.put(0, g_settings);
    g_saveAt = 0;
  }
}
