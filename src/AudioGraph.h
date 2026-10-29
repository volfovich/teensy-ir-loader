// AudioGraph.h - the audio objects (I2S in -> convolver + delay + reverb +
// drums -> I2S out L/R) and the codec. Settings reach them through Params.h.
#pragma once

#include "AudioConvolverIR.h"  // first: Arduino.h, which the codec's header needs
#include <control_sgtl5000.h>

extern AudioConvolverIR convolver;

// The library's enable() tells the codec it runs at 44.1 kHz; setRate()
// sets its CHIP_CLK_CTRL to the real rate (SYS_FS; MCLK stays 256 x Fs).
class Codec : public AudioControlSGTL5000 {
 public:
  bool setRate(int hz);
};
extern Codec codec;

// Starts the audio library and the codec, and applies the saved settings
// (call loadSettings() first). The passthrough runs from here on, even with
// no IR.
void audioBegin();
