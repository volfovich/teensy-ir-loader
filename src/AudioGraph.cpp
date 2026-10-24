#include "AudioGraph.h"

#include <Arduino.h>
#include <Audio.h>

#include "DrumKit.h"
#include "DrumMachine.h"
#include "Settings.h"
#include "config.h"

AudioInputI2S i2sIn;
AudioConvolverIR convolver;
AudioOutputI2S i2sOut;
AudioConnection patchIn(i2sIn, 0, convolver, 0);  // left input = guitar
AudioConnection patchL(convolver, 0, i2sOut, 0);
AudioConnection patchR(convolver, 1, i2sOut, 1);
static AudioControlSGTL5000 codec;

// Reverb delay lines live in the 512 KB OCRAM (DMAMEM); the DTCM is mostly
// taken by the convolver.
DMAMEM static float g_reverbMem[irdsp::PlateReverb::kMemFloats];
// The two delay lines are too big for either RAM bank alone: one goes in
// DTCM, the other in OCRAM.
static const int kDelayLen = (int)(DELAY_MAX_MS * AUDIO_SAMPLE_RATE_EXACT / 1000.0f) + 8;
static float g_delayA[kDelayLen];
DMAMEM static float g_delayB[kDelayLen];

FLASHMEM static void applyLevels() {
  codec.lineInLevel(g_settings.lineIn);
  codec.lineOutLevel(g_settings.lineOut);
  codec.volume(g_settings.hpVolume);
  convolver.setOutputGainDb(g_settings.outGainDb);
  convolver.setBypass(g_settings.bypass);
  convolver.setDelayShape(DELAY_BASS_CUT, DELAY_TREBLE, DELAY_BASS, DELAY_MOD_RATE, DELAY_INERTIA);
  convolver.setDelayTimeMs(g_settings.delayTimeMs);
  convolver.setDelayFeedback(g_settings.delayFeedback / 100.0f);
  convolver.setDelayTone(g_settings.delayTone / 100.0f);
  convolver.setDelayMod(g_settings.delayMod / 100.0f);
  convolver.setDelayStudioShape(DELAY80_RIGHT_RATIO, DELAY80_CROSS, DELAY80_BASS_CUT,
                                DELAY80_MOD_RATE_HZ, DELAY80_DUCK_THRESHOLD_DB,
                                DELAY80_DUCK_RELEASE_MS);
  convolver.setDelayType(g_settings.delayType);
  convolver.setDelayDuck(g_settings.delayDuck / 100.0f);
  convolver.setDelayDrive(g_settings.delayDrive / 100.0f);
  convolver.setDelayEra(g_settings.delayEra / 100.0f);
  convolver.setDelayMix(g_settings.delayMix / 100.0f);
  convolver.setDelayOn(g_settings.delayOn);
  convolver.setReverbShape(REVERB_DIFFUSION, REVERB_LODAMP, REVERB_LOWPASS, REVERB_HIPASS);
  convolver.setReverbSize(g_settings.reverbSize / 100.0f);
  convolver.setReverbTone(g_settings.reverbTone / 100.0f);
  convolver.setReverbMix(g_settings.reverbMix / 100.0f);
  convolver.setReverbOn(g_settings.reverbOn);
  convolver.setDrumStyle(g_settings.drumStyle);
  convolver.setDrumTempo(g_settings.drumTempo);
  convolver.setDrumVolume(g_settings.drumVolume / 100.0f);
  convolver.setDrumCountIn(g_settings.drumCountIn);
  convolver.setDrumKit(g_settings.drumKit);
  convolver.setDrumCrash(g_settings.drumCrash);
  convolver.setDrumTrainer(g_settings.trainerOn, g_settings.trainerStep, g_settings.trainerEvery,
                           g_settings.trainerTarget);
}

FLASHMEM void audioBegin() {
  AudioMemory(AUDIO_MEMORY_BLOCKS);
  convolver.engine().setFadeLength((int)(AUDIO_SAMPLE_RATE_EXACT * IR_FADE_MS / 1000.0f));
  convolver.beginReverb(g_reverbMem);
  convolver.beginDelay(g_delayA, g_delayB, kDelayLen);
  codec.enable();
  codec.inputSelect(AUDIO_INPUT_LINEIN);
  applyLevels();
}

FLASHMEM void setBypass(bool on) {
  g_settings.bypass = on;
  convolver.setBypass(on);
  scheduleSave();
  Serial.println(on ? "BYPASS (dry)" : "IR ON");
}

