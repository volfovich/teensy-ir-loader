// Reverb80.h - 32-bit float stereo "80s" studio reverb with two modes:
//
//   hall   the big rack halls of 80s rock records (Lexicon 224 / 480L): a
//          pre-delay, a dense 8-line feedback delay network with a chorused
//          (modulated) tail, dark highs and lean lows, wide stereo
//   gated  the AMS RMX16 "Nonlin" sound: a dense burst of reverb that stays
//          flat (or decays / rises) for the gate time, then stops dead
//
//   (L+R)/2 -> pre-delay -> era: anti-alias LP + quantize -> 4 allpass diffusers
//     hall:  -> 8-line FDN (Hadamard), per-line damping LP (Tone), decay (Size),
//               4 lines read at an LFO-modulated position (Mod)
//     gated: -> one line, 32 taps per side spread over the gate time, tap
//               gains shaped by Size (decaying / flat / rising), Tone lowpass
//   -> low cut (100 Hz) -> wet L / R
//
// Era is the converters of the period, as in StudioDelay: the wet input's
// bandwidth falls from 16 to 6 kHz and its resolution from 16 to 10 bits.
//
// Line lengths are in samples, tuned for 44.1 kHz. The modes share one block
// of memory; switching mode while running needs a clear (the firmware fades
// out, clears and fades in, as for a type change).
//
// Threading model: process() runs in the audio interrupt. The setters only
// write single values; process() recomputes the coefficients when they
// change.
#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "irdsp_blocks.h"

namespace irdsp {

class Reverb80 {
 public:
  enum Mode : int { kHall = 0, kGated = 1 };

  static constexpr int kPreMax = 6620;  // 150 ms + margin
  static constexpr int kDiffLen[4] = {142, 107, 379, 277};
  static constexpr int kHallLen[8] = {2003, 2341, 2729, 3011, 3313, 3637, 3943, 4271};
  static constexpr int kModPad = 24;    // room for the modulated reads
  static constexpr int kGateMax = 26464;  // 600 ms + margin
  static constexpr int kTaps = 32;      // per side (gated)
  static constexpr int kHallFloats = 2003 + 2341 + 2729 + 3011 + 3313 + 3637 + 3943 + 4271 + 8 * kModPad;
  static constexpr int kSharedFloats = kHallFloats > kGateMax ? kHallFloats : kGateMax;
  // Total buffer size for init(), in floats (about 136 KB).
  static constexpr int kMemFloats = kPreMax + (142 + 107 + 379 + 277) + kSharedFloats;

  explicit Reverb80(float sampleRate = 44100.0f) : fs_(sampleRate) {
    lfo_[0].setRate(0.53f, sampleRate);
    lfo_[1].setRate(0.89f, sampleRate);
  }

  // Hands over the line memory (kMemFloats floats) and clears it.
  void init(float* mem) {
    float* p = mem;
    pre_ = p;
    p += kPreMax;
    for (int i = 0; i < 4; i++) {
      diff_[i].bf = p;
      diff_[i].len = kDiffLen[i];
      p += kDiffLen[i];
    }
    shared_ = p;
    float* q = p;
    for (int i = 0; i < 8; i++) {
      line_[i] = q;
      lineLen_[i] = kHallLen[i] + kModPad;
      q += lineLen_[i];
    }
    mem_ = mem;
    reset();
  }
  bool ready() const { return mem_ != nullptr; }

  // Silences everything at once.
  void reset() {
    clearPos_ = 0;
    clearStep(kMemFloats);
  }

  // Clears up to n floats per call; returns true when the reverb is silent.
  bool clearStep(int n) {
    if (!mem_) return true;
    if (clearPos_ == 0) {
      preIdx_ = 0;
      for (auto& d : diff_) d.idx = 0;
      for (auto& w : lineIdx_) w = 0;
      gateIdx_ = 0;
      for (auto& s : damp_) s = 0.0f;
      inLp_.clear();
      inLp2_.clear();
      hpL_.clear();
      hpR_.clear();
      toneL_.clear();
      toneR_.clear();
    }
    const int count = n < kMemFloats - clearPos_ ? n : kMemFloats - clearPos_;
    memset(mem_ + clearPos_, 0, count * sizeof(float));
    clearPos_ += count;
    if (clearPos_ < kMemFloats) return false;
    clearPos_ = 0;
    return true;
  }

