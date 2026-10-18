// FxSlot.h - on/off, dry/wet mix and idle detection for one time-based
// effect (delay, reverb) in a block-processed chain.
//
//   - On fades the effect's input in; off fades it out and lets the tail
//     ring, while the dry signal returns to unity.
//   - Dry and wet gains follow the mix setting (equal-power) with 20 ms ramps.
//   - Once off and the wet output has stayed below -100 dBFS for 0.5 s, the
//     slot goes idle: the caller clears the effect and skips it, so its
//     output is exactly its input and it costs no CPU.
//
// Threading model: setOn()/setMix() may be called from the background; the
// rest runs once per block in the audio interrupt.
#pragma once

#include "irdsp_blocks.h"

namespace irdsp {

template <int B>
class FxSlot {
 public:
  explicit FxSlot(float sampleRate)
      : step_((float)B / (sampleRate * 0.02f)), quietLimit_((int)(sampleRate * 0.5f / (float)B)) {}

  void setOn(bool on) { on_ = on; }
  bool on() const { return on_; }
  // 0 = dry only, 1 = wet only.
  void setMix(float m) {
    float wet, dry;
    mixPower(m, &wet, &dry);
    wetMix_ = wet;
    dryMix_ = dry;
  }
  // True while the effect is on or its tail is still ringing.
  bool running() const { return running_; }

  // Starts a block. Returns false while idle: skip the effect.
  bool begin(bool ready) {
    en_ = on_ && ready;
    if (!en_ && !running_) return false;
    if (!running_) {  // starting from idle: the effect is already clear
      running_ = true;
      quiet_ = 0;
    }
    in0_ = inCur_;
    in1_ = ramp(in0_, en_ ? 1.0f : 0.0f);
    d0_ = dryCur_;
    d1_ = ramp(d0_, en_ ? dryMix_ : 1.0f);
    e0_ = wetCur_;
    e1_ = ramp(e0_, wetMix_);
    wetPeak_ = 0.0f;
    return true;
  }

  // in x effect input gain (fades the effect input on and off).
  void feed(const float* in, float* out) const {
    for (int i = 0; i < B; i++) out[i] = in[i] * (in0_ + (in1_ - in0_) * (float)(i + 1) / (float)B);
  }

  // wet (in place) = dry gain x dry + wet gain x wet. Call once per channel.
  void mix(const float* dry, float* wet) {
    float pk = wetPeak_;
    for (int i = 0; i < B; i++) {
      const float a = (float)(i + 1) / (float)B;
      const float d = d0_ + (d1_ - d0_) * a, e = e0_ + (e1_ - e0_) * a;
      pk = fmaxf(pk, fabsf(wet[i]));
      wet[i] = d * dry[i] + e * wet[i];
    }
    wetPeak_ = pk;
  }

  // Ends the block. Returns true when the slot has just gone idle; the
  // caller then clears the effect's state.
  bool end() {
    inCur_ = in1_;
    dryCur_ = d1_;
    wetCur_ = e1_;
    if (en_ || wetPeak_ > 1e-5f) {
      quiet_ = 0;
    } else if (++quiet_ >= quietLimit_ && d1_ == 1.0f) {
      running_ = false;
      return true;
    }
    return false;
  }

  // Exponential average of the cycles the effect took per block (the
  // caller measures them).
  void addCycles(uint32_t c) { cyclesAvg_ = cyclesAvg_ + ((float)c - cyclesAvg_) * 0.01f; }
  float cyclesAvg() const { return cyclesAvg_; }

 private:
  // Moves cur one block-length step toward target (0 -> 1 takes 20 ms).
  float ramp(float cur, float target) const {
    return target > cur ? fminf(cur + step_, target) : fmaxf(cur - step_, target);
  }

  const float step_;
  const int quietLimit_;
  // Settings (written from the background, read once per block).
  volatile bool on_ = false;
  volatile float wetMix_ = 0.0f, dryMix_ = 1.0f;
  // State (audio interrupt only, except reads of running_).
  volatile bool running_ = false;
  bool en_ = false;
  float inCur_ = 0.0f, dryCur_ = 1.0f, wetCur_ = 0.0f;
  float in0_ = 0.0f, in1_ = 0.0f, d0_ = 1.0f, d1_ = 1.0f, e0_ = 0.0f, e1_ = 0.0f;
  float wetPeak_ = 0.0f;
  int quiet_ = 0;
  volatile float cyclesAvg_ = 0.0f;
};

}  // namespace irdsp
