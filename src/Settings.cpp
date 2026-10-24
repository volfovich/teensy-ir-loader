#include "Settings.h"

#include <Arduino.h>
#include <EEPROM.h>

#include "DrumKit.h"
#include "DrumMachine.h"

Settings g_settings;

static const uint32_t kSettingsMagic = 0x49524C41;  // "IRLA"
static const uint32_t kSettingsMagicV9 = 0x49524C39;  // "IRL9": no drum comp
static const uint32_t kSettingsMagicV8 = 0x49524C38;  // "IRL8": no drum songs
static const uint32_t kSettingsMagicV7 = 0x49524C37;  // "IRL7": plate reverb only
static const uint32_t kSettingsMagicV6 = 0x49524C36;  // "IRL6": count-in on/off
static const uint32_t kSettingsMagicV5 = 0x49524C35;  // "IRL5": no kit or crash
static const uint32_t kSettingsMagicV4 = 0x49524C34;  // "IRL4": no drums
static const uint32_t kSettingsMagicV3 = 0x49524C33;  // "IRL3": one delay type
static const uint32_t kSettingsMagicV2 = 0x49524C32;  // "IRL2": no delay fields
static const uint32_t kSettingsMagicV1 = 0x49524C31;  // "IRL1": no reverb or delay fields
static uint32_t g_saveAt = 0;  // millis() when a pending save is due (0 = none)

FLASHMEM static void defaultReverbSettings() {
  g_settings.reverbOn = DEFAULT_REVERB_ON;
  g_settings.reverbMix = DEFAULT_REVERB_MIX;
  g_settings.reverbSize = DEFAULT_REVERB_SIZE;
  g_settings.reverbTone = DEFAULT_REVERB_TONE;
}

FLASHMEM static void defaultDelaySettings() {
  g_settings.delayOn = DEFAULT_DELAY_ON;
  g_settings.delayMix = DEFAULT_DELAY_MIX;
  g_settings.delayFeedback = DEFAULT_DELAY_FEEDBACK;
  g_settings.delayTone = DEFAULT_DELAY_TONE;
  g_settings.delayMod = DEFAULT_DELAY_MOD;
  g_settings.delayTimeMs = DEFAULT_DELAY_TIME_MS;
}

FLASHMEM static void defaultDelayTypeSettings() {
  g_settings.delayType = DEFAULT_DELAY_TYPE;
  g_settings.delayDuck = DEFAULT_DELAY_DUCK;
  g_settings.delayDrive = DEFAULT_DELAY_DRIVE;
  g_settings.delayEra = DEFAULT_DELAY_ERA;
}

FLASHMEM static void defaultDrumSettings() {
  g_settings.drumStyle = DEFAULT_DRUM_STYLE - 1;
  g_settings.drumTempo = DEFAULT_DRUM_TEMPO;
  g_settings.drumVolume = DEFAULT_DRUM_VOLUME;
  g_settings.drumCountIn = DEFAULT_DRUM_COUNT_IN;
  g_settings.trainerOn = DEFAULT_TRAINER_ON;
  g_settings.trainerStep = DEFAULT_TRAINER_STEP;
  g_settings.trainerEvery = DEFAULT_TRAINER_EVERY;
  g_settings.trainerTarget = DEFAULT_TRAINER_TARGET;
}

FLASHMEM static void defaultKitSettings() {
  g_settings.drumKit = DEFAULT_DRUM_KIT - 1;
  g_settings.drumCrash = DEFAULT_DRUM_CRASH;
}

FLASHMEM static void defaultReverb80Settings() {
  g_settings.reverbType = DEFAULT_REVERB_TYPE;
  g_settings.reverbMode = DEFAULT_REVERB_MODE;
  g_settings.reverbPreDelay = DEFAULT_REVERB_PREDELAY;
  g_settings.reverbMod = DEFAULT_REVERB_MOD;
  g_settings.reverbEra = DEFAULT_REVERB_ERA;
  g_settings.reverbGateMs = DEFAULT_REVERB_GATE;
}

FLASHMEM static void defaultSettings() {
  memset(&g_settings, 0, sizeof(g_settings));
  g_settings.magic = kSettingsMagic;
  g_settings.outGainDb = DEFAULT_OUTPUT_GAIN_DB;
  g_settings.hpVolume = DEFAULT_HP_VOLUME;
  g_settings.lineIn = DEFAULT_LINE_IN_LEVEL;
  g_settings.lineOut = DEFAULT_LINE_OUT_LEVEL;
  defaultReverbSettings();
  defaultDelaySettings();
  defaultDelayTypeSettings();
  defaultDrumSettings();
  defaultKitSettings();
  defaultReverb80Settings();
  g_settings.drumComp = DEFAULT_DRUM_COMP;
}