  // ---- Parameters -----------------------------------------------------------

  // Hall or gated. Changing it while sound is in the lines needs a clear.
  void setMode(int m) { mode_ = m == kGated ? kGated : kHall; }
  int mode() const { return mode_; }
  // Hall: decay time, 0..1 -> RT60 0.8 .. 8 s. Gated: the burst's shape,
  // 0 = decaying, 0.5 = flat, 1 = rising.
  void setSize(float n) { size_ = clamp01(n); }
  // Brightness of the tail, 0..1 (2.5 kHz .. 12 kHz, capped by the era).
  void setTone(float n) { tone_ = clamp01(n); }
  void setPreDelayMs(float ms) {
    const float s = ms * 0.001f * fs_;
    preDelay_ = s < 0.0f ? 0 : (s > (float)(kPreMax - 2) ? kPreMax - 2 : (int)s);
  }
  // Chorus depth of the hall's tail, 0..1.
  void setMod(float n) { mod_ = clamp01(n); }
  // Converter era, 0..1: 0 = 16-bit / 16 kHz, 1 = 10-bit / 6 kHz.
  void setEra(float n) { era_ = clamp01(n); }
  // Gated: the burst length, 100 .. 600 ms.
  void setGateMs(float ms) { gateMs_ = ms < 100.0f ? 100.0f : (ms > 600.0f ? 600.0f : ms); }

  // ---- Audio ------------------------------------------------------------------

  // Processes n samples into the wet-only stereo outputs (inL and inR may be
  // the same buffer). At the default settings 100 % wet is about as loud as
  // the input (checked by test/host/test_reverb80.cpp).
  void process(const float* inL, const float* inR, float* outL, float* outR, int n) {
    if (size_ != cSize_ || tone_ != cTone_ || era_ != cEra_ || gateMs_ != cGate_) update();
    const float qs = qScale_, qi = qInv_;
    for (int i = 0; i < n; i++) {
      // Pre-delay, then the converters of the era.
      pre_[preIdx_] = 0.5f * (inL[i] + inR[i]);
      int r = preIdx_ - preDelay_;
      if (r < 0) r += kPreMax;
      if (++preIdx_ >= kPreMax) preIdx_ = 0;
      float x = inLp2_.process(inLp_.process(pre_[r]));
      x = quantize(x, qs, qi);

      float l, rr;
      if (mode_ == kGated) {
        // Short diffusion only, so the burst can stop dead.
        x = diff_[0].process(x, 0.7f);
        x = diff_[1].process(x, 0.7f);
        gated(x, l, rr);
      } else {
        x = diff_[0].process(x, 0.75f);
        x = diff_[1].process(x, 0.75f);
        x = diff_[2].process(x, 0.625f);
        x = diff_[3].process(x, 0.625f);
        hall(x, l, rr);
      }
      outL[i] = hpL_.process(l);
      outR[i] = hpR_.process(rr);
    }
  }

 private:
  struct Allpass {
    float* bf;
    int len, idx = 0;
    inline float process(float in, float k) {
      const float d = bf[idx];
      const float v = in + k * d;
      bf[idx] = v;
      if (++idx >= len) idx = 0;
      return d - k * v;
    }
  };

