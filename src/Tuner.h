// Tuner.h - the tuner: pitch from the input (PitchDetect.h on the audio
// object's tap) while it is open; the guitar is muted meanwhile.
#pragma once

#include <stdint.h>

#include "PitchDetect.h"

struct TunerReading {
  bool valid = false;  // false: no pitch (silence, noise)
  float hz = 0.0f;
  irdsp::Note note;
};

// Opens (taps the input, mutes the guitar) or closes the tuner.
void tunerOpen(bool on);
// Call often while open: a new estimate every 100 ms. True when the reading
// changed.
bool serviceTuner(uint32_t nowMs);
const TunerReading& tunerReading();
