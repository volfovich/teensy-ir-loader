// AmpStage.h - the amp block (NAM.md): an A2-Lite network (NamA2.h) with its
// input and output gains, switched on and off and given new models without
// clicks.
//
//   on        the dry signal stays while the network warms up (its receptive
//             field, 132 ms at 48 kHz, from a cleared state), then a 20 ms
//             crossfade to the amp
//   off       a 20 ms crossfade back to the dry signal; then the network
//             stops (no CPU) and its state is stale until the next on
//   new model while on: a 20 ms fade to silence, the weights swapped in, the
//             warm-up in silence, a 20 ms fade back in (a ~170 ms gap); while
//             off the weights are just swapped in
//
// Threading model: setOn(), setGains() and loadWeights() are called from the
// background; process() runs once per block in the audio interrupt. A
// loadWeights() buffer must stay untouched until weightsTaken().
#pragma once

#include <math.h>
#include <stdint.h>

#include "NamA2.h"

namespace irdsp {

template <int B>
class AmpStage {
 public:
  using Net = NamA2<B>;
  static constexpr int kHistoryFloats = Net::kHistoryFloats;
  // Blocks of warm-up: the network's receptive field (6,346 samples).
  static constexpr int kWarmBlocks = (6331 + nama2::kHeadTaps - 1 + B - 1) / B;

  explicit AmpStage(float sampleRate) : step_((float)B / (sampleRate * 0.02f)) {}

  // Hands over the network's history memory (kHistoryFloats).
  void begin(float* history) { net_.begin(history); }

  void setOn(bool on) { on_ = on; }
  // Linear gains into and out of the network.
  void setGains(float in, float out) {
    inTarget_ = in;
    outTarget_ = out;
  }
  // New weights (nama2::kWeightCount floats in .nam order), swapped in once
  // the amp is silent.
  void loadWeights(const float* w) { pending_ = w; }
  bool weightsTaken() const { return pending_ == nullptr; }
  bool hasModel() const { return hasModel_; }
  // True while the network runs (on, warming up, or fading out).
  bool active() const { return running_; }

  // One block. Returns false while the amp is idle: out is not written and
  // the signal is the input, unchanged.
  bool process(const float* in, float* out) {
    // 1. New weights, once the amp is silent. With the amp on, the new model
    //    then warms up in silence.
    if (pending_ && wetCur_ == 0.0f) {
      net_.setWeights(pending_);
      pending_ = nullptr;
      hasModel_ = true;
      running_ = false;
      swapping_ = on_;
    }
    // 2. Starting the network: from a cleared state, with a warm-up.
    const bool want = on_ && hasModel_;
    if (want && !running_ && !pending_) {
      net_.reset();
      running_ = true;
      warm_ = kWarmBlocks;
    }
    // 3. Where the dry and amp levels go.
    float dryT = 1.0f, wetT = 0.0f;
    if (pending_) dryT = want ? 0.0f : 1.0f;  // fading out for a new model
    else if (want && warm_ > 0) dryT = swapping_ ? 0.0f : 1.0f;
    else if (want) dryT = 0.0f, wetT = 1.0f;
    const float d0 = dryCur_, d1 = ramp(d0, dryT), e0 = wetCur_, e1 = ramp(e0, wetT);
    dryCur_ = d1;
    wetCur_ = e1;
    if (!running_ && d0 == 1.0f && d1 == 1.0f) return false;  // idle

    // 4. The network, and the mix.
    float y[B];
    if (running_) {
      const float g0 = inCur_, g1 = inTarget_;
      for (int i = 0; i < B; i++) y[i] = in[i] * (g0 + (g1 - g0) * (float)(i + 1) / (float)B);
      inCur_ = g1;
      net_.process(y, y);
      if (warm_ > 0 && --warm_ == 0) swapping_ = false;
    } else {
      for (int i = 0; i < B; i++) y[i] = 0.0f;
    }
    const float o0 = outCur_, o1 = outTarget_;
    outCur_ = o1;
    for (int i = 0; i < B; i++) {
      const float a = (float)(i + 1) / (float)B;
      const float d = d0 + (d1 - d0) * a, e = (e0 + (e1 - e0) * a) * (o0 + (o1 - o0) * a);
      out[i] = d * in[i] + e * y[i];
    }
    // 5. Off (or a new model coming) and faded out: the network stops.
    if (running_ && (!want || pending_) && e1 == 0.0f) {
      running_ = false;
      warm_ = 0;
      swapping_ = false;
    }
    return true;
  }

  // Exponential average of the cycles the stage took per block (the caller
  // measures them).
  void addCycles(uint32_t c) { cyclesAvg_ = cyclesAvg_ + ((float)c - cyclesAvg_) * 0.01f; }
  float cyclesAvg() const { return cyclesAvg_; }

 private:
  // Moves cur one block-length step toward target (0 -> 1 takes 20 ms).
  float ramp(float cur, float target) const {
    return target > cur ? fminf(cur + step_, target) : fmaxf(cur - step_, target);
  }

  Net net_;
  const float step_;
  // Settings (written from the background, read once per block).
  volatile bool on_ = false;
  volatile float inTarget_ = 1.0f, outTarget_ = 1.0f;
  const float* volatile pending_ = nullptr;
  // State (audio interrupt only, except reads of the flags).
  volatile bool hasModel_ = false, running_ = false;
  bool swapping_ = false;
  int warm_ = 0;
  float dryCur_ = 1.0f, wetCur_ = 0.0f, inCur_ = 1.0f, outCur_ = 1.0f;
  float cyclesAvg_ = 0.0f;
};

}  // namespace irdsp