  // One step of the hall: read the 8 lines (4 of them modulated), damp and
  // decay them, mix with an 8-point Hadamard matrix, add the input.
  inline void hall(float x, float& outL, float& outR) {
    float y[8];
    lfo_[0].tick();
    lfo_[1].tick();
    const float depth = modDepth_;
    for (int k = 0; k < 8; k++) {
      const int len = lineLen_[k], w = lineIdx_[k];
      float v;
      if (k & 1) {
        int r = w - kHallLen[k];
        if (r < 0) r += len;
        v = line_[k][r];
      } else {
        // Lines 0, 2, 4, 6: chorused, two LFOs in quadrature.
        const float m = lfo_[k >> 2].value((uint32_t)(k & 2) * 32u);  // 0 .. 2
        v = readHermite(line_[k], len, w, (float)kHallLen[k] - depth * m);
      }
      damp_[k] += (v - damp_[k]) * dampK_;
      y[k] = damp_[k] * gain_[k];
    }
    // Stereo out: taps along the lines (different points and signs for L
    // and R), so the reverb starts right after the pre-delay and the sides
    // are decorrelated.
    float l = 0.0f, r = 0.0f;
    for (int k = 0; k < 8; k++) {
      const int len = lineLen_[k], w = lineIdx_[k];
      int a = w - kTapL[k], b = w - kTapR[k];
      if (a < 0) a += len;
      if (b < 0) b += len;
      l += (k & 1) ? -line_[k][a] : line_[k][a];
      r += (k & 2) ? -line_[k][b] : line_[k][b];
    }
    outL = kHallOut * l;
    outR = kHallOut * r;
    // Fast Walsh-Hadamard transform (orthogonal with the 1/sqrt(8)).
    for (int h = 1; h < 8; h <<= 1)
      for (int a = 0; a < 8; a += h << 1)
        for (int b = a; b < a + h; b++) {
          const float u = y[b], v = y[b + h];
          y[b] = u + v;
          y[b + h] = u - v;
        }
    for (int k = 0; k < 8; k++) {
      int& w = lineIdx_[k];
      line_[k][w] = y[k] * 0.35355339f + ((k & 1) ? -x : x);
      if (++w >= lineLen_[k]) w = 0;
    }
  }

  // One step of the gated burst: write the diffused input, sum the taps.
  inline void gated(float x, float& outL, float& outR) {
    float* bf = shared_;
    bf[gateIdx_] = x;
    float l = 0.0f, r = 0.0f;
    for (int k = 0; k < kTaps; k++) {
      int a = gateIdx_ - tapL_[k];
      if (a < 0) a += kGateMax;
      int b = gateIdx_ - tapR_[k];
      if (b < 0) b += kGateMax;
      l += bf[a] * gainL_[k];
      r += bf[b] * gainR_[k];
    }
    if (++gateIdx_ >= kGateMax) gateIdx_ = 0;
    outL = kGateOut * toneL_.process(l);
    outR = kGateOut * toneR_.process(r);
  }

