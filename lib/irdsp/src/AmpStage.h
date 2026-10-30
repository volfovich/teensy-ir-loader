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
//   tone      Bass, Middle, Treble and Presence after the network
//             (ToneStack.h), flat at 0 dB
//   gate      a noise gate, as in the NAM plugin: it listens to the input
//             (before the input gain, so its threshold reads like the level
//             meter) and mutes the amp's output, so the input's noise
//             floor, amplified 50 dB or more by a high-gain model, isn't
//             heard between notes. Opens within 1 ms above the threshold;
//             closes when the input stays 6 dB below it for an unbroken
//             20 ms, then fades out exponentially, reaching -60 dB after the
//             release time (setGateRelease, 50 ms by default: a palm mute is
//             gone in about 140 ms). Its level is the blocks' power, so noise
//             spikes don't make it flicker.
//
// Threading model: setOn(), setGains() and loadWeights() are called from the
// background; process() runs once per block in the audio interrupt. A
// loadWeights() buffer must stay untouched until weightsTaken(). The offline
// calls (beginOffline() .. endOffline()) are background-only too.
#pragma once

#include <math.h>
#include <stdint.h>

#include "NamA2.h"
#include "ToneStack.h"

namespace irdsp {

template <int B>
class AmpStage {
 public:
  using Net = NamA2<B>;
  static constexpr int kHistoryFloats = Net::kHistoryFloats;
  // Blocks of warm-up: the network's receptive field (6,346 samples).
  static constexpr int kWarmBlocks = (6331 + nama2::kHeadTaps - 1 + B - 1) / B;

  explicit AmpStage(float sampleRate)
      : tone_(sampleRate),
        fs_(sampleRate),
        step_((float)B / (sampleRate * 0.02f)),
        gateOpenStep_((float)B / (sampleRate * 0.001f)),
        envFall_(expf(-(float)B / (sampleRate * 0.005f))),
        gateHold_((int)(sampleRate * 0.02f / (float)B)) {
    setGateRelease(50.0f);
  }

  // Hands over the network's history memory (kHistoryFloats).
  void begin(float* history) { net_.begin(history); }

  void setOn(bool on) { on_ = on; }
  // Linear gains into and out of the network.
  void setGains(float in, float out) {
    inTarget_ = in;
    outTarget_ = out;
  }
  // The tone controls' gains in dB (0 = flat).
  void setTone(float bass, float middle, float treble, float presence) {
    tone_.set(bass, middle, treble, presence);
  }
  // The noise gate's threshold, linear on the input's scale (full scale 1);
  // 0 = no gate.
  void setGate(float threshold) { gateThr_ = threshold; }
  // The gate's fade from open to -60 dB, in ms.
  void setGateRelease(float ms) { gateFall_ = expf(-6.9078f * (float)B / (fs_ * ms * 0.001f)); }
  // New weights (nama2::kWeightCount floats in .nam order), swapped in once
  // the amp is silent.
  void loadWeights(const float* w) { pending_ = w; }
  bool weightsTaken() const { return pending_ == nullptr; }
  bool hasModel() const { return hasModel_; }
  // True while the network runs (on, warming up, or fading out).
  bool active() const { return running_; }
  // The gate's gain on the amp's output (1 = open), for tests.
  float gateGain() const { return gateCur_; }

  // Runs the network outside the audio (the device self-test): only while the
  // amp is off and idle with its model taken, and the audio leaves it alone
  // until endOffline(). The next on starts it from a cleared state anyway.
  bool beginOffline() {
    if (on_ || running_ || !hasModel_ || pending_) return false;
    offline_ = true;
    net_.reset();
    return true;
  }
  void processOffline(const float* in, float* out) { net_.process(in, out); }
  void endOffline() { offline_ = false; }

  // One block. Returns false while the amp is idle: out is not written and
  // the signal is the input, unchanged.
  bool process(const float* in, float* out) {
    if (offline_) return false;
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
      env_ = 0.0f;
      gateOpen_ = false;
      gateHeld_ = 0;
      gateCur_ = 0.0f;
      tone_.reset();
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
      tone_.process(y, B);
      if (warm_ > 0 && --warm_ == 0) swapping_ = false;
    } else {
      for (int i = 0; i < B; i++) y[i] = 0.0f;
    }
    const float o0 = outCur_, o1 = outTarget_;
    outCur_ = o1;
    const float q0 = gateCur_, q1 = gate(in);
    for (int i = 0; i < B; i++) {
      const float a = (float)(i + 1) / (float)B;
      const float d = d0 + (d1 - d0) * a, e = (e0 + (e1 - e0) * a) * (o0 + (o1 - o0) * a) * (q0 + (q1 - q0) * a);
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

  // The gate's gain at the end of this block, from the input's level.
  float gate(const float* in) {
    const float thr = gateThr_;
    if (thr <= 0.0f) return gateCur_ = 1.0f;
    // The level: each block's mean power, taken at once when it rises and
    // falling with 5 ms. The hold (20 ms, longer than a low string's cycle)
    // keeps the dips between cycles from closing it, and the power from
    // reacting to the noise's spikes.
    float p = 0.0f;
    for (int i = 0; i < B; i++) p += in[i] * in[i];
    p *= 1.0f / (float)B;
    env_ = p > env_ ? p : env_ * envFall_;
    const float open = thr * thr, close = 0.25f * open;  // 6 dB below
    if (env_ >= open) gateOpen_ = true;
    if (env_ >= close) gateHeld_ = 0;  // the hold counts only an unbroken stretch below
    else if (gateOpen_ && ++gateHeld_ > gateHold_) gateOpen_ = false;
    if (gateOpen_) gateCur_ = fminf(gateCur_ + gateOpenStep_, 1.0f);
    else gateCur_ = gateCur_ < 1e-4f ? 0.0f : gateCur_ * gateFall_;  // to 0 below -80 dB
    return gateCur_;
  }

  Net net_;
  ToneStack tone_;
  const float fs_, step_, gateOpenStep_, envFall_;
  const int gateHold_;  // blocks
  // Settings (written from the background, read once per block).
  volatile bool on_ = false;
  volatile float inTarget_ = 1.0f, outTarget_ = 1.0f, gateThr_ = 0.0f, gateFall_ = 0.0f;
  volatile bool offline_ = false;
  const float* volatile pending_ = nullptr;
  // State (audio interrupt only, except reads of the flags).
  volatile bool hasModel_ = false, running_ = false;
  bool swapping_ = false;
  int warm_ = 0;
  float dryCur_ = 1.0f, wetCur_ = 0.0f, inCur_ = 1.0f, outCur_ = 1.0f;
  float env_ = 0.0f, gateCur_ = 1.0f;  // the gate's detector and gain
  bool gateOpen_ = false;
  int gateHeld_ = 0;  // blocks below the closing level
  float cyclesAvg_ = 0.0f;
};

}  // namespace irdsp
