// StereoDelay.h - 32-bit float modulated ping-pong delay (mono in, stereo out).
//
// A port of AudioEffectDelayStereo_F32 from hexefx_audiolib_F32
// (https://github.com/hexeguitar/hexefx_audiolib_F32):
//
//   in --> (+) --> tone --> line A --+--> line B --+--> out R
//           ^                        |             |
//           |                        +--> out L    |
//           +---- feedback x damping filter <------+
//
// The first repeat comes out on the left after T, the next on the right after
// 2T, and so on. Reads are 4-point Hermite interpolated, the delay time glides
// (tape-like pitch bend) instead of jumping, and a slow LFO can modulate both
// lines for a chorused tail.
//
// The original runs two such pairs, fed by the left and right inputs. With a
// mono input both pairs carry the same signal (only their LFO phases differ),
// so one pair with twice the output gain is used: identical with modulation
// off, at half the memory. Filter corners, parameter mappings, the time
// smoothing and the feedback-dependent input attenuation are unchanged. The
// original's freeze, tap tempo and PSRAM support are left out.
//
// Original code: Copyright (c) 2024 by Piotr Zapart, www.hexefx.com, MIT
// license. Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including without
// limitation the rights to use, copy, modify, merge, publish, distribute,
// sublicense, and/or sell copies of the Software, and to permit persons to whom
// the Software is furnished to do so, subject to the following conditions: The
// above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
// LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Threading model: process() and clearStep() run in the audio interrupt. The
// setters only write single values, so they may be called from either side.
#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "irdsp_blocks.h"

namespace irdsp {

class StereoDelay {
 public:
  static constexpr int kMinDelay = 128;  // samples (the original's dly_time_min)

  explicit StereoDelay(float sampleRate = 44100.0f) : fs_(sampleRate) {
    setFeedback(0.5f);  // as in the original's begin()
  }

  // Hands over the two line buffers (len floats each) and clears them.
  // The longest delay is len - 4 samples.
  void init(float* lineA, float* lineB, int len) {
    a_ = lineA;
    b_ = lineB;
    len_ = len;
    reset();
    dSet_ = dCur_ = dFlt_ = (float)maxDelay();
  }
  bool ready() const { return a_ != nullptr; }
  int maxDelay() const { return len_ - 4; }

  // Silences both lines and the filters at once.
  void reset() {
    clearPos_ = 0;
    clearStep(2 * len_);
  }

  // Clears up to n samples of the lines per call (spreads a full clear over
  // several audio blocks); returns true when both lines are silent.
  bool clearStep(int n) {
    if (clearPos_ == 0) {
      fltFb_.clear();
      fltIn_.clear();
    }
    while (n > 0 && clearPos_ < 2 * len_) {
      float* buf = clearPos_ < len_ ? a_ : b_;
      const int start = clearPos_ % len_;
      int count = len_ - start;
      if (count > n) count = n;
      memset(buf + start, 0, count * sizeof(float));
      clearPos_ += count;
      n -= count;
    }
    if (clearPos_ < 2 * len_) return false;
    clearPos_ = 0;
    return true;
  }

  // ---- Parameters (same mappings as the HexeFX setters) -------------------

