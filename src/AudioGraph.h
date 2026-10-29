// AudioGraph.h - the audio objects (I2S in -> convolver + delay + reverb +
// drums -> I2S out L/R) and the codec. Settings reach them through Params.h.
#pragma once

#include "AudioConvolverIR.h"

class AudioControlSGTL5000;

extern AudioConvolverIR convolver;
extern AudioControlSGTL5000 codec;

// Starts the audio library and the codec, and applies the saved settings
// (call loadSettings() first). The passthrough runs from here on, even with
// no IR.
void audioBegin();
