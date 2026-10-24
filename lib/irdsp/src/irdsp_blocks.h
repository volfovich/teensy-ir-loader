// irdsp_blocks.h - small DSP building blocks shared by PlateReverb,
// Reverb80, StereoDelay, StudioDelay and DrumMachine (mostly ported from
// hexefx_audiolib_F32, MIT, Piotr Zapart).
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

// 2-pole lowpass / highpass / bandpass (RBJ cookbook, transposed direct
// form II). The coefficients may change between samples; the state carries
// over.
struct Biquad {
  float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
  float z1 = 0.0f, z2 = 0.0f;
  void clear() { z1 = 0.0f; z2 = 0.0f; }
  void setLowpass(float hz, float sampleRate, float q = 0.70710678f) {
    const float w = 6.2831853f * hz / sampleRate;
    const float c = cosf(w), alpha = sinf(w) / (2.0f * q);
    const float inv = 1.0f / (1.0f + alpha);
    b0 = b2 = 0.5f * (1.0f - c) * inv;
    b1 = (1.0f - c) * inv;
    a1 = -2.0f * c * inv;
    a2 = (1.0f - alpha) * inv;
  }
  void setHighpass(float hz, float sampleRate, float q = 0.70710678f) {
    const float w = 6.2831853f * hz / sampleRate;
    const float c = cosf(w), alpha = sinf(w) / (2.0f * q);
    const float inv = 1.0f / (1.0f + alpha);
    b0 = b2 = 0.5f * (1.0f + c) * inv;
    b1 = -(1.0f + c) * inv;
    a1 = -2.0f * c * inv;
    a2 = (1.0f - alpha) * inv;
  }
  // Unity gain at the center frequency.
  void setBandpass(float hz, float sampleRate, float q) {
    const float w = 6.2831853f * hz / sampleRate;
    const float c = cosf(w), alpha = sinf(w) / (2.0f * q);
    const float inv = 1.0f / (1.0f + alpha);
    b0 = alpha * inv;
    b1 = 0.0f;
    b2 = -alpha * inv;
    a1 = -2.0f * c * inv;
    a2 = (1.0f - alpha) * inv;
  }
  inline float process(float x) {
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

// Truncates toward zero to steps of inv (= 1 / scale): the resolution of an
// old converter, e.g. scale 2^9 for 10 bits. |v * scale| must fit in int32.
inline float quantize(float v, float scale, float inv) { return (float)(int32_t)(v * scale) * inv; }

// 4-point Hermite-interpolated read of a circular line of len samples, d
// samples behind the write index idx (HexeFX getTapHermite, which addresses
// the line by write index + (len - d)). d is clamped to 4 .. len - 1. Must
// run before this sample is written.
inline float readHermite(const float* bf, int len, int idx, float d) {
  const float lo = 4.0f, hi = (float)(len - 1);
  d = d < lo ? lo : (d > hi ? hi : d);
  const float p = (float)len - d;
  const int ip = (int)p;
  const float f = p - (float)ip;
  int t = idx + ip;
  if (t >= len) t -= len;
  const int tm1 = t == 0 ? len - 1 : t - 1;
  const int t1 = t + 1 >= len ? t + 1 - len : t + 1;
  const int t2 = t1 + 1 >= len ? t1 + 1 - len : t1 + 1;
  const float xm1 = bf[tm1], x0 = bf[t], x1 = bf[t1], x2 = bf[t2];
  const float c = (x1 - xm1) * 0.5f;
  const float v = x0 - x1;
  const float w = c + v;
  const float a = w + v + (x2 - x0) * 0.5f;
  const float bNeg = w + a;
  return (((a * f) - bNeg) * f + c) * f + x0;
}

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
