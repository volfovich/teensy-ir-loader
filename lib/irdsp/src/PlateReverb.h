// PlateReverb.h - 32-bit float stereo plate reverb (mono in, stereo out).
//
// A port of AudioEffectPlateReverb_F32 from hexefx_audiolib_F32
// (https://github.com/hexeguitar/hexefx_audiolib_F32), an FV-1 style plate:
//
//   in -> 4 input allpasses (L) --+--------------------+
//   in -> 4 input allpasses (R) --|--+                 |
//                                 v  v                 v
//   +-> delay1 -> damp -> allpass2 -> delay2 -> damp -> allpass3 -> delay3
//   |   -> damp -> allpass4 -> delay4 -> damp --+
//   +-------------------------------------------+  (x reverb time each stage)
//
// The output taps are read from the four loop delays at different offsets for
// L and R, which gives a wide, decorrelated stereo image from a mono input.
// The loop delays are modulated by two quadrature LFOs (chorused tail).
//
// Line lengths, taps, gains, filter coefficients, LFO rates and the parameter
// mappings are unchanged from the original (tuned for 44.1 kHz). The original's
// pitch shifter, shimmer and freeze are left out; with their default settings
// they pass the signal through unchanged. Its loop allpass 1 is allocated but
// never used, so it is omitted here.
//
// Original code: Copyright (c) 2020 by Piotr Zapart, www.hexefx.com, MIT
// license. Development of that audio library was funded by PJRC.COM, LLC by
// sales of Teensy and Audio Adaptor boards. Permission is hereby granted, free
// of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to
// the following conditions: The above copyright notice, development funding
// notice, and this permission notice shall be included in all copies or
// substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
// WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
// TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
// THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Threading model: process() runs in the audio interrupt. The setters only
// write single floats, so they may be called from either side.
#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace irdsp {

// Cheap, nearly energy-preserving crossfade (Signalsmith Audio, as used by
// HexeFX mix_pwr): mix 0 -> (wet 0, dry 1), mix 1 -> (wet 1, dry 0).
inline void mixPower(float mix, float* wet, float* dry) {
  mix = mix < 0.0f ? 0.0f : (mix > 1.0f ? 1.0f : mix);
  const float x2 = 1.0f - mix;
  const float a = mix * x2;
  const float b = a * (1.0f + 1.4186f * a);
  const float c = b + mix, d = b + x2;
  *wet = c * c;
  *dry = d * d;
}

class PlateReverb {
 public:
  // Total buffer size for init(), in floats (about 120 KB).
  static constexpr int kMemFloats = (224 + 420 + 856 + 1089) +   // input allpasses L
                                    (156 + 520 + 956 + 1289) +   // input allpasses R
                                    (2905 + 3175 + 2398) +       // loop allpasses 2..4
                                    (3423 + 4589 + 4365 + 3698); // loop delays

  explicit PlateReverb(float sampleRate = 44100.0f) {
    for (int i = 0; i <= 256; i++)
      lfoTable_[i] = (1.0f + sinf(6.2831853f * (float)i / 256.0f)) * (float)kLfoDepth;
    lfo1_.adder = (uint32_t)(1.35f * 4294967295.0f / sampleRate);
    lfo2_.adder = (uint32_t)(1.57f * 4294967295.0f / sampleRate);
  }

  // Hands over the line memory (kMemFloats floats) and clears it.
  void init(float* mem) {
    static const int kInL[4] = {224, 420, 856, 1089};
    static const int kInR[4] = {156, 520, 956, 1289};
    static const int kLoopAp[3] = {2905, 3175, 2398};
    static const int kLoopDly[4] = {3423, 4589, 4365, 3698};
    float* p = mem;
    for (int i = 0; i < 4; i++) p = inL_[i].attach(p, kInL[i]);
    for (int i = 0; i < 4; i++) p = inR_[i].attach(p, kInR[i]);
    for (int i = 0; i < 3; i++) p = loopAp_[i].attach(p, kLoopAp[i]);
    for (int i = 0; i < 4; i++) p = dly_[i].attach(p, kLoopDly[i]);
    mem_ = mem;
    reset();
  }
  bool ready() const { return mem_ != nullptr; }

