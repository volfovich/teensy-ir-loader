#include "AudioGraph.h"

#include <Arduino.h>
#include <Audio.h>

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

static void applyLevels() {
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
  convolver.setDelayMix(g_settings.delayMix / 100.0f);
  convolver.setDelayOn(g_settings.delayOn);
  convolver.setReverbShape(REVERB_DIFFUSION, REVERB_LODAMP, REVERB_LOWPASS, REVERB_HIPASS);
  convolver.setReverbSize(g_settings.reverbSize / 100.0f);
  convolver.setReverbTone(g_settings.reverbTone / 100.0f);
  convolver.setReverbMix(g_settings.reverbMix / 100.0f);
  convolver.setReverbOn(g_settings.reverbOn);
}

void audioBegin() {
  AudioMemory(AUDIO_MEMORY_BLOCKS);
  convolver.engine().setFadeLength((int)(AUDIO_SAMPLE_RATE_EXACT * IR_FADE_MS / 1000.0f));
  convolver.beginReverb(g_reverbMem);
  convolver.beginDelay(g_delayA, g_delayB, kDelayLen);
  codec.enable();
  codec.inputSelect(AUDIO_INPUT_LINEIN);
  applyLevels();
}

void setBypass(bool on) {
  g_settings.bypass = on;
  convolver.setBypass(on);
  scheduleSave();
  Serial.println(on ? "BYPASS (dry)" : "IR ON");
}

void setOutputGainDb(float db) {
  g_settings.outGainDb = constrain(db, -60.0f, 24.0f);
  convolver.setOutputGainDb(g_settings.outGainDb);
  scheduleSave();
  Serial.printf("Output gain %.1f dB\n", g_settings.outGainDb);
}

void setLineInLevel(int level) {
  g_settings.lineIn = constrain(level, 0, 15);
  codec.lineInLevel(g_settings.lineIn);
  scheduleSave();
  Serial.printf("Line-in level %u\n", g_settings.lineIn);
}

void setLineOutLevel(int level) {
  g_settings.lineOut = constrain(level, 13, 31);
  codec.lineOutLevel(g_settings.lineOut);
  scheduleSave();
  Serial.printf("Line-out level %u\n", g_settings.lineOut);
}

void setDelayOn(bool on) {
  g_settings.delayOn = on;
  convolver.setDelayOn(on);
  scheduleSave();
  Serial.println(on ? "DELAY ON" : "DELAY OFF");
}

void setDelayMix(int percent) {
  g_settings.delayMix = constrain(percent, 0, 100);
  convolver.setDelayMix(g_settings.delayMix / 100.0f);
  scheduleSave();
  Serial.printf("Delay mix %u%% wet\n", g_settings.delayMix);
}

void setDelayTimeMs(int ms) {
  g_settings.delayTimeMs = constrain(ms, 20, DELAY_MAX_MS);
  convolver.setDelayTimeMs(g_settings.delayTimeMs);
  scheduleSave();
  Serial.printf("Delay time %u ms\n", g_settings.delayTimeMs);
}

void setDelayFeedback(int percent) {
  g_settings.delayFeedback = constrain(percent, 0, 100);
  convolver.setDelayFeedback(g_settings.delayFeedback / 100.0f);
  scheduleSave();
  Serial.printf("Delay repeats %u%%\n", g_settings.delayFeedback);
}

void setDelayTone(int percent) {
  g_settings.delayTone = constrain(percent, 0, 100);
  convolver.setDelayTone(g_settings.delayTone / 100.0f);
  scheduleSave();
  Serial.printf("Delay tone %u%%\n", g_settings.delayTone);
}

void setDelayMod(int percent) {
  g_settings.delayMod = constrain(percent, 0, 100);
  convolver.setDelayMod(g_settings.delayMod / 100.0f);
  scheduleSave();
  Serial.printf("Delay modulation %u%%\n", g_settings.delayMod);
}

void setReverbOn(bool on) {
  g_settings.reverbOn = on;
  convolver.setReverbOn(on);
  scheduleSave();
  Serial.println(on ? "REVERB ON" : "REVERB OFF");
}

void setReverbMix(int percent) {
  g_settings.reverbMix = constrain(percent, 0, 100);
  convolver.setReverbMix(g_settings.reverbMix / 100.0f);
  scheduleSave();
  Serial.printf("Reverb mix %u%% wet\n", g_settings.reverbMix);
}

void setReverbSize(int percent) {
  g_settings.reverbSize = constrain(percent, 0, 100);
  convolver.setReverbSize(g_settings.reverbSize / 100.0f);
  scheduleSave();
  Serial.printf("Reverb size %u%%\n", g_settings.reverbSize);
}

void setReverbTone(int percent) {
  g_settings.reverbTone = constrain(percent, 0, 100);
  convolver.setReverbTone(g_settings.reverbTone / 100.0f);
  scheduleSave();
  Serial.printf("Reverb tone %u%%\n", g_settings.reverbTone);
}

void setHeadphoneVolume(int percent) {
  g_settings.hpVolume = constrain(percent, 0, 100) / 100.0f;
  codec.volume(g_settings.hpVolume);
  scheduleSave();
  Serial.printf("Headphone volume %d%%\n", (int)lroundf(g_settings.hpVolume * 100));
}
