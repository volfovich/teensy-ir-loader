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
//     gated: -> one line, 24 taps per side spread over the gate time, tap
//               gains shaped by Size (decaying / flat / rising), Tone lowpass
//   -> low cut (100 Hz) -> wet L / R
//
// Era is the converters of the period, as in StudioDelay: the wet input's
// bandwidth falls from 16 to 6 kHz and its resolution from 16 to 10 bits.
//
// Line lengths are in samples, tuned for 44.1 kHz. The modes share one block
// of memory, so a mode change while the reverb sounds fades the tail out
// (20 ms), clears the lines a few KB per block, then starts the new mode.
//
// Threading model: process() runs in the audio interrupt. The setters only
// write single values; process() recomputes the coefficients when they
// change (that code, and the clearing, run from flash to save RAM1).
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
  static constexpr int kTaps = 24;      // per side (gated)
  static constexpr int kHallFloats = 2003 + 2341 + 2729 + 3011 + 3313 + 3637 + 3943 + 4271 + 8 * kModPad;
  static constexpr int kSharedFloats = kHallFloats > kGateMax ? kHallFloats : kGateMax;
  // Total buffer size for init(), in floats (about 136 KB).
  static constexpr int kMemFloats = kPreMax + (142 + 107 + 379 + 277) + kSharedFloats;

  explicit Reverb80(float sampleRate = 44100.0f) : fs_(sampleRate) {
    lfo_[0].setRate(0.53f, sampleRate);
    lfo_[1].setRate(0.89f, sampleRate);
  }

  // Hands over the line memory (kMemFloats floats) and clears it.
  void init(float* mem);
  bool ready() const { return mem_ != nullptr; }

  // Silences everything at once.
  void reset() {
    clearPos_ = 0;
    clearStep(kMemFloats);
  }

  // Clears up to n floats per call; returns true when the reverb is silent.
  bool clearStep(int n);

  // ---- Parameters -----------------------------------------------------------

  // Hall or gated. With sound in the lines the change fades out, clears and
  // switches (see above); with the lines already clear it applies at once.
  void setMode(int m) { modeReq_ = m == kGated ? kGated : kHall; }
  int mode() const { return modeReq_; }
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
    if (modeReq_ != mode_ && !fading_ && !switchClearing_) {
      if (clean_) mode_ = modeReq_;
      else fading_ = true;
    }
    if (switchClearing_) {
      for (int i = 0; i < n; i++) outL[i] = outR[i] = 0.0f;
      if (clearStep(kSwitchClear)) {
        switchClearing_ = false;
        mode_ = modeReq_;
        fadeGain_ = 1.0f;
      }
      return;
    }
    clean_ = false;
    if (size_ != cSize_ || tone_ != cTone_ || era_ != cEra_ || gateMs_ != cGate_) update();
    for (int off = 0; off < n; off += kChunk) {
      const int m = n - off < kChunk ? n - off : kChunk;
      float x[kChunk], l[kChunk], r[kChunk];
      front(inL + off, inR + off, x, m);
      if (mode_ == kGated) gatedBlock(x, l, r, m);
      else hallBlock(x, l, r, m);
      for (int i = 0; i < m; i++) {
        outL[off + i] = hpL_.process(l[i]);
        outR[off + i] = hpR_.process(r[i]);
      }
    }
    if (fading_) fadeOut(outL, outR, n);
  }

 private:
  static constexpr int kSwitchClear = 4096;  // floats cleared per block in a mode switch

  // The mode switch's 20 ms fade; at silence the clearing starts.
  void fadeOut(float* outL, float* outR, int n) {
    const float step = 1.0f / (0.02f * fs_);
    float g = fadeGain_;
    for (int i = 0; i < n; i++) {
      g = g > step ? g - step : 0.0f;
      outL[i] *= g;
      outR[i] *= g;
    }
    fadeGain_ = g;
    if (g == 0.0f) {
      fading_ = false;
      switchClearing_ = true;
    }
  }

  // Work is done in chunks, one line at a time: the lines live in the
  // Teensy's cached OCRAM, and streaming through each line is much faster
  // than touching all of them every sample. Every line delay and output tap
  // is longer than a chunk, so the result is the same as sample by sample.
  static constexpr int kChunk = 32;

  // Calls f(i, idx) for i = 0 .. m-1 with idx = start + i wrapped at len
  // (m <= len): two straight loops instead of a wrap check per sample.
  template <class F>
  static inline void span(int len, int start, int m, F f) {
    const int first = len - start < m ? len - start : m;
    for (int i = 0; i < first; i++) f(i, start + i);
    for (int i = first; i < m; i++) f(i, i - first);
  }
  static inline int back(int idx, int d, int len) { return idx - d < 0 ? idx - d + len : idx - d; }

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

  // Pre-delay, the era's converters, then the input diffusion (short in the
  // gated mode, so the burst can stop dead).
  inline void front(const float* inL, const float* inR, float* x, int m) {
    const float qs = qScale_, qi = qInv_;
    const bool gated = mode_ == kGated;
    for (int i = 0; i < m; i++) {
      pre_[preIdx_] = 0.5f * (inL[i] + inR[i]);
      int r = preIdx_ - preDelay_;
      if (r < 0) r += kPreMax;
      if (++preIdx_ >= kPreMax) preIdx_ = 0;
      float v = quantize(inLp2_.process(inLp_.process(pre_[r])), qs, qi);
      if (gated) {
        v = diff_[0].process(v, 0.7f);
        v = diff_[1].process(v, 0.7f);
      } else {
        v = diff_[0].process(v, 0.75f);
        v = diff_[1].process(v, 0.75f);
        v = diff_[2].process(v, 0.625f);
        v = diff_[3].process(v, 0.625f);
      }
      x[i] = v;
    }
  }

  // The hall: read the 8 lines (4 of them chorused), damp and decay them,
  // take the output taps, mix with an 8-point Hadamard matrix, add the input.
  inline void hallBlock(const float* x, float* outL, float* outR, int m) {
    float y[8][kChunk];
    // Chorus: the LFOs (0..2) at the chunk's start and end, interpolated.
    float m0[4], m1[4];
    m0[0] = lfo_[0].value(0), m0[1] = lfo_[0].value(64), m0[2] = lfo_[1].value(0), m0[3] = lfo_[1].value(64);
    lfo_[0].acc += lfo_[0].adder * (uint32_t)m;
    lfo_[1].acc += lfo_[1].adder * (uint32_t)m;
    m1[0] = lfo_[0].value(0), m1[1] = lfo_[0].value(64), m1[2] = lfo_[1].value(0), m1[3] = lfo_[1].value(64);
    const float depth = modDepth_, c = dampK_, step = 1.0f / (float)m;
    for (int k = 0; k < 8; k++) {
      const float* bf = line_[k];
      const int len = lineLen_[k], w0 = lineIdx_[k];
      const float g = gain_[k];
      float d = damp_[k];
      float* yk = y[k];
      if (k & 1) {
        span(len, back(w0, kHallLen[k], len), m, [&](int i, int r) {
          d += (bf[r] - d) * c;
          yk[i] = d * g;
        });
      } else {
        // Lines 0, 2, 4, 6: chorused (linear interpolation, as the units of
        // the day), delay kHallLen - depth * lfo.
        const float a0 = (float)kHallLen[k] - depth * m0[k >> 1];
        const float da = -depth * (m1[k >> 1] - m0[k >> 1]) * step;
        for (int i = 0; i < m; i++) {
          float pos = (float)(w0 + i) - (a0 + da * (float)(i + 1));
          if (pos < 0.0f) pos += (float)len;
          const int i0 = (int)pos;
          const float f = pos - (float)i0;
          const int j0 = i0 >= len ? i0 - len : i0;
          const int j1 = j0 + 1 >= len ? 0 : j0 + 1;
          d += (bf[j0] + f * (bf[j1] - bf[j0]) - d) * c;
          yk[i] = d * g;
        }
      }
      damp_[k] = d;
    }
    // Stereo out: taps along the lines (different points and signs for L
    // and R), so the reverb starts right after the pre-delay and the sides
    // are decorrelated.
    // Each side taps four of the lines (the others go to the other side).
    for (int i = 0; i < m; i++) outL[i] = outR[i] = 0.0f;
    for (int k = 0; k < 8; k++) {
      const float* bf = line_[k];
      const int len = lineLen_[k];
      const float sg = (k & 2) ? -kHallOut : kHallOut;
      float* out = kTapLeft[k] ? outL : outR;
      span(len, back(lineIdx_[k], kTap[k], len), m, [&](int i, int a) { out[i] += sg * bf[a]; });
    }
    // Fast Walsh-Hadamard transform (orthogonal with the 1/sqrt(8)), then
    // write back with the input added (alternating signs).
    for (int i = 0; i < m; i++) {
      float v[8];
      for (int k = 0; k < 8; k++) v[k] = y[k][i];
      for (int h = 1; h < 8; h <<= 1)
        for (int a = 0; a < 8; a += h << 1)
          for (int b = a; b < a + h; b++) {
            const float u = v[b], t = v[b + h];
            v[b] = u + t;
            v[b + h] = u - t;
          }
      for (int k = 0; k < 8; k++) y[k][i] = v[k] * 0.35355339f + ((k & 1) ? -x[i] : x[i]);
    }
    for (int k = 0; k < 8; k++) {
      float* bf = line_[k];
      const float* yk = y[k];
      const int len = lineLen_[k];
      span(len, lineIdx_[k], m, [&](int i, int w) { bf[w] = yk[i]; });
      lineIdx_[k] = lineIdx_[k] + m >= len ? lineIdx_[k] + m - len : lineIdx_[k] + m;
    }
  }

  // The gated burst: write the diffused input, then sum the taps.
  inline void gatedBlock(const float* x, float* outL, float* outR, int m) {
    float* bf = shared_;
    const int w0 = gateIdx_;
    span(kGateMax, w0, m, [&](int i, int w) { bf[w] = x[i]; });
    gateIdx_ = w0 + m >= kGateMax ? w0 + m - kGateMax : w0 + m;
    for (int i = 0; i < m; i++) outL[i] = outR[i] = 0.0f;
    for (int k = 0; k < kTaps; k++) {
      const float gl = gainL_[k], gr = gainR_[k];
      span(kGateMax, back(w0, tapL_[k], kGateMax), m, [&](int i, int a) { outL[i] += gl * bf[a]; });
      span(kGateMax, back(w0, tapR_[k], kGateMax), m, [&](int i, int b) { outR[i] += gr * bf[b]; });
    }
    for (int i = 0; i < m; i++) {
      outL[i] = kGateOut * toneL_.process(outL[i]);
      outR[i] = kGateOut * toneR_.process(outR[i]);
    }
  }

  // Recomputes the coefficients after a parameter change (rare: from flash).
  void update();
  static float shape(float f, float tilt, float gate, float fade);

  // Output levels: 100 % wet about as loud as the input (pink noise).
  static constexpr float kHallOut = 0.3f * 1.41421356f, kGateOut = 1.45f;
  // Output taps: one per line, at these points; lines 0, 3, 5, 6 feed the
  // left side and 1, 2, 4, 7 the right.
  static constexpr int kTap[8] = {113, 211, 1913, 2203, 397, 331, 1301, 977};
  static constexpr bool kTapLeft[8] = {true, false, false, true, false, true, true, false};

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

  // Mode switching: the requested mode, the one running, and the switch's
  // state (audio interrupt only). clean_: the lines are known silent.
  volatile int modeReq_ = kHall;
  int mode_ = kHall;
  bool clean_ = false, fading_ = false, switchClearing_ = false;
  float fadeGain_ = 1.0f;
  // Parameters and the values the coefficients were computed for.
  volatile float size_ = 0.5f, tone_ = 0.5f, mod_ = 0.4f, era_ = 0.4f, gateMs_ = 300.0f;
  float cSize_ = -1.0f, cTone_ = -1.0f, cEra_ = -1.0f, cGate_ = -1.0f;
  float qScale_ = 32768.0f, qInv_ = 1.0f / 32768.0f, dampK_ = 0.5f, modDepth_ = 3.2f;
};

}  // namespace irdsp
