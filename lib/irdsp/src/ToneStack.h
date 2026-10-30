// ToneStack.h - the amp's tone controls, after the network (NAM.md): Bass,
// Middle, Treble and Presence, each a gain in dB, flat at 0. An EQ,
// as in the NAM plugin, not a model of a passive tone stack: the capture
// already has the amp's own.
//
//   Bass      low shelf at 120 Hz            (the settings: +-12 dB)
//   Middle    peak at 650 Hz, Q 0.7          (+-10 dB)
//   Treble    high shelf at 2.5 kHz          (+-12 dB)
//   Presence  peak at 5 kHz, Q 0.7           (+-10 dB)
//
// A band at 0 dB is skipped (no CPU, the signal untouched). set() runs in the
// background and process() in the audio interrupt: set() fills the spare
// set of coefficients, then switches to it, so a block never sees half of
// one.
#pragma once

#include "irdsp_blocks.h"
#include "irdsp_port.h"

namespace irdsp {

class ToneStack {
 public:
  static constexpr int kBands = 4;  // bass, middle, treble, presence

  explicit ToneStack(float sampleRate) : fs_(sampleRate) {}

  // The gains in dB (0 = flat).
  IRDSP_COLD void set(float bass, float middle, float treble, float presence) {
    Set& s = sets_[1 - cur_];
    const float db[kBands] = {bass, middle, treble, presence};
    for (int k = 0; k < kBands; k++) s.on[k] = fabsf(db[k]) > 0.01f;
    s.f[0].setLowShelf(120.0f, fs_, bass);
    s.f[1].setPeak(650.0f, fs_, middle, 0.7f);
    s.f[2].setHighShelf(2500.0f, fs_, treble);
    s.f[3].setPeak(5000.0f, fs_, presence, 0.7f);
    cur_ = 1 - cur_;
  }

  void reset() {
    for (auto& z : z_) z[0] = z[1] = 0.0f;
  }

  // In place.
  void process(float* x, int n) {
    const Set& s = sets_[cur_];
    for (int k = 0; k < kBands; k++) {
      float z1 = z_[k][0], z2 = z_[k][1];
      if (s.on[k]) {
        const Biquad& f = s.f[k];
        for (int i = 0; i < n; i++) {
          const float y = f.b0 * x[i] + z1;
          z1 = f.b1 * x[i] - f.a1 * y + z2;
          z2 = f.b2 * x[i] - f.a2 * y;
          x[i] = y;
        }
      } else {
        z1 = z2 = 0.0f;  // starts clean when turned again
      }
      z_[k][0] = z1;
      z_[k][1] = z2;
    }
  }

 private:
  struct Set {
    Biquad f[kBands];
    bool on[kBands] = {};
  };
  const float fs_;
  Set sets_[2];
  volatile int cur_ = 0;
  float z_[kBands][2] = {};
};

}  // namespace irdsp
