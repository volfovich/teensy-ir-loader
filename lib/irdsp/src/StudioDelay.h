// StudioDelay.h - 32-bit float "80s studio rack" dual delay (mono in, stereo
// out), voiced after the digital racks of the wet/dry/wet guitar rigs of that
// era (TC 2290, Korg SDD-3000, Roland SDE-3000):
//
//   in -> drive -> anti-alias LP -+-> (+) -> quantize -> line A (time T)         -> x duck -> out L
//                                 |    ^ fb x ((1 - c) filtA + c filtB)
//                                 +-> (+) -> quantize -> line B (time T x ratio) -> x duck -> out R
//                                 |    ^ fb x ((1 - c) filtB + c filtA)
//                                 +-> envelope follower -> duck gain
//
// filtX = bass cut (LP(line X)): every repeat loses some treble and bass.
//
//   - Two independent loops instead of a ping-pong. With the right time a
//     fraction of the left one, the repeats spread wide without bouncing. A
//     little cross-feed blends the two tails.
//   - Drive: the rack's input preamp, a blended cubic soft clip.
//   - Era: the converters. Bandwidth falls from 16 to 6 kHz and every
//     written sample is truncated to 16 down to 10 bits (grainy tails).
//     Truncation toward zero cannot sustain limit cycles, so a tail always
//     decays to exact silence.
//   - Duck: the repeats drop while you play and swell up in the gaps (the
//     2290's "dynamic delay"). It acts on the outputs only, not on the loop.
//   - Modulation: a slow sine, L and R in opposite phase (chorused tail).
//   - Time changes crossfade from the old to the new read position instead of
//     gliding, so the repeats keep their pitch (digital, not tape).
//
// Threading model: process() and clearStep() run in the audio interrupt. The
// setters only write a few values each, so they may be called from either
// side; the filters are recomputed inside process() when Tone or Era change.
#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "irdsp_blocks.h"

namespace irdsp {

class StudioDelay {
 public:
  static constexpr int kMinDelay = 128;  // samples

  explicit StudioDelay(float sampleRate = 44100.0f)
      : fs_(sampleRate), fadeLen_((int)(sampleRate * 0.02f)) {
    setFeedback(0.5f);
    setRightRatio(0.75f);
    setCross(0.2f);
    setModRate(0.8f);
    setDuckThresholdDb(-36.0f);
    setDuckReleaseMs(300.0f);
    duckAtk_ = 1.0f - expf(-1.0f / (0.005f * fs_));
  }

