// FxStage.h - one time-based effect (StudioDelay, Reverb80) in the chain,
// with its FxSlot: on/off, dry/wet mix, and the clearing once it has gone
// idle.
//
//   - process() returns false while the effect is idle; the caller then
//     uses the input unchanged, and the effect costs no CPU.
//   - An effect that has just gone idle is cleared a few KB per block
//     (clearing it all at once would stall the audio interrupt), or all at
//     once if it is switched on again meanwhile.
//
// Fx needs ready(), clearStep(n) and process(); kStereoIn picks
// process(inL, inR, outL, outR, n) over the mono process(in, outL, outR, n).
//
// Threading model: as FxSlot. setOn()/setMix() and fx()'s setters may be
// called from the background; process() runs in the audio interrupt.
#pragma once

#include <stdint.h>

#include "FxSlot.h"

namespace irdsp {

template <class Fx, int B, bool kStereoIn>
class FxStage {
 public:
  explicit FxStage(float sampleRate) : fx_(sampleRate), slot_(sampleRate) {}

  Fx& fx() { return fx_; }
  void setOn(bool on) { slot_.setOn(on); }
  void setMix(float m) { slot_.setMix(m); }  // 0 = dry only, 1 = wet only
  // True while the effect is on or its tail is still ringing.
  bool running() const { return slot_.running(); }
  float cyclesAvg() const { return slot_.cyclesAvg(); }
  void addCycles(uint32_t c) { slot_.addCycles(c); }

  // One block: in L/R (the same buffer for a mono input) -> out L/R, dry
  // and wet mixed. Returns false while idle, writing nothing.
  bool process(const float* inL, const float* inR, float* outL, float* outR) {
    if (clearing_) {
      if (!slot_.on()) {
        if (fx_.clearStep(kClearPerBlock)) clearing_ = false;
        return false;
      }
      fx_.clearStep(1 << 30);
      clearing_ = false;
    }
    if (!slot_.begin(fx_.ready())) return false;
    float l[B];
    slot_.feed(inL, l);
    if constexpr (kStereoIn) {
      float r[B];
      slot_.feed(inR, r);
      fx_.process(l, r, outL, outR, B);
    } else {
      fx_.process(l, outL, outR, B);
    }
    slot_.mix(inL, outL);
    slot_.mix(inR, outR);
    if (slot_.end()) clearing_ = true;
    return true;
  }

 private:
  static constexpr int kClearPerBlock = 4096;  // floats

  Fx fx_;
  FxSlot<B> slot_;
  bool clearing_ = false;  // audio interrupt only
};

}  // namespace irdsp
