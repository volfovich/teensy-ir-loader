// irdsp_blocks.h - small DSP building blocks shared by PlateReverb and
// StereoDelay (ported from hexefx_audiolib_F32, MIT, Piotr Zapart).
#pragma once

#include <math.h>
#include <stdint.h>

namespace irdsp {

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Cheap, nearly energy-preserving crossfade (Signalsmith Audio, as used by
// HexeFX mix_pwr): mix 0 -> (wet 0, dry 1), mix 1 -> (wet 1, dry 0).
inline void mixPower(float mix, float* wet, float* dry) {
  mix = clamp01(mix);
  const float x2 = 1.0f - mix;
  const float a = mix * x2;
  const float b = a * (1.0f + 1.4186f * a);
  const float c = b + mix, d = b + x2;
  *wet = c * c;
  *dry = d * d;
}

// One-pole low/high shelving pair (HexeFX AudioFilterShelvingLPHP):
//   out = lp + hiGain * (in - lp) + loGain * hp
// hiGain 1 / loGain 0 is flat; hiGain < 1 cuts treble, loGain < 0 cuts bass.
// The gains slew toward their targets by 0.02 per sample (no zipper noise).
struct Shelf {
  float lp = 0.0f, hp = 0.0f;
  float hiGain = 1.0f, loGain = 0.0f;  // current (smoothed) shelf gains
  void clear() { lp = 0.0f; hp = 0.0f; }
  static inline float slew(float cur, float target) {
    if (cur < target) { cur += kStep; if (cur > target) cur = target; }
    else if (cur > target) { cur -= kStep; if (cur < target) cur = target; }
    return cur;
  }
  // hpF / lpF are the one-pole coefficients of the bass and treble corners.
  inline float process(float in, float hiTarget, float loTarget, float hpF, float lpF) {
    hiGain = slew(hiGain, hiTarget);
    loGain = slew(loGain, loTarget);
    lp += (in - lp) * lpF;
    const float hi = in - lp;
    hp += (lp - hp) * hpF;
    return lp + hiGain * hi + loGain * hp;
  }
  static constexpr float kStep = 0.02f;
};

// 1 + sin(2 pi i / 256), i = 0..256 (filled on first use; call it once
// outside the audio interrupt, e.g. from a constructor).
inline const float* lfoSineTable() {
  static float table[257];
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i <= 256; i++) table[i] = 1.0f + sinf(6.2831853f * (float)i / 256.0f);
    ready = true;
  }
  return table;
}

// Phase-accumulator sine LFO (HexeFX AudioBasicLfo, in float).
struct Lfo {
  uint32_t acc = 0, adder = 0;
  const float* table = lfoSineTable();
  void setRate(float hz, float sampleRate) { adder = (uint32_t)(hz * 4294967295.0f / sampleRate); }
  inline void tick() { acc += adder; }
  // 0 .. 2 at the given phase offset (64 = 90 degrees).
  inline float value(uint32_t phase) const {
    const uint32_t i = ((acc >> 24) + phase) & 0xFF;
    const float f = (float)(acc & 0x00FFFFFF) * (1.0f / 16777216.0f);
    return table[i] + (table[i + 1] - table[i]) * f;
  }
};

}  // namespace irdsp