FLASHMEM void loadSettings() {
  EEPROM.get(0, g_settings);
  // Older firmware: keep what it stored, add defaults for the newer fields.
  if (g_settings.magic == kSettingsMagicV1) {
    g_settings.magic = kSettingsMagicV2;
    defaultReverbSettings();
  }
  if (g_settings.magic == kSettingsMagicV2) {
    g_settings.magic = kSettingsMagicV3;
    defaultDelaySettings();
  }
  if (g_settings.magic == kSettingsMagicV3) {
    g_settings.magic = kSettingsMagicV4;
    defaultDelayTypeSettings();
  }
  if (g_settings.magic == kSettingsMagicV4) {
    g_settings.magic = kSettingsMagicV5;
    defaultDrumSettings();
  }
  if (g_settings.magic == kSettingsMagicV5) {
    g_settings.magic = kSettingsMagicV6;
    defaultKitSettings();
  }
  if (g_settings.magic == kSettingsMagicV6) {
    // The count-in became a number of bars: "on" becomes the default length.
    g_settings.magic = kSettingsMagicV7;
    if (g_settings.drumCountIn) g_settings.drumCountIn = DEFAULT_DRUM_COUNT_IN;
  }
  if (g_settings.magic == kSettingsMagicV7) {
    g_settings.magic = kSettingsMagicV8;
    defaultReverb80Settings();
  }
  if (g_settings.magic == kSettingsMagicV8) {
    g_settings.magic = kSettingsMagicV9;
    g_settings.drumSong = 0;
  }
  if (g_settings.magic == kSettingsMagicV9) {
    g_settings.magic = kSettingsMagic;
    g_settings.drumComp = DEFAULT_DRUM_COMP;
  }
  // The style and kit lists can change between firmware versions: one that
  // no longer exists falls back to the default, without resetting the rest.
  if (g_settings.magic == kSettingsMagic) {
    if (g_settings.drumStyle >= irdsp::drumStyleCount()) g_settings.drumStyle = DEFAULT_DRUM_STYLE - 1;
    if (g_settings.drumKit >= irdsp::drumKitCount()) g_settings.drumKit = 0;
    const uint8_t c = g_settings.drumCrash;
    if (c != 0 && c != 2 && c != 4 && c != 8 && c != 16) g_settings.drumCrash = DEFAULT_DRUM_CRASH;
  }
  if (g_settings.magic != kSettingsMagic || g_settings.lineIn > 15 ||
      g_settings.lineOut < 13 || g_settings.lineOut > 31 ||
      !(g_settings.outGainDb >= -60.0f && g_settings.outGainDb <= 24.0f) ||
      !(g_settings.hpVolume >= 0.0f && g_settings.hpVolume <= 1.0f) ||
      g_settings.reverbOn > 1 || g_settings.reverbMix > 100 ||
      g_settings.reverbSize > 100 || g_settings.reverbTone > 100 ||
      g_settings.delayOn > 1 || g_settings.delayMix > 100 || g_settings.delayFeedback > 100 ||
      g_settings.delayTone > 100 || g_settings.delayMod > 100 ||
      g_settings.delayTimeMs < 20 || g_settings.delayTimeMs > DELAY_MAX_MS ||
      g_settings.delayType > 1 || g_settings.delayDuck > 100 || g_settings.delayDrive > 100 ||
      g_settings.delayEra > 100 || g_settings.drumTempo < 40 || g_settings.drumTempo > 240 || g_settings.drumVolume > 100 ||
      g_settings.drumCountIn > 4 || g_settings.drumComp > 100 || g_settings.reverbType > 1 || g_settings.reverbMode > 1 ||
      g_settings.reverbPreDelay > 150 || g_settings.reverbMod > 100 || g_settings.reverbEra > 100 ||
      g_settings.reverbGateMs < 100 || g_settings.reverbGateMs > 600 || g_settings.trainerOn > 1 || g_settings.trainerStep < 1 ||
      g_settings.trainerStep > 20 || g_settings.trainerEvery < 1 || g_settings.trainerEvery > 16 ||
      g_settings.trainerTarget < 40 || g_settings.trainerTarget > 240) {
    defaultSettings();
  }
  g_settings.irPath[IR_PATH_MAX - 1] = 0;
}

void scheduleSave() { g_saveAt = millis() + 3000; if (!g_saveAt) g_saveAt = 1; }

FLASHMEM void serviceSave() {
  if (g_saveAt && (int32_t)(millis() - g_saveAt) >= 0) {
    EEPROM.put(0, g_settings);
    g_saveAt = 0;
  }
}
