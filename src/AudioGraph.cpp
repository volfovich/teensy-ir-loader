#include "AudioGraph.h"

#include <Arduino.h>
#include <Audio.h>

#include "Params.h"
#include "config.h"

AudioInputI2S i2sIn;
AudioConvolverIR convolver;
AudioOutputI2S i2sOut;
AudioConnection patchIn(i2sIn, 0, convolver, 0);  // left input = guitar
AudioConnection patchL(convolver, 0, i2sOut, 0);
AudioConnection patchR(convolver, 1, i2sOut, 1);
Codec codec;

FLASHMEM bool Codec::setRate(int hz) {
  const int sysFs = hz == 32000 ? 0 : (hz == 48000 ? 2 : (hz == 96000 ? 3 : 1));
  return write(0x0004, sysFs << 2);  // CHIP_CLK_CTRL
}

// Reverb lines live in the 512 KB OCRAM (DMAMEM): random access, but moving
// them to DTCM only saved 0.3 % CPU.
DMAMEM static float g_reverbMem[AudioConvolverIR::kReverbMemFloats];
// The two delay lines are too big for either RAM bank alone: one goes in
// DTCM, the other in OCRAM.
static const int kDelayLen = (int)(DELAY_MAX_MS * AUDIO_SAMPLE_RATE_EXACT / 1000.0f) + 8;
static float g_delayA[kDelayLen];
DMAMEM static float g_delayB[kDelayLen];
// The bass's strings and its block buffers (13.6 KB).
DMAMEM static float g_bassMem[AudioConvolverIR::kBassBufLen];

FLASHMEM void AudioConvolverIR::begin(float* reverbMem, float* delayA, float* delayB, int delayLen,
                                      float* bassBuf) {
  engine_.setFadeLength((int)(AUDIO_SAMPLE_RATE_EXACT * IR_FADE_MS / 1000.0f));
  __disable_irq();
  reverb_.fx().init(reverbMem);
  delay_.fx().init(delayA, delayB, delayLen);
  __enable_irq();
  drums_.setBassBuffer(bassBuf);
}

FLASHMEM void audioBegin() {
  AudioMemory(AUDIO_MEMORY_BLOCKS);
  convolver.begin(g_reverbMem, g_delayA, g_delayB, kDelayLen, g_bassMem);
  codec.enable();
  codec.setRate((int)AUDIO_SAMPLE_RATE_EXACT);
  codec.inputSelect(AUDIO_INPUT_LINEIN);
  // The delay's fixed voicing (config.h); the rest is the saved settings.
  irdsp::StudioDelay& d = convolver.delay();
  d.setRightRatio(DELAY80_RIGHT_RATIO);
  d.setCross(DELAY80_CROSS);
  d.setBassCut(DELAY80_BASS_CUT);
  d.setModRate(DELAY80_MOD_RATE_HZ);
  d.setDuckThresholdDb(DELAY80_DUCK_THRESHOLD_DB);
  d.setDuckReleaseMs(DELAY80_DUCK_RELEASE_MS);
  applyAllParams();
}
