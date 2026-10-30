// NamFile.h - reads the A2-Lite network out of a Neural Amp Modeler .nam
// file (see NAM.md).
//
// A .nam file is JSON. An A2 file is a "SlimmableContainer" holding complete
// WaveNet models of different sizes (A2-Lite: 3 channels, A2-Full: 8); a
// plain "WaveNet" file in the Lite shape is read too. The file is streamed
// through a ByteSource in two passes and never held in memory: the first
// notes where the parts are (JSON doesn't fix the order of keys), the second
// checks the network against the A2 shape (NamA2.h) and reads its weights.
// Full's weights are skipped without being converted.
//
// Portable C++, no Arduino; the firmware wraps an SD File, the host tests a
// FILE* or a memory buffer.
#pragma once

#include <math.h>
#include <stdint.h>

#include "IRLoader.h"  // ByteSource

namespace irdsp {

struct NamInfo {
  char name[48] = "";       // metadata: the model's display name
  char gearMake[32] = "";   // e.g. "Marshall"
  char gearModel[32] = "";  // e.g. "JCM800 2203"
  char gearType[24] = "";   // amp, pedal, pedal_amp, amp_cab, amp_pedal_cab, preamp, studio
  char toneType[24] = "";   // clean, overdrive, crunch, hi_gain, fuzz (some say "neuralampmodeler")
  float sampleRate = 48000.0f;
  // NAN when the file doesn't have them.
  float loudness = NAN;        // dB on the trainer's standard input (the Lite network's own when it has one)
  float inputLevelDbu = NAN;   // dBu of a sine at 0 dBFS when the capture was made (calibrated captures)
  float outputLevelDbu = NAN;  // dBu at the interface that reached 0 dBFS
  bool container = false;      // read from an A2 SlimmableContainer (else a plain WaveNet)
};

// Reads the A2-Lite network's weights (nama2::kWeightCount floats, in file
// order, for NamA2::setWeights) and the file's metadata. Returns false with
// *err set to a message for the user if the file has no A2-Lite network or
// can't be read; weights may then be partly written.
bool readNamA2Lite(ByteSource& src, float* weights, NamInfo* info, const char** err);

}  // namespace irdsp