  // Silences the tank (all lines and filter states).
  void reset() {
    if (mem_) memset(mem_, 0, kMemFloats * sizeof(float));
    for (auto& a : inL_) a.idx = 0;
    for (auto& a : inR_) a.idx = 0;
    for (auto& a : loopAp_) a.idx = 0;
    for (auto& d : dly_) d.idx = 0;
    for (auto& f : loopFlt_) f.clear();
    masterL_.clear();
    masterR_.clear();
    loopOut_ = 0.0f;
  }

  // ---- Parameters, all 0..1 (same mappings as the HexeFX setters) ---------

  // Reverb time.
  void setSize(float n) {
    n = clamp01(n);
    n = 2.0f * n - n * n;
    rvTime_ = 0.2f + n * (kRvTimeMax - 0.2f);
  }
  // Lower values give more distinct echoes in the tail.
  void setDiffusion(float n) { apK_ = 0.005f + clamp01(n) * (0.65f - 0.005f); }
  // Treble loss in the tail.
  void setHiDamp(float n) { loopHiDamp_ = 1.0f - clamp01(n); }
  // Bass loss in the tail (also shortens the maximum time so it cannot clip).
  void setLoDamp(float n) {
    loopLoDamp_ = -clamp01(n);
    rvTimeScaler_ = 1.0f + loopLoDamp_ * 0.12f;
  }
  // Output treble cut.
  void setLowpass(float n) { masterLp_ = 1.0f - clamp01(n); }
  // Output bass cut.
  void setHipass(float n) { masterHp_ = -clamp01(n); }

  // ---- Audio --------------------------------------------------------------

  // Processes n samples of mono input into the wet-only stereo outputs.
  // At the default settings 100 % wet is about as loud as the input
  // (checked by test/host/test_reverb.cpp).
  void process(const float* in, float* outL, float* outR, int n) {
    const float rvt = rvTime_ * rvTimeScaler_;
    const float k = apK_;
    const float hiD = loopHiDamp_, loD = loopLoDamp_;
    const float mLp = masterLp_, mHp = masterHp_;
    Delay& d1 = dly_[0];
    Delay& d2 = dly_[1];
    Delay& d3 = dly_[2];
    Delay& d4 = dly_[3];
    float loopOut = loopOut_;
    for (int i = 0; i < n; i++) {
      lfo1_.acc += lfo1_.adder;
      lfo2_.acc += lfo2_.adder;

      // Input diffusers (the original's 0.5 input gain).
      const float x = in[i] * 0.5f;
      float acc = x;
      for (auto& a : inL_) acc = a.process(acc, k);
      const float inOutL = acc;
      acc = x;
      for (auto& a : inR_) acc = a.process(acc, k);
      const float inOutR = acc;

      // The tank loop.
      acc = d1.process(loopOut + inOutR);
      acc = loopFlt_[0].process(acc, hiD, loD, kBassLossF, kTrebleLossF) * rvt;
      acc = loopAp_[0].process(acc + inOutL, k);
      acc = d2.process(acc);
      acc = loopFlt_[1].process(acc, hiD, loD, kBassLossF, kTrebleLossF) * rvt;
      acc = loopAp_[1].process(acc + inOutR, k);
      acc = d3.process(acc);
      acc = loopFlt_[2].process(acc, hiD, loD, kBassLossF, kTrebleLossF) * rvt;
      acc = loopAp_[2].process(acc + inOutL, k);
      acc = d4.process(acc);
      loopOut = loopFlt_[3].process(acc, hiD, loD, kBassLossF, kTrebleLossF) * rvt;

      // Stereo output taps.
      float l = d1.tap(201) * 0.8f + d2.tap(145) * 0.7f + d3.tap(1897) * 0.6f + d4.tap(280) * 0.5f;
      float r = d1.tap(1897) * 0.8f + d2.tap(1245) * 0.7f + d3.tap(487) * 0.6f + d4.tap(780) * 0.5f;
      outL[i] = masterL_.process(l, mLp, mHp, kMasterHpF, kMasterLpF);
      outR[i] = masterR_.process(r, mLp, mHp, kMasterHpF, kMasterLpF);

      // Modulate the loop delays: move a recent sample (picked by the LFO)
      // to a fixed point 2 x depth back, which wobbles the effective length.
      modulate(d1, lfo1_, 0);
      modulate(d2, lfo1_, 64);  // +90 degrees
      modulate(d3, lfo2_, 0);
      modulate(d4, lfo2_, 64);
    }
    loopOut_ = loopOut;
  }

