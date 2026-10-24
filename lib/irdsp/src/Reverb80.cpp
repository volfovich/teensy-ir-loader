// Reverb80.cpp - see Reverb80.h. The setup, clearing and coefficient code
// lives here so it can run from flash (IRDSP_COLD); process() and its inner
// loops stay in the header.
#include "Reverb80.h"

#include "irdsp_port.h"

namespace irdsp {

IRDSP_COLD void Reverb80::init(float* mem) {
  float* p = mem;
  pre_ = p;
  p += kPreMax;
  for (int i = 0; i < 4; i++) {
    diff_[i].bf = p;
    diff_[i].len = kDiffLen[i];
    p += kDiffLen[i];
  }
  shared_ = p;
  float* q = p;
  for (int i = 0; i < 8; i++) {
    line_[i] = q;
    lineLen_[i] = kHallLen[i] + kModPad;
    q += lineLen_[i];
  }
  mem_ = mem;
  reset();
}

IRDSP_COLD bool Reverb80::clearStep(int n) {
  if (!mem_) return true;
  if (clearPos_ == 0) {
    preIdx_ = 0;
    for (auto& d : diff_) d.idx = 0;
    for (auto& w : lineIdx_) w = 0;
    gateIdx_ = 0;
    for (auto& s : damp_) s = 0.0f;
    inLp_.clear();
    inLp2_.clear();
    hpL_.clear();
    hpR_.clear();
    toneL_.clear();
    toneR_.clear();
  }
  const int count = n < kMemFloats - clearPos_ ? n : kMemFloats - clearPos_;
  memset(mem_ + clearPos_, 0, count * sizeof(float));
  clearPos_ += count;
  if (clearPos_ < kMemFloats) return false;
  clearPos_ = 0;
  return true;
}

IRDSP_COLD void Reverb80::update() {
  cSize_ = size_;
  cTone_ = tone_;
  cEra_ = era_;
  cGate_ = gateMs_;
  const float ceil = 16000.0f * powf(6000.0f / 16000.0f, era_);
  inLp_.setLowpass(ceil, fs_, 0.54f);  // 4th-order Butterworth: steep, like
  inLp2_.setLowpass(ceil, fs_, 1.31f);  // the anti-alias filters of the day
  qScale_ = exp2f(15.0f - 6.0f * era_);
  qInv_ = 1.0f / qScale_;
  float toneHz = 2500.0f * powf(12000.0f / 2500.0f, tone_);
  if (toneHz > ceil) toneHz = ceil;
  dampK_ = 1.0f - expf(-6.2831853f * toneHz / fs_);
  toneL_.setLowpass(toneHz, fs_);
  toneR_.setLowpass(toneHz, fs_);
  hpL_.setHighpass(100.0f, fs_);
  hpR_.setHighpass(100.0f, fs_);
  // Hall decay: each line loses 60 dB per RT60 (x 1.2: the damping and
  // the low cut shorten the measured decay by about that much).
  const float rt60 = 1.2f * 0.8f * powf(10.0f, size_);
  for (int k = 0; k < 8; k++) gain_[k] = powf(10.0f, -3.0f * (float)kHallLen[k] / (rt60 * fs_));
  modDepth_ = 8.0f * mod_;
  // Gated: taps spread over the gate time with fixed jitter, gains tilted
  // by Size (decaying .. flat .. rising), the last 10 ms faded, and the
  // whole burst normalized to unity power.
  static const float kJitL[kTaps] = {0.31f, -0.22f, 0.12f, -0.35f, 0.27f, -0.08f, 0.38f, -0.29f,
                                     0.05f, -0.17f, 0.33f, -0.38f, 0.21f, -0.02f, 0.16f, -0.26f,
                                     0.36f, -0.11f, 0.09f, -0.33f, 0.24f, -0.19f, 0.02f, -0.06f};
  static const float kJitR[kTaps] = {-0.27f, 0.18f, -0.36f, 0.07f, -0.13f, 0.34f, -0.24f, 0.11f,
                                     -0.39f, 0.29f, -0.04f, 0.15f, -0.31f, 0.37f, -0.18f, 0.26f,
                                     -0.09f, 0.03f, -0.28f, 0.19f, -0.37f, 0.08f, -0.14f, 0.01f};
  const float gate = gateMs_ * 0.001f * fs_;
  const float tilt = 2.0f * size_ - 1.0f;
  const float fade = 0.010f * fs_;
  float pl = 0.0f, pr = 0.0f;
  for (int k = 0; k < kTaps; k++) {
    const float fl = ((float)k + 0.5f + kJitL[k]) / (float)kTaps;
    const float fr = ((float)k + 0.5f + kJitR[k]) / (float)kTaps;
    tapL_[k] = 1 + (int)(fl * gate);
    tapR_[k] = 1 + (int)(fr * gate);
    gainL_[k] = shape(fl, tilt, gate, fade) * ((k & 1) ? -1.0f : 1.0f);
    gainR_[k] = shape(fr, tilt, gate, fade) * ((k & 2) ? -1.0f : 1.0f);
    pl += gainL_[k] * gainL_[k];
    pr += gainR_[k] * gainR_[k];
  }
  const float nl = 1.0f / sqrtf(pl), nr = 1.0f / sqrtf(pr);
  for (int k = 0; k < kTaps; k++) {
    gainL_[k] *= nl;
    gainR_[k] *= nr;
  }
}

IRDSP_COLD float Reverb80::shape(float f, float tilt, float gate, float fade) {
  float g = 1.0f + tilt * (2.0f * f - 1.0f);
  const float tail = (1.0f - f) * gate / fade;
  if (tail < 1.0f) g *= tail < 0.0f ? 0.0f : tail;
  return g < 0.02f ? 0.02f : g;
}

}  // namespace irdsp
