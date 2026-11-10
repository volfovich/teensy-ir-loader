// NoiseGate.h - the noise gate. In the firmware it sits after the amp and
// before the cab (AudioConvolverIR.h), so it gates the amp's output, or the
// dry guitar while the amp is off.
//
// It listens to the input (before the amp's input gain, so its threshold reads
// like the level meter), so the input's noise floor, amplified 50 dB or more
// by a high-gain model, isn't heard between notes. Two types:
//
//   kNam   the NAM plugin's gate, ported sample for sample from AudioDSPTools
//          (dsp/NoiseGate.cpp, MIT, Steven Atkinson): a downward expander.
//          The level is the input's power, averaged with a 10 ms half-life;
//          below the threshold T the gain is cut by 0.1 (L - T)^2 "dB",
//          applied as 10^(GR / 10) (so 10 dB below T is -20 dB, 20 dB below
//          is -80 dB). It opens at most in 5 ms, holds 10 ms below T before
//          it starts to close, and closes at most in 50 ms, each step half
//          the way to the target. A dying note fades out along that curve
//          instead of being cut. The release setting doesn't apply.
//   kHard  this firmware's first gate: opens within 1 ms above the
//          threshold; closes when the input stays 6 dB below it for an
//          unbroken 20 ms, then fades out exponentially, reaching -60 dB
//          after the release time (setRelease, 50 ms by default: a palm mute
//          is gone in about 140 ms). Its level is the blocks' power, so noise
//          spikes don't make it flicker.
//
// Threading model: the setters are called from the background; process()
// runs once per block in the audio interrupt.
#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace irdsp {

// log2 and 2^x for the audio interrupt, inline so they run from ITCM (the
// library's are in flash on the Teensy). Polynomials fitted to minimax on
// [0, 1): log2's error is below 2.1e-6 (6e-6 dB of power), exp2's relative
// error below 7.5e-8.
inline float fastLog2(float x) {  // x > 0, normal
  uint32_t b;
  memcpy(&b, &x, 4);
  const float e = (float)((int)(b >> 23) - 127);
  b = (b & 0x007FFFFFu) | 0x3F800000u;  // the mantissa, 1 .. 2
  float m;
  memcpy(&m, &b, 4);
  const float t = m - 1.0f;  // 0 .. 1: log2(1 + t)
  return e + t * (1.44255313f + t * (-0.71828178f + t * (0.45827016f + t * (-0.27953684f + t * (0.12345031f + t * -0.02645705f)))));
}

inline float fastExp2(float x) {
  if (x < -126.0f) return 0.0f;
  if (x > 127.0f) x = 127.0f;
  const float fl = floorf(x), f = x - fl;  // 0 .. 1
  const float p = 0.99999993f + f * (0.69315307f + f * (0.24015362f + f * (0.05582632f + f * (0.00898934f + f * 0.00187758f))));
  const uint32_t e = (uint32_t)((int)fl + 127) << 23;
  float s;
  memcpy(&s, &e, 4);
  return p * s;
}

template <int B>
class NoiseGate {
 public:
  enum class Type : uint8_t { kNam, kHard };

  // The NAM gate's fixed times (the plugin's), in seconds.
  static constexpr float kNamTime = 0.01f, kNamOpen = 0.005f, kNamHold = 0.01f, kNamClose = 0.05f;
  static constexpr float kNamRatio = 0.1f;

  explicit NoiseGate(float sampleRate)
      : fs_(sampleRate),
        openStep_((float)B / (sampleRate * 0.001f)),
        envFall_(expf(-(float)B / (sampleRate * 0.005f))),
        hold_((int)(sampleRate * 0.02f / (float)B)),
        alpha_(powf(0.5f, 1.0f / (kNamTime * sampleRate))),
        dt_(1.0f / sampleRate) {
    setRelease(50.0f);
  }

  // The threshold, linear on the input's scale (full scale 1); 0 = off.
  void setThreshold(float threshold) { thr_ = threshold; }
  // The fade from open to -60 dB, in ms (kHard only).
  void setRelease(float ms) { fall_ = expf(-6.9078f * (float)B / (fs_ * ms * 0.001f)); }
  // The type. Set before the first block, it is the starting type (nam
  // starts closed, as in the plugin); a change later starts the new one open
  // (it closes on its own if the input is quiet).
  void setType(Type t) {
    if (t != type_ && started_) typeChanged_ = true;
    type_ = t;
  }
  Type type() const { return type_; }
  // The gain at the end of the last block (1 = open), for tests.
  float gain() const { return type_ == Type::kNam ? namGain_ : cur_; }