FLASHMEM void setOutputGainDb(float db) {
  g_settings.outGainDb = constrain(db, -60.0f, 24.0f);
  convolver.setOutputGainDb(g_settings.outGainDb);
  scheduleSave();
  Serial.printf("Output gain %.1f dB\n", g_settings.outGainDb);
}

FLASHMEM void setLineInLevel(int level) {
  g_settings.lineIn = constrain(level, 0, 15);
  codec.lineInLevel(g_settings.lineIn);
  scheduleSave();
  Serial.printf("Line-in level %u\n", g_settings.lineIn);
}

FLASHMEM void setLineOutLevel(int level) {
  g_settings.lineOut = constrain(level, 13, 31);
  codec.lineOutLevel(g_settings.lineOut);
  scheduleSave();
  Serial.printf("Line-out level %u\n", g_settings.lineOut);
}

FLASHMEM void setDelayOn(bool on) {
  g_settings.delayOn = on;
  convolver.setDelayOn(on);
  scheduleSave();
  Serial.println(on ? "DELAY ON" : "DELAY OFF");
}

FLASHMEM void setDelayMix(int percent) {
  g_settings.delayMix = constrain(percent, 0, 100);
  convolver.setDelayMix(g_settings.delayMix / 100.0f);
  scheduleSave();
  Serial.printf("Delay mix %u%% wet\n", g_settings.delayMix);
}

FLASHMEM void setDelayTimeMs(int ms) {
  g_settings.delayTimeMs = constrain(ms, 20, DELAY_MAX_MS);
  convolver.setDelayTimeMs(g_settings.delayTimeMs);
  scheduleSave();
  Serial.printf("Delay time %u ms\n", g_settings.delayTimeMs);
}

FLASHMEM void setDelayFeedback(int percent) {
  g_settings.delayFeedback = constrain(percent, 0, 100);
  convolver.setDelayFeedback(g_settings.delayFeedback / 100.0f);
  scheduleSave();
  Serial.printf("Delay repeats %u%%\n", g_settings.delayFeedback);
}

FLASHMEM void setDelayTone(int percent) {
  g_settings.delayTone = constrain(percent, 0, 100);
  convolver.setDelayTone(g_settings.delayTone / 100.0f);
  scheduleSave();
  Serial.printf("Delay tone %u%%\n", g_settings.delayTone);
}

FLASHMEM void setDelayMod(int percent) {
  g_settings.delayMod = constrain(percent, 0, 100);
  convolver.setDelayMod(g_settings.delayMod / 100.0f);
  scheduleSave();
  Serial.printf("Delay modulation %u%%\n", g_settings.delayMod);
}

const char* delayTypeName(int type) { return type ? "studio 80s" : "ping-pong"; }

FLASHMEM void setDelayType(int type) {
  g_settings.delayType = constrain(type, 0, 1);
  convolver.setDelayType(g_settings.delayType);
  scheduleSave();
  Serial.printf("Delay type %s\n", delayTypeName(g_settings.delayType));
}

FLASHMEM void setDelayDuck(int percent) {
  g_settings.delayDuck = constrain(percent, 0, 100);
  convolver.setDelayDuck(g_settings.delayDuck / 100.0f);
  scheduleSave();
  Serial.printf("Delay ducking %u%%\n", g_settings.delayDuck);
}

FLASHMEM void setDelayDrive(int percent) {
  g_settings.delayDrive = constrain(percent, 0, 100);
  convolver.setDelayDrive(g_settings.delayDrive / 100.0f);
  scheduleSave();
  Serial.printf("Delay drive %u%%\n", g_settings.delayDrive);
}

FLASHMEM void setDelayEra(int percent) {
  g_settings.delayEra = constrain(percent, 0, 100);
  convolver.setDelayEra(g_settings.delayEra / 100.0f);
  scheduleSave();
  Serial.printf("Delay era %u%%\n", g_settings.delayEra);
}

FLASHMEM void setReverbOn(bool on) {
  g_settings.reverbOn = on;
  convolver.setReverbOn(on);
  scheduleSave();
  Serial.println(on ? "REVERB ON" : "REVERB OFF");
}

FLASHMEM void setReverbMix(int percent) {
  g_settings.reverbMix = constrain(percent, 0, 100);
  convolver.setReverbMix(g_settings.reverbMix / 100.0f);
  scheduleSave();
  Serial.printf("Reverb mix %u%% wet\n", g_settings.reverbMix);
}

