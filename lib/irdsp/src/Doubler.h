// Doubler.h - imitates a double-tracked guitar: a second take panned
// opposite the first (mono in, stereo out).
//
//   in -> original                         -> pan -width  -> L / R
//   in -> line (Time, Hermite read) = double -> pan +width  -> L / R
//
//   - The original stays untouched, so the attack is as tight as before;
//     the double comes Time later (1 - 35 ms). A change of Time crossfades
//     (20 ms) from the old delay to the new, so it neither clicks nor bends
//     the pitch.
//   - Width pans the two equal-power from the centre (0) to hard left and
//     right (1); hard-panned, each channel keeps the input's level.
//   - On and off ramp (20 ms) between that and the input on both channels.
//     Off and settled, process() returns false and costs no CPU; the next
//     on starts from a cleared line.
//
// Threading model: set() and setOn() run in the background, process() in
// the audio interrupt. set() fills the spare set of values, then switches
// to it, so a block never sees half of one.
#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "irdsp_blocks.h"
#include "irdsp_port.h"

namespace irdsp {

class Doubler {
 public:
  static constexpr int kLen = 2048;  // the line, floats (42.7 ms at 48 kHz)
  static constexpr float kMaxTimeMs = 35.0f;

  explicit Doubler(float sampleRate)
      : fs_(sampleRate),
        step_(1.0f / (sampleRate * 0.02f)) {
    set(10.0f, 1.0f);
  }

  // Hands over the line (kLen floats). Until then the doubler stays off.
  void init(float* line) {
    memset(line, 0, sizeof(float) * kLen);
    line_ = line;
  }
  bool ready() const { return line_ != nullptr; }

  void setOn(bool on) { on_ = on; }
  // Time in ms (1 - 35), width 0..1.
  IRDSP_COLD void set(float timeMs, float width) {
    Set& s = sets_[1 - cur_];
    timeMs = timeMs < 1.0f ? 1.0f : (timeMs > kMaxTimeMs ? kMaxTimeMs : timeMs);
    s.time = timeMs * fs_ / 1000.0f;
    // Pan positions -w (the original) and +w (the double): angle (1 + p) pi/4.
    const float w = clamp01(width), q = 0.78539816f;
    s.g[0] = cosf((1.0f - w) * q);  // original -> L
    s.g[1] = sinf((1.0f - w) * q);  // original -> R
    s.g[2] = cosf((1.0f + w) * q);  // double -> L
    s.g[3] = sinf((1.0f + w) * q);  // double -> R
    for (float& g : s.g) g = g < 1e-6f ? 0.0f : g;  // hard-panned: exactly 0
    cur_ = 1 - cur_;
  }

  // One block of n samples. Returns false while idle: outL / outR are not
  // written and the signal is the input, on both sides.
  bool process(const float* in, float* outL, float* outR, int n) {
    if (!line_ || (!on_ && mix_ == 0.0f)) return false;
    const Set& s = sets_[cur_];
    if (mix_ == 0.0f) {  // starting from idle
      memset(line_, 0, sizeof(float) * kLen);
      tOld_ = tNew_ = s.time;
      fade_ = 1.0f;
    }
    // A new time: crossfade to it from the old (one change at a time).
    if (fade_ >= 1.0f && s.time != tNew_) {
      tOld_ = tNew_;
      tNew_ = s.time;
      fade_ = 0.0f;
    }
    const float mixT = on_ ? 1.0f : 0.0f;
    float m = mix_, f = fade_;
    const float dOld = tOld_, dNew = tNew_;
    int idx = idx_;
    for (int i = 0; i < n; i++) {
      const float x = in[i];
      float dbl = readHermite(line_, kLen, idx, dNew);
      if (f < 1.0f) {
        f = fminf(f + step_, 1.0f);
        dbl = f * dbl + (1.0f - f) * readHermite(line_, kLen, idx, dOld);
      }
      line_[idx] = x;
      idx = (idx + 1) & (kLen - 1);
      m = mixT > m ? fminf(m + step_, mixT) : fmaxf(m - step_, mixT);
      // Between the input on both sides (m = 0) and the doubled pair (m = 1).
      const float xl = 1.0f + m * (s.g[0] - 1.0f), xr = 1.0f + m * (s.g[1] - 1.0f);
      outL[i] = xl * x + m * s.g[2] * dbl;
      outR[i] = xr * x + m * s.g[3] * dbl;
    }
    idx_ = idx;
    fade_ = f;
    mix_ = m;
    return true;
  }

  // For tests: the delay being faded to, samples, and how far (1 = there).
  float delaySamples() const { return tNew_; }
  float fade() const { return fade_; }

 private:
  struct Set {
    float time = 0.0f;  // samples
    float g[4] = {1.0f, 1.0f, 0.0f, 0.0f};
  };
  const float fs_, step_;
  Set sets_[2];
  volatile int cur_ = 0;
  volatile bool on_ = false;
  float* line_ = nullptr;
  // State (audio interrupt only).
  int idx_ = 0;
  float mix_ = 0.0f, tOld_ = 0.0f, tNew_ = 0.0f, fade_ = 1.0f;
};

}  // namespace irdsp