  // One block: measures detect, then applies the gain to io (in place; io may
  // be detect). Off: io is left as it is.
  void process(const float* detect, float* io) {
    started_ = true;
    const Type t = type_;
    if (typeChanged_) {
      typeChanged_ = false;
      cur_ = 1.0f, open_ = true, held_ = 0;
      namState_ = kHolding, namGr_ = 0.0f, namHeld_ = 0.0f, namGain_ = 1.0f;
    }
    if (t == Type::kNam) {
      processNam(detect, io);
      return;
    }
    const float q0 = cur_, q1 = level(detect);
    if (q0 == 1.0f && q1 == 1.0f) return;
    for (int i = 0; i < B; i++) io[i] *= q0 + (q1 - q0) * (float)(i + 1) / (float)B;
  }

 private:
  enum { kHolding, kMoving };

  // AudioDSPTools' Trigger::Process and Gain::Process, one channel. The
  // level in dB and the gain are only worked out while moving; while
  // holding, the level is compared with the threshold as power.
  void processNam(const float* in, float* io) {
    const float thr = thr_;
    if (thr <= 0.0f) {
      namGain_ = 1.0f;
      return;
    }
    constexpr float kDbPerLog2 = 3.0103f;    // 10 log10(2)
    constexpr float kLog2PerDb = 0.3321928f;  // log2(10) / 10
    const float thrDb = 20.0f * log10Fast(thr), thrPow = thr * thr;
    const float maxGr = grFor(-120.0f, thrDb);  // < 0
    const float dOpen = -maxGr / kNamOpen * dt_, dClose = maxGr / kNamClose * dt_;
    const float a = alpha_, b = 1.0f - a;
    float level = namLevel_, gr = namGr_, held = namHeld_;
    if (gr < maxGr) gr = maxGr;  // the start (fully closed), or a new threshold
    int state = namState_;
    float g = 1.0f;
    for (int i = 0; i < B; i++) {
      const float x = in[i];
      level = a * level + b * x * x;
      level = level < 1e-12f ? 1e-12f : (level > 1000.0f ? 1000.0f : level);
      if (state == kHolding) {
        gr = 0.0f;
        if (level < thrPow) {
          held += dt_;
          if (held >= kNamHold) state = kMoving;
        } else {
          held = 0.0f;
        }
        g = 1.0f;
      } else {
        const float target = grFor(kDbPerLog2 * fastLog2(level), thrDb);
        if (target > gr) {
          float step = 0.5f * (target - gr);
          if (step > dOpen) step = dOpen;
          gr += step;
          if (gr >= 0.0f) gr = 0.0f, state = kHolding, held = 0.0f;
        } else if (target < gr) {
          float step = 0.5f * (target - gr);
          if (step < dClose) step = dClose;
          gr += step;
          if (gr < maxGr) gr = maxGr;
        }
        g = fastExp2(gr * kLog2PerDb);
      }
      io[i] *= g;
    }
    namLevel_ = level, namGr_ = gr, namHeld_ = held, namState_ = state, namGain_ = g;
  }

  // The plugin's quadratic gain reduction, in its "dB" (applied as 10^(GR/10)).
  static float grFor(float levelDb, float thrDb) {
    const float d = levelDb - thrDb;
    return levelDb < thrDb ? -kNamRatio * d * d : 0.0f;
  }
  static float log10Fast(float x) { return 0.30103f * fastLog2(x); }

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
  const float alpha_, dt_;  // the NAM gate's level average, a sample's time
  // Settings (written from the background, read once per block).
  volatile float thr_ = 0.0f, fall_ = 0.0f;
  volatile Type type_ = Type::kHard;
  volatile bool typeChanged_ = false, started_ = false;
  // The NAM gate's state (audio interrupt only), as the plugin starts it:
  // closed, the level at its floor.
  float namLevel_ = 1e-12f, namGr_ = -1e30f, namHeld_ = 0.0f, namGain_ = 0.0f;
  int namState_ = kMoving;
  // State (audio interrupt only).
  float env_ = 0.0f, cur_ = 1.0f;  // the detector and the gain
  bool open_ = false;
  int held_ = 0;  // blocks below the closing level
};

}  // namespace irdsp