FLASHMEM void setReverbSize(int percent) {
  g_settings.reverbSize = constrain(percent, 0, 100);
  convolver.setReverbSize(g_settings.reverbSize / 100.0f);
  scheduleSave();
  Serial.printf("Reverb size %u%%\n", g_settings.reverbSize);
}

FLASHMEM void setReverbTone(int percent) {
  g_settings.reverbTone = constrain(percent, 0, 100);
  convolver.setReverbTone(g_settings.reverbTone / 100.0f);
  scheduleSave();
  Serial.printf("Reverb tone %u%%\n", g_settings.reverbTone);
}

FLASHMEM void setDrumsPlaying(bool on) {
  convolver.setDrumsPlaying(on);
  if (!on) {
    Serial.println("DRUMS STOP");
    return;
  }
  Serial.printf("DRUMS START: %s, %u BPM", irdsp::drumStyle(g_settings.drumStyle).title, g_settings.drumTempo);
  if (g_settings.drumCountIn) Serial.printf(", %u-bar count-in", g_settings.drumCountIn);
  Serial.println();
}

FLASHMEM void setDrumStyle(int idx) {
  g_settings.drumStyle = constrain(idx, 0, irdsp::drumStyleCount() - 1);
  convolver.setDrumStyle(g_settings.drumStyle);
  scheduleSave();
  Serial.printf("Drum style %s%s\n", irdsp::drumStyle(g_settings.drumStyle).title,
                convolver.drumsPlaying() ? " (from the next bar)" : "");
}

FLASHMEM void setDrumTempo(int bpm) {
  g_settings.drumTempo = constrain(bpm, 40, 240);
  convolver.setDrumTempo(g_settings.drumTempo);
  scheduleSave();
  Serial.printf("Tempo %u BPM\n", g_settings.drumTempo);
}

FLASHMEM void setDrumVolume(int percent) {
  g_settings.drumVolume = constrain(percent, 0, 100);
  convolver.setDrumVolume(g_settings.drumVolume / 100.0f);
  scheduleSave();
  Serial.printf("Drum volume %u%%\n", g_settings.drumVolume);
}

FLASHMEM void setDrumCountIn(int bars) {
  g_settings.drumCountIn = constrain(bars, 0, 4);
  convolver.setDrumCountIn(g_settings.drumCountIn);
  scheduleSave();
  if (g_settings.drumCountIn)
    Serial.printf("Count-in %u bar%s\n", g_settings.drumCountIn, g_settings.drumCountIn == 1 ? "" : "s");
  else
    Serial.println("Count-in off");
}

FLASHMEM void setDrumKit(int idx) {
  g_settings.drumKit = constrain(idx, 0, irdsp::drumKitCount() - 1);
  convolver.setDrumKit(g_settings.drumKit);
  scheduleSave();
  Serial.printf("Drum kit %s\n", irdsp::drumKit(g_settings.drumKit).title);
}

FLASHMEM void setDrumCrash(int bars) {
  // The nearest of off / 2 / 4 / 8 / 16 bars.
  g_settings.drumCrash = bars <= 0 ? 0 : (bars <= 2 ? 2 : (bars <= 5 ? 4 : (bars <= 11 ? 8 : 16)));
  convolver.setDrumCrash(g_settings.drumCrash);
  scheduleSave();
  if (g_settings.drumCrash)
    Serial.printf("Crash every %u bars%s\n", g_settings.drumCrash, convolver.drumsPlaying() ? " (from the next bar)" : "");
  else
    Serial.println("Crash accents off");
}

FLASHMEM void setDrumTrainer(bool on, int step, int every, int target) {
  g_settings.trainerOn = on;
  g_settings.trainerStep = constrain(step, 1, 20);
  g_settings.trainerEvery = constrain(every, 1, 16);
  g_settings.trainerTarget = constrain(target, 40, 240);
  convolver.setDrumTrainer(on, g_settings.trainerStep, g_settings.trainerEvery, g_settings.trainerTarget);
  scheduleSave();
  if (on)
    Serial.printf("Speed trainer on: +%u BPM every %u bar%s, up to %u BPM\n", g_settings.trainerStep,
                  g_settings.trainerEvery, g_settings.trainerEvery == 1 ? "" : "s", g_settings.trainerTarget);
  else
    Serial.println("Speed trainer off");
}

FLASHMEM void setHeadphoneVolume(int percent) {
  g_settings.hpVolume = constrain(percent, 0, 100) / 100.0f;
  codec.volume(g_settings.hpVolume);
  scheduleSave();
  Serial.printf("Headphone volume %d%%\n", (int)lroundf(g_settings.hpVolume * 100));
}