  // Recomputes the coefficients after a parameter change.
  void update() {
    cSize_ = size_;
    cTone_ = tone_;
    cEra_ = era_;
    cGate_ = gateMs_;
    const float ceil = 16000.0f * powf(6000.0f / 16000.0f, era_);
    inLp_.setLowpass(ceil, fs_, 0.54f);  // 4th-order Butterworth: steep, like
    inLp2_.setLowpass(ceil, fs_, 1.31f);  // the anti-alias filters of the day
    qScale_ = exp2f(15.0f - 6.0f * era_);
    qInv_ = 1.0f / qScale_;
    float toneHz = 2500.0f * powf(12000.0f / 2500.0f, tone_);
    if (toneHz > ceil) toneHz = ceil;
    dampK_ = 1.0f - expf(-6.2831853f * toneHz / fs_);
    toneL_.setLowpass(toneHz, fs_);
    toneR_.setLowpass(toneHz, fs_);
    hpL_.setHighpass(100.0f, fs_);
    hpR_.setHighpass(100.0f, fs_);
    // Hall decay: each line loses 60 dB per RT60 (x 1.2: the damping and
    // the low cut shorten the measured decay by about that much).
    const float rt60 = 1.2f * 0.8f * powf(10.0f, size_);
    for (int k = 0; k < 8; k++) gain_[k] = powf(10.0f, -3.0f * (float)kHallLen[k] / (rt60 * fs_));
    modDepth_ = 8.0f * mod_;
    // Gated: taps spread over the gate time with fixed jitter, gains tilted
    // by Size (decaying .. flat .. rising), the last 10 ms faded, and the
    // whole burst normalized to unity power.
    static const float kJitL[kTaps] = {0.31f, -0.22f, 0.12f, -0.35f, 0.27f, -0.08f, 0.38f, -0.29f,
                                       0.05f, -0.17f, 0.33f, -0.38f, 0.21f, -0.02f, 0.16f, -0.26f,
                                       0.36f, -0.11f, 0.09f, -0.33f, 0.24f, -0.19f, 0.02f, -0.06f,
                                       0.28f, -0.31f, 0.14f, -0.24f, 0.37f, -0.15f, 0.07f, -0.12f};
    static const float kJitR[kTaps] = {-0.27f, 0.18f, -0.36f, 0.07f, -0.13f, 0.34f, -0.24f, 0.11f,
                                       -0.39f, 0.29f, -0.04f, 0.15f, -0.31f, 0.37f, -0.18f, 0.26f,
                                       -0.09f, 0.03f, -0.28f, 0.19f, -0.37f, 0.08f, -0.14f, 0.01f,
                                       -0.21f, 0.32f, -0.05f, 0.23f, -0.34f, 0.13f, -0.26f, 0.04f};
    const float gate = gateMs_ * 0.001f * fs_;
    const float tilt = 2.0f * size_ - 1.0f;
    const float fade = 0.010f * fs_;
    float pl = 0.0f, pr = 0.0f;
    for (int k = 0; k < kTaps; k++) {
      const float fl = ((float)k + 0.5f + kJitL[k]) / (float)kTaps;
      const float fr = ((float)k + 0.5f + kJitR[k]) / (float)kTaps;
      tapL_[k] = 1 + (int)(fl * gate);
      tapR_[k] = 1 + (int)(fr * gate);
      gainL_[k] = shape(fl, tilt, gate, fade) * ((k & 1) ? -1.0f : 1.0f);
      gainR_[k] = shape(fr, tilt, gate, fade) * ((k & 2) ? -1.0f : 1.0f);
      pl += gainL_[k] * gainL_[k];
      pr += gainR_[k] * gainR_[k];
    }
    const float nl = 1.0f / sqrtf(pl), nr = 1.0f / sqrtf(pr);
    for (int k = 0; k < kTaps; k++) {
      gainL_[k] *= nl;
      gainR_[k] *= nr;
    }
  }
  static float shape(float f, float tilt, float gate, float fade) {
    float g = 1.0f + tilt * (2.0f * f - 1.0f);
    const float tail = (1.0f - f) * gate / fade;
    if (tail < 1.0f) g *= tail < 0.0f ? 0.0f : tail;
    return g < 0.02f ? 0.02f : g;
  }

  // Output levels: 100 % wet about as loud as the input (pink noise).
  static constexpr float kHallOut = 0.3f, kGateOut = 1.45f;
  static constexpr int kTapL[8] = {113, 1499, 761, 2203, 397, 2963, 1301, 3541};
  static constexpr int kTapR[8] = {1237, 211, 1913, 593, 2617, 331, 3203, 977};

  const float fs_;
  float* mem_ = nullptr;
  float* pre_ = nullptr;
  float* shared_ = nullptr;
  float* line_[8] = {};
  int lineLen_[8] = {};
  Allpass diff_[4];
  int clearPos_ = 0, preIdx_ = 0, preDelay_ = 1764, gateIdx_ = 0;
  int lineIdx_[8] = {};
  float damp_[8] = {};
  float gain_[8] = {};
  Lfo lfo_[2];
  Biquad inLp_, inLp2_, hpL_, hpR_, toneL_, toneR_;
  int tapL_[kTaps] = {}, tapR_[kTaps] = {};
  float gainL_[kTaps] = {}, gainR_[kTaps] = {};

  // Parameters and the values the coefficients were computed for.
  volatile int mode_ = kHall;
  volatile float size_ = 0.5f, tone_ = 0.5f, mod_ = 0.4f, era_ = 0.4f, gateMs_ = 300.0f;
  float cSize_ = -1.0f, cTone_ = -1.0f, cEra_ = -1.0f, cGate_ = -1.0f;
  float qScale_ = 32768.0f, qInv_ = 1.0f / 32768.0f, dampK_ = 0.5f, modDepth_ = 3.2f;
};

}  // namespace irdsp
