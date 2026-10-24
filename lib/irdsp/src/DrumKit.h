// DrumKit.h - sampled drum kits for DrumMachine.
//
// The kits are made from kits/*/ by tools/make_kits.py, which writes the
// table and the samples (16-bit mono, 44.1 kHz, in flash on the Teensy) to
// DrumKitData.cpp. Kit 0 is the synthesized kit: it has no samples.
#pragma once

#include <stdint.h>

namespace irdsp {

enum DrumVoice : uint8_t {
  kKick, kSnare, kRim, kHatClosed, kHatOpen, kRide, kClick, kCrash, kTomHi, kTomLo, kDrumVoices
};

struct DrumSample {
  const int16_t* data;
  uint32_t len;
  uint8_t vel;  // the velocity (1..9) it was recorded / levelled for
};

struct DrumKit {
  char name[10];   // command name ("rock")
  char title[14];  // display name ("rock (AVL)")
  // Per lane, the range in samples[] (sorted by velocity); count 0 =
  // synthesized.
  uint8_t first[kDrumVoices], count[kDrumVoices];
  const DrumSample* samples;
};

int drumKitCount();
const DrumKit& drumKit(int i);
// The kit named (case-insensitive) or numbered 1..count; -1 if none.
int findDrumKit(const char* nameOrNumber);

}  // namespace irdsp
