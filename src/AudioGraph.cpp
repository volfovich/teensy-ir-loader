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
AudioConnection patchR(convolver, 0, i2sOut, 1);
static AudioControlSGTL5000 codec;

static void applyLevels() {
  codec.lineInLevel(g_settings.lineIn);
  codec.lineOutLevel(g_settings.lineOut);
  codec.volume(g_settings.hpVolume);
  convolver.setOutputGainDb(g_settings.outGainDb);
  convolver.setBypass(g_settings.bypass);
}

void audioBegin() {
  AudioMemory(AUDIO_MEMORY_BLOCKS);
  convolver.engine().setFadeLength((int)(AUDIO_SAMPLE_RATE_EXACT * IR_FADE_MS / 1000.0f));
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

void setHeadphoneVolume(int percent) {
  g_settings.hpVolume = constrain(percent, 0, 100) / 100.0f;
  codec.volume(g_settings.hpVolume);
  scheduleSave();
  Serial.printf("Headphone volume %d%%\n", (int)lroundf(g_settings.hpVolume * 100));
}
