#include "PluckBass.h"

#include <math.h>

#include "irdsp_port.h"

namespace irdsp {

// Sustain (time to -60 dB) of the fundamental: an open E1 rings ~3 s,
// higher notes less, like a real bass.
static constexpr float kSustainE1 = 3.0f, kE1Hz = 41.2034f;
static constexpr float kMuteT60 = 0.08f, kStealT60 = 0.03f, kDeadT60 = 0.04f;
static constexpr float kOutGain = 0.5f;   // a vel-7 E1 peaks around -12 dBFS
static constexpr float kAttackS = 0.0015f;
static constexpr float kIdle = 1e-5f;     // a string below this is off
static constexpr float kEnvOff = 1e-4f;   // a faded string below this is off

IRDSP_COLD float PluckBass::noteHz(int note) { return 440.0f * powf(2.0f, (note - 69) / 12.0f); }

IRDSP_COLD void PluckBass::init(float sampleRate, float* buf) {
  fs_ = sampleRate;
  for (int i = 0; i < 2; i++) {
    slot_[i] = Slot();
    slot_[i].line = buf + i * kLineLen;
    for (int k = 0; k < kLineLen; k++) slot_[i].line[k] = 0.0f;
  }
  cur_ = 0;
  dcX_ = dcY_ = 0.0f;
  dcR_ = 1.0f - 6.2831853f * 20.0f / fs_;  // 20 Hz
  attStep_ = 1.0f / (kAttackS * fs_);
  body_.clear();
  setTone(tone_);
}

IRDSP_COLD void PluckBass::setTone(float t) {
  tone_ = clamp01(t);
  body_.setLowpass(1500.0f + 3500.0f * tone_, fs_, 0.8f);
}

// The envelope factor per sample that fades to -60 dB in t60 seconds.
IRDSP_COLD float PluckBass::fadeMul(float t60) const { return expf(-6.9078f / (t60 * fs_)); }

// The string's length for hz: the delay line, the low-pass's phase delay at
// hz and the allpass's fractional delay add up to one period.
IRDSP_COLD void PluckBass::tune(Slot& s, float hz, float t60) {
  s.hz = hz;
  // Loop low-pass at 6 .. 36 harmonics (Tone), so every note keeps about the
  // same number of lasting harmonics.
  float fc = hz * harmonics();
  if (fc > 0.4f * fs_) fc = 0.4f * fs_;
  s.lpA = 1.0f - expf(-6.2831853f * fc / fs_);
  const float w = 6.2831853f * hz / fs_, b = 1.0f - s.lpA;
  // The low-pass delays low frequencies most. A low note is heard mostly
  // through its 2nd and higher harmonics, so the string is tuned where the
  // 2nd harmonic lands (the fundamental is then a cent or two flat).
  const float w2 = 2.0f * w;
  const float lpDelay = atan2f(b * sinf(w2), 1.0f - b * cosf(w2)) / w2;
  const float period = fs_ / hz - lpDelay;
  // The allpass is well-behaved for a fractional delay of 0.5 .. 1.5.
  int len = (int)(period - 0.5f);
  if (len > kLineLen - 2) len = kLineLen - 2;
  if (len < 2) len = 2;
  const float frac = period - (float)len;
  s.len = len;
  s.ap = (1.0f - frac) / (1.0f + frac);
  // Loop gain for a fundamental decay of t60 seconds, given what the loop
  // low-pass already takes off at the fundamental.
  const float lpMag = s.lpA / sqrtf(1.0f - 2.0f * b * cosf(w) + b * b);
  const float g = powf(10.0f, -3.0f / (t60 * hz)) / lpMag;
  s.gain = g > 0.9999f ? 0.9999f : g;
}

// Fills the string with a pluck: a triangle peaking at 1/5 of its length
// (fingered near the bridge) with a trace of noise, low-passed to about as
// many harmonics as the string keeps (fewer for soft and dead notes), with
// no DC, scaled to the velocity's level. The low-pass runs forward and
// backward, twice each way around the period, so the shape stays centered
// and continuous where the period wraps.
IRDSP_COLD void PluckBass::pluck(Slot& s, int vel, bool dead) {
  const int n = s.len;
  const int p = n / 5 > 1 ? n / 5 : 1;
  const float v = (vel < 1 ? 1 : (vel > 9 ? 9 : vel)) / 9.0f;
  float fc = s.hz * harmonics() * (0.6f + 0.9f * v) * (dead ? 0.5f : 1.0f);
  if (fc > 0.4f * fs_) fc = 0.4f * fs_;
  const float a = 1.0f - expf(-6.2831853f * fc / fs_);
  int r = s.w - n;
  if (r < 0) r += kLineLen;
  auto at = [&](int k) -> float& { return s.line[(r + k) % kLineLen]; };
  for (int k = 0; k < n; k++) at(k) = (k < p ? (float)k / p : (float)(n - k) / (n - p)) + 0.03f * noise();
  float y = at(n - 1);
  for (int pass = 0; pass < 2; pass++)
    for (int k = 0; k < n; k++) at(k) = y += a * (at(k) - y);
  y = at(0);
  for (int pass = 0; pass < 2; pass++)
    for (int k = n - 1; k >= 0; k--) at(k) = y += a * (at(k) - y);
  float mean = 0.0f;
  for (int k = 0; k < n; k++) mean += at(k);
  mean /= n;
  float peak = 1e-9f;
  for (int k = 0; k < n; k++) {
    at(k) -= mean;
    if (fabsf(at(k)) > peak) peak = fabsf(at(k));
  }
  const float amp = v * sqrtf(v) * (dead ? 0.6f : 1.0f) / peak;
  for (int k = 0; k < n; k++) at(k) *= amp;
  // The loop filters start where the waveform is (just before the read
  // point), not at 0: a step there would circulate as a click per period.
  s.apX = s.apY = s.lpY = at(n - 1);
  s.env = 1.0f;
  s.envMul = dead ? fadeMul(kDeadT60) : 1.0f;
  s.att = 0.0f;
  s.peak = 1.0f;
  s.on = true;
}

IRDSP_COLD void PluckBass::noteOn(int note, int vel, bool dead) {
  while (note < kMinNote) note += 12;
  while (note > kMaxNote) note -= 12;
  // The sounding string is muted quickly; the other one plays the note.
  Slot& old = slot_[cur_];
  if (old.on) old.envMul = fadeMul(kStealT60);
  cur_ ^= 1;
  Slot& s = slot_[cur_];
  const float hz = noteHz(note);
  tune(s, hz, kSustainE1 * sqrtf(kE1Hz / hz));
  pluck(s, vel, dead);
}

IRDSP_COLD void PluckBass::mute() {
  const float m = fadeMul(kMuteT60);
  for (Slot& s : slot_)
    if (s.on && s.envMul > m) s.envMul = m;
}

bool PluckBass::process(float* out, int n) {
  if (!active()) return false;
  for (int i = 0; i < n; i++) {
    float x = 0.0f;
    for (Slot& s : slot_) {
      if (!s.on) continue;
      int r = s.w - s.len;
      if (r < 0) r += kLineLen;
      const float d = s.line[r];
      // Allpass (fractional delay), loop low-pass, loop gain.
      const float a = s.ap * (d - s.apY) + s.apX;
      s.apX = d;
      s.apY = a;
      s.lpY += s.lpA * (a - s.lpY);
      s.line[s.w] = s.gain * s.lpY;
      if (++s.w == kLineLen) s.w = 0;
      const float o = d * s.env * s.att;
      s.env *= s.envMul;
      s.att = s.att + attStep_ < 1.0f ? s.att + attStep_ : 1.0f;
      x += o;
      const float m = fabsf(o);
      s.peak = m > s.peak ? m : s.peak * 0.9995f;
    }
    // DC blocker, body low-pass, gentle saturation (a little warmth).
    const float hp = x - dcX_ + dcR_ * dcY_;
    dcX_ = x;
    dcY_ = hp;
    float y = body_.process(hp);
    y = y > 1.5f ? 1.5f : (y < -1.5f ? -1.5f : y);
    y = y - 0.08f * y * y * y;
    out[i] += kOutGain * y;
  }
  for (Slot& s : slot_)
    if (s.on && (s.peak < kIdle || s.env < kEnvOff)) s.on = false;
  return true;
}

}  // namespace irdsp