 private:
  static constexpr int kLfoDepth = 20;  // samples
  static constexpr float kRvTimeMax = 0.97f;
  static constexpr float kBassLossF = 0.06f;
  static constexpr float kTrebleLossF = 0.3f;
  static constexpr float kMasterHpF = 0.08f;
  static constexpr float kMasterLpF = 0.1f;

  static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

  struct Allpass {
    float* bf;
    int len, idx;
    float* attach(float* mem, int n) { bf = mem; len = n; idx = 0; return mem + n; }
    inline float process(float in, float k) {
      const float out = bf[idx] + k * in;
      bf[idx] = in - k * out;
      if (++idx >= len) idx = 0;
      return out;
    }
  };

  // Ring buffer; idx is the write position (it advances once per sample in
  // modulate()), so tap(0) is the newest sample and process() returns the
  // oldest one.
  struct Delay {
    float* bf;
    int len, idx;
    float* attach(float* mem, int n) { bf = mem; len = n; idx = 0; return mem + n; }
    inline float process(float in) {
      const float out = bf[idx];
      bf[idx] = in;
      return out;
    }
    inline int wrap(int i) const { return i < 0 ? i + len : i; }
    inline float tap(int offset) const { return bf[wrap(idx - offset)]; }
    inline float tap(int offset, float frac) const {
      const int r = wrap(idx - offset);
      const int r1 = wrap(r - 1);
      return bf[r] * (1.0f - frac) + bf[r1] * frac;
    }
    inline void write(float v, int offset) { bf[wrap(idx - offset)] = v; }
    inline void advance() { if (++idx >= len) idx = 0; }
  };

  // One-pole low/high shelving pair with per-sample smoothing of the shelf
  // gains toward their targets.
  struct Shelf {
    float lp = 0.0f, hp = 0.0f;
    float hiDamp = 1.0f, loDamp = 0.0f;  // current (smoothed) shelf gains
    void clear() { lp = 0.0f; hp = 0.0f; }
    static inline float slew(float cur, float target) {
      if (cur < target) { cur += kStep; if (cur > target) cur = target; }
      else if (cur > target) { cur -= kStep; if (cur < target) cur = target; }
      return cur;
    }
    inline float process(float in, float hiTarget, float loTarget, float hpF, float lpF) {
      hiDamp = slew(hiDamp, hiTarget);
      loDamp = slew(loDamp, loTarget);
      lp += (in - lp) * lpF;
      const float hi = in - lp;
      hp += (lp - hp) * hpF;
      return lp + hiDamp * hi + loDamp * hp;
    }
    static constexpr float kStep = 0.02f;
  };

  struct Lfo {
    uint32_t acc = 0, adder = 0;
  };

  inline void modulate(Delay& d, const Lfo& lfo, uint32_t phase) {
    const uint32_t i = ((lfo.acc >> 24) + phase) & 0xFF;
    const float f = (float)(lfo.acc & 0x00FFFFFF) * (1.0f / 16777216.0f);
    const float v = lfoTable_[i] + (lfoTable_[i + 1] - lfoTable_[i]) * f;  // 0 .. 2 x depth
    const int off = (int)v;
    d.write(d.tap(off, v - (float)off), 2 * kLfoDepth);
    d.advance();
  }

  Allpass inL_[4], inR_[4], loopAp_[3];
  Delay dly_[4];
  Shelf loopFlt_[4];
  Shelf masterL_, masterR_;
  Lfo lfo1_, lfo2_;
  float lfoTable_[257];
  float* mem_ = nullptr;
  float loopOut_ = 0.0f;

  // Parameters (defaults as in the original's begin()).
  float apK_ = 0.65f;
  float rvTime_ = 0.2f, rvTimeScaler_ = 1.0f;
  float loopHiDamp_ = 1.0f, loopLoDamp_ = 0.0f;
  float masterLp_ = 1.0f, masterHp_ = 0.0f;
};

}  // namespace irdsp