  // Delay time in samples, clamped to kMinDelay .. maxDelay(). With force
  // the time jumps instead of gliding.
  void setDelaySamples(float d, bool force = false) {
    const float hi = (float)maxDelay();
    d = d < (float)kMinDelay ? (float)kMinDelay : (d > hi ? hi : d);
    if (force) dCur_ = dFlt_ = d;
    dSet_ = d;
  }
  // Repeats, 0..1. More feedback also lowers the input level a little so the
  // repeats do not build up.
  void setFeedback(float n) {
    n = clamp01(n);
    feedback_ = n * kFeedbackMax;
    inGainSet_ = 1.0f - 0.6f * n * n * n;
  }
  // How fast a new delay time is reached, 0..1 (0 = fastest).
  void setInertia(float n) {
    n = clamp01(n);
    n = 2.0f * n - n * n;
    timeStep_ = 10.0f + n * (0.3f - 10.0f);
  }
  // Treble loss per repeat, 0..1 (0 = none).
  void setTrebleCut(float n) { trebleCut_ = 1.0f - clamp01(n); }
  // Bass loss per repeat, 0..1 (0 = none).
  void setBassCut(float n) {
    n = clamp01(n);
    bassCut_ = -(2.0f * n - n * n);
  }
  // Treble / bass of the signal entering the lines, 0..1 (1 = flat).
  void setTreble(float n) { treble_ = clamp01(n); }
  void setBass(float n) {
    n = clamp01(n);
    bass_ = -(1.0f - 2.0f * n + n * n);
  }
  // Modulation rate (0..1 -> 0..16 Hz, cubic) and depth (0..1 -> 0..127 samples).
  void setModRate(float r) {
    r = clamp01(r);
    lfo_.setRate(r * r * r * kLfoMaxHz, fs_);
  }
  void setModDepth(float d) { modDepth_ = clamp01(d) * kLfoMaxDepth; }

  // ---- Audio --------------------------------------------------------------

  // Processes n samples of mono input into the wet-only stereo outputs.
  void process(const float* in, float* outL, float* outR, int n) {
    const float fb = feedback_, step = timeStep_;
    const float tCut = trebleCut_, bCut = bassCut_, tre = treble_, bas = bass_;
    const float depth = modDepth_, inSet = inGainSet_;
    float inGain = inGain_, dCur = dCur_, dFlt = dFlt_;
    const float dSet = dSet_;
    for (int i = 0; i < n; i++) {
      inGain += (inSet - inGain) * 0.25f;

      // Glide toward the set time: rate-limited, then a one-pole lowpass.
      if (dCur < dSet) { dCur += step; if (dCur > dSet) dCur = dSet; }
      else if (dCur > dSet) { dCur -= step; if (dCur < dSet) dCur = dSet; }
      dFlt += (dCur - dFlt) * 0.1f;
      dCur = dFlt;

      lfo_.tick();
      const float dB = dCur - lfo_.value(0) * depth;
      const float dA = dCur - lfo_.value(43) * depth;  // +60 degrees

      const float b = readHermite(b_, len_, idx_, dB);
      float acc = fltFb_.process(b, tCut, bCut, kBassLossF, kTrebleLossF) * fb;
      acc = fltIn_.process(acc + in[i] * inGain, tre, bas, kBassLossF, kTrebleLossF);
      const float a = readHermite(a_, len_, idx_, dA);
      b_[idx_] = a;
      a_[idx_] = acc;
      if (++idx_ >= len_) idx_ = 0;

      outL[i] = a * kOutGain;
      outR[i] = b * kOutGain;
    }
    inGain_ = inGain;
    dCur_ = dCur;
    dFlt_ = dFlt;
  }

 private:
  static constexpr float kFeedbackMax = 0.96f;
  static constexpr float kOutGain = 1.2f;  // 2 pairs x 0.6 in the original
  static constexpr float kTrebleLossF = 0.20f;
  static constexpr float kBassLossF = 0.05f;
  static constexpr float kLfoMaxHz = 16.0f;
  static constexpr float kLfoMaxDepth = 127.0f;

  const float fs_;
  float* a_ = nullptr;
  float* b_ = nullptr;
  int len_ = 0;
  int idx_ = 0;
  int clearPos_ = 0;

  Shelf fltFb_, fltIn_;
  Lfo lfo_;

  // Parameters (defaults as in the original).
  float feedback_ = 0.0f, inGainSet_ = 1.0f, inGain_ = 1.0f;
  float timeStep_ = 10.0f;
  float trebleCut_ = 1.0f, bassCut_ = 0.0f, treble_ = 1.0f, bass_ = 0.0f;
  float modDepth_ = 0.0f;
  float dSet_ = 0.0f, dCur_ = 0.0f, dFlt_ = 0.0f;  // delay time, samples
};

}  // namespace irdsp