  // Hands over the two line buffers (len floats each) and clears them.
  // The longest delay is len - 4 samples.
  void init(float* lineA, float* lineB, int len) {
    a_ = lineA;
    b_ = lineB;
    len_ = len;
    reset();
    setDelaySamples((float)maxDelay(), true);
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
      inLp_.clear();
      lpA_.clear();
      lpB_.clear();
      bassA_.clear();
      bassB_.clear();
      env_ = 0.0f;
      duckGain_ = 1.0f;
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

  // ---- Parameters -----------------------------------------------------------

  // Left delay time in samples, clamped to kMinDelay .. maxDelay(). A change
  // crossfades to the new time over 20 ms; with force the time jumps.
  void setDelaySamples(float d, bool force = false) {
    const float hi = (float)maxDelay();
    d = d < (float)kMinDelay ? (float)kMinDelay : (d > hi ? hi : d);
    dSet_ = d;
    if (force) {
      curA_ = nextA_ = d;
      curB_ = nextB_ = rightTime(d);
      fadePos_ = 0;
    }
  }
  // Right time as a fraction of the left one, 0.1 .. 1.
  void setRightRatio(float r) { ratio_ = r < 0.1f ? 0.1f : (r > 1.0f ? 1.0f : r); }
  // Share of each loop's feedback taken from the other line, 0 .. 0.5.
  void setCross(float c) { cross_ = c < 0.0f ? 0.0f : (c > 0.5f ? 0.5f : c); }
  // Repeats, 0..1. More feedback also lowers the input level a little so the
  // repeats do not build up (as in StereoDelay).
  void setFeedback(float n) {
    n = clamp01(n);
    feedback_ = n * kFeedbackMax;
    inGainSet_ = 1.0f - 0.6f * n * n * n;
  }
  // Brightness of the repeats, 0..1 (1 = up to the era's bandwidth).
  void setTone(float n) { tone_ = clamp01(n); }
  // Bass loss per repeat, 0..1 (0 = none).
  void setBassCut(float n) {
    n = clamp01(n);
    bassCut_ = -(2.0f * n - n * n);
  }
  // Converter era, 0..1: 0 = late-80s 16-bit / 16 kHz, 1 = early 10-bit / 6 kHz.
  void setEra(float n) {
    n = clamp01(n);
    if (n == era_) return;
    era_ = n;
    qScale_ = exp2f(15.0f - 6.0f * n);
    qInv_ = 1.0f / qScale_;
  }
  // Input preamp grit, 0..1 (0 = clean and exactly linear).
  void setDrive(float n) {
    n = clamp01(n);
    if (n == drive_) return;
    drivePre_ = 1.0f + 7.0f * n * n;
    driveInv_ = 1.0f / drivePre_;
    drive_ = n;
  }
  // Ducking depth, 0..1 -> 0..24 dB less repeats while you play.
  void setDuck(float n) {
    n = clamp01(n);
    if (n == duck_) return;
    duck_ = n;
    duckFloor_ = powf(10.0f, -kDuckMaxDb * n / 20.0f);
  }
  // Input level where ducking starts; it is complete 12 dB above it.
  void setDuckThresholdDb(float db) { duckThr_ = powf(10.0f, db / 20.0f); }
  // How fast the repeats come back once you stop playing.
  void setDuckReleaseMs(float ms) { duckRel_ = 1.0f - expf(-1.0f / (ms * 0.001f * fs_)); }
  // Modulation rate in Hz and depth (0..1 -> up to +-1.5 ms).
  void setModRate(float hz) { lfo_.setRate(hz, fs_); }
  void setModDepth(float d) { modDepth_ = clamp01(d) * kModMaxS * fs_; }

  // ---- Audio ----------------------------------------------------------------

  // Processes n samples of mono input into the wet-only stereo outputs.
  void process(const float* in, float* outL, float* outR, int n) {
    if (tone_ != coefTone_ || era_ != coefEra_) updateFilters();
    if (fadePos_ == 0) {  // start a crossfade toward a new time
      const float tA = dSet_, tB = rightTime(dSet_);
      if (tA != curA_ || tB != curB_) {
        nextA_ = tA;
        nextB_ = tB;
        fadePos_ = 1;
      }
    }
    const float fb = feedback_, c = cross_, bCut = bassCut_, depth = modDepth_;
    const float inSet = inGainSet_, qs = qScale_, qi = qInv_;
    const float drv = drive_, pre = drivePre_, preInv = driveInv_;
    const float thr = duckThr_, thrInv = 1.0f / (3.0f * duckThr_), floor = duckFloor_;
    const float atk = duckAtk_, rel = duckRel_;
    float inGain = inGain_, env = env_, dg = duckGain_;
    for (int i = 0; i < n; i++) {
      inGain += (inSet - inGain) * 0.25f;

      float x = in[i];
      if (drv > 0.0f) x += drv * (softClip(x * pre) * preInv - x);
      x = inLp_.process(x);

      // Ducking: the input envelope pulls the wet gain down from unity
      // (at the threshold) to the floor (12 dB above it).
      const float ax = fabsf(x);
      env += (ax - env) * (ax > env ? atk : rel);
      float t = (env - thr) * thrInv;
      t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
      dg += ((1.0f - t * (1.0f - floor)) - dg) * kDuckSmooth;

      lfo_.tick();
      const float mA = (lfo_.value(0) - 1.0f) * depth;
      const float mB = (lfo_.value(128) - 1.0f) * depth;  // opposite phase
      float ra = readHermite(a_, len_, idx_, curA_ + mA);
      float rb = readHermite(b_, len_, idx_, curB_ + mB);
      if (fadePos_ > 0) {
        const float g = (float)fadePos_ / (float)fadeLen_;
        ra += (readHermite(a_, len_, idx_, nextA_ + mA) - ra) * g;
        rb += (readHermite(b_, len_, idx_, nextB_ + mB) - rb) * g;
        if (++fadePos_ > fadeLen_) {
          curA_ = nextA_;
          curB_ = nextB_;
          fadePos_ = 0;
        }
      }

      const float fA = bassA_.process(lpA_.process(ra), 1.0f, bCut, kBassLossF, kTrebleLossF);
      const float fB = bassB_.process(lpB_.process(rb), 1.0f, bCut, kBassLossF, kTrebleLossF);
      const float xin = x * inGain;
      a_[idx_] = quantize(xin + fb * (fA + (fB - fA) * c), qs, qi);
      b_[idx_] = quantize(xin + fb * (fB + (fA - fB) * c), qs, qi);
      if (++idx_ >= len_) idx_ = 0;

      outL[i] = ra * kOutGain * dg;
      outR[i] = rb * kOutGain * dg;
    }
    inGain_ = inGain;
    env_ = env;
    duckGain_ = dg;
  }

 private:
  static constexpr float kFeedbackMax = 0.96f;
  static constexpr float kOutGain = 1.2f;  // as StereoDelay, for similar loudness
  static constexpr float kTrebleLossF = 0.20f;
  static constexpr float kBassLossF = 0.05f;
  static constexpr float kModMaxS = 0.0015f;
  static constexpr float kDuckMaxDb = 24.0f;
  static constexpr float kDuckSmooth = 0.002f;  // ~11 ms gain smoothing
  static constexpr float kToneMinHz = 1500.0f;
  static constexpr float kLineMax = 4.0f;  // headroom of the stored samples

  float rightTime(float d) const {
    const float r = d * ratio_;
    return r < (float)kMinDelay ? (float)kMinDelay : r;
  }

  // Cubic soft clip: unity gain at 0, flat at +-1 from |x| = 1.5.
  static inline float softClip(float x) {
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (4.0f / 27.0f) * x * x * x;
  }

  // Truncates toward zero to the era's resolution (int conversion truncates).
  static inline float quantize(float v, float scale, float inv) {
    v = v < -kLineMax ? -kLineMax : (v > kLineMax ? kLineMax : v);
    return (float)(int32_t)(v * scale) * inv;
  }

  // Anti-alias ceiling 16 kHz (era 0) .. 6 kHz (era 1), log-scaled; the
  // repeats' lowpass runs from 1.5 kHz (tone 0) up to that ceiling.
  void updateFilters() {
    coefTone_ = tone_;
    coefEra_ = era_;
    const float ceil = 16000.0f * powf(6000.0f / 16000.0f, era_);
    const float lp = kToneMinHz * powf(ceil / kToneMinHz, tone_);
    inLp_.setLowpass(ceil, fs_);
    lpA_.setLowpass(lp, fs_);
    lpB_.setLowpass(lp, fs_);
  }

  const float fs_;
  const int fadeLen_;
  float* a_ = nullptr;
  float* b_ = nullptr;
  int len_ = 0;
  int idx_ = 0;
  int clearPos_ = 0;

  Biquad inLp_, lpA_, lpB_;
  Shelf bassA_, bassB_;
  Lfo lfo_;

  // Parameters.
  float feedback_ = 0.0f, inGainSet_ = 1.0f, inGain_ = 1.0f;
  float ratio_ = 0.75f, cross_ = 0.2f, bassCut_ = 0.0f, modDepth_ = 0.0f;
  float tone_ = 0.6f, era_ = 0.0f, coefTone_ = -1.0f, coefEra_ = -1.0f;
  float qScale_ = 32768.0f, qInv_ = 1.0f / 32768.0f;
  float drive_ = 0.0f, drivePre_ = 1.0f, driveInv_ = 1.0f;
  float duck_ = 0.0f, duckFloor_ = 1.0f, duckThr_ = 0.0f, duckAtk_ = 0.0f, duckRel_ = 0.0f;
  float env_ = 0.0f, duckGain_ = 1.0f;
  // Delay times (samples): current, and the crossfade target.
  float dSet_ = 0.0f, curA_ = 0.0f, curB_ = 0.0f, nextA_ = 0.0f, nextB_ = 0.0f;
  int fadePos_ = 0;  // 0 = no crossfade running
};

}  // namespace irdsp
