// NoiseGate.h - the noise gate, as in the NAM plugin. In the firmware it sits
// after the amp and before the cab (AudioConvolverIR.h), so it gates the
// amp's output, or the dry guitar while the amp is off.
//
// It listens to the input (before the amp's input gain, so its threshold reads
// like the level meter), so the input's noise floor, amplified 50 dB or more
// by a high-gain model, isn't heard between notes. Opens within 1 ms above the
// threshold; closes when the input stays 6 dB below it for an unbroken 20 ms,
// then fades out exponentially, reaching -60 dB after the release time
// (setRelease, 50 ms by default: a palm mute is gone in about 140 ms). Its
// level is the blocks' power, so noise spikes don't make it flicker.
//
// Threading model: the setters are called from the background; process()
// runs once per block in the audio interrupt.
#pragma once

#include <math.h>

namespace irdsp {

template <int B>
class NoiseGate {
 public:
  explicit NoiseGate(float sampleRate)
      : fs_(sampleRate),
        openStep_((float)B / (sampleRate * 0.001f)),
        envFall_(expf(-(float)B / (sampleRate * 0.005f))),
        hold_((int)(sampleRate * 0.02f / (float)B)) {
    setRelease(50.0f);
  }

  // The threshold, linear on the input's scale (full scale 1); 0 = off.
  void setThreshold(float threshold) { thr_ = threshold; }
  // The fade from open to -60 dB, in ms.
  void setRelease(float ms) { fall_ = expf(-6.9078f * (float)B / (fs_ * ms * 0.001f)); }
  // The gain at the end of the last block (1 = open), for tests.
  float gain() const { return cur_; }

  // One block: measures detect, then applies the gain to io (in place; io may
  // be detect). Off: io is left as it is.
  void process(const float* detect, float* io) {
    const float q0 = cur_, q1 = level(detect);
    if (q0 == 1.0f && q1 == 1.0f) return;
    for (int i = 0; i < B; i++) io[i] *= q0 + (q1 - q0) * (float)(i + 1) / (float)B;
  }

 private:
  // The gain at the end of this block, from the input's level.
  float level(const float* in) {
    const float thr = thr_;
    if (thr <= 0.0f) return cur_ = 1.0f;
    // The level: each block's mean power, taken at once when it rises and
    // falling with 5 ms. The hold (20 ms, longer than a low string's cycle)
    // keeps the dips between cycles from closing it, and the power from
    // reacting to the noise's spikes.
    float p = 0.0f;
    for (int i = 0; i < B; i++) p += in[i] * in[i];
    p *= 1.0f / (float)B;
    env_ = p > env_ ? p : env_ * envFall_;
    const float open = thr * thr, close = 0.25f * open;  // 6 dB below
    if (env_ >= open) open_ = true;
    if (env_ >= close) held_ = 0;  // the hold counts only an unbroken stretch below
    else if (open_ && ++held_ > hold_) open_ = false;
    if (open_) cur_ = fminf(cur_ + openStep_, 1.0f);
    else cur_ = cur_ < 1e-4f ? 0.0f : cur_ * fall_;  // to 0 below -80 dB
    return cur_;
  }

  const float fs_, openStep_, envFall_;
  const int hold_;  // blocks
  // Settings (written from the background, read once per block).
  volatile float thr_ = 0.0f, fall_ = 0.0f;
  // State (audio interrupt only).
  float env_ = 0.0f, cur_ = 1.0f;  // the detector and the gain
  bool open_ = false;
  int held_ = 0;  // blocks below the closing level
};

}  // namespace irdsp
