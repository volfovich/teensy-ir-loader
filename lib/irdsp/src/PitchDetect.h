// PitchDetect.h - the tuner's pitch detection: YIN (de Cheveigné and
// Kawahara, 2002) on a window of the input, decimated to a low rate.
//
//   d(t)  = sum over the window of (x[j] - x[j + t])^2
//   d'(t) = d(t) * t / sum(d(1..t))          (cumulative mean normalized)
//   the period is the first dip of d' below kThreshold (its local minimum),
//   refined by a parabola through d at it and its neighbours
//
// Background only: a window of 2048 at 24 kHz with lags up to 50 Hz costs
// about 0.75 M multiply-adds (a few ms on the Teensy).
#pragma once

#include <math.h>

namespace irdsp {

struct Pitch {
  float hz = 0.0f;       // 0: no pitch (silence, noise, a chord)
  float clarity = 0.0f;  // 1 - d' at the period: 1 = a pure tone
};

struct Note {
  int index = 0;   // 0 = C .. 11 = B
  int octave = 0;  // scientific: A4 = 440 Hz
  float cents = 0.0f;  // -50 .. +50 from the note
};

class PitchDetect {
 public:
  static constexpr float kMinHz = 50.0f, kMaxHz = 1000.0f;
  static constexpr int kMaxLag = 512;
  static constexpr float kThreshold = 0.15f;
  static constexpr float kMinRms = 0.001f;  // -60 dBFS: quieter is no pitch

  // n samples at fs; n must exceed fs / kMinHz (the longest lag) by at
  // least as much again.
  static Pitch detect(const float* x, int n, float fs) {
    Pitch p;
    const int maxLag = (int)(fs / kMinHz) < kMaxLag ? (int)(fs / kMinHz) : kMaxLag;
    const int minLag = (int)(fs / kMaxHz);
    const int w = n - maxLag - 1;  // the window: lags up to maxLag + 1 are read
    if (w < maxLag || minLag < 2) return p;
    float e = 0.0f;
    for (int j = 0; j < n; j++) e += x[j] * x[j];
    if (e < kMinRms * kMinRms * (float)n) return p;

    float d[kMaxLag + 2], raw[kMaxLag + 2];
    d[0] = 1.0f;
    raw[0] = 0.0f;
    float sum = 0.0f;
    for (int t = 1; t <= maxLag + 1; t++) {
      float a0 = 0.0f, a1 = 0.0f;
      int j = 0;
      for (; j + 1 < w; j += 2) {
        const float u = x[j] - x[j + t], v = x[j + 1] - x[j + 1 + t];
        a0 += u * u;
        a1 += v * v;
      }
      for (; j < w; j++) {
        const float u = x[j] - x[j + t];
        a0 += u * u;
      }
      const float dt = a0 + a1;
      raw[t] = dt;
      sum += dt;
      d[t] = sum > 0.0f ? dt * (float)t / sum : 1.0f;
    }
    // The first dip below the threshold, followed to its minimum.
    int t = minLag;
    while (t <= maxLag && d[t] >= kThreshold) t++;
    if (t > maxLag) return p;
    while (t + 1 <= maxLag && d[t + 1] < d[t]) t++;
    // Parabola through the three around it, on d (less biased than d').
    const float a = raw[t - 1], b = raw[t], c = raw[t + 1];
    const float den = a - 2.0f * b + c;
    const float shift = den > 0.0f ? 0.5f * (a - c) / den : 0.0f;
    p.hz = fs / ((float)t + shift);
    p.clarity = 1.0f - d[t];
    return p;
  }

  // The nearest equal-tempered note to hz, A4 = 440.
  static Note note(float hz) {
    Note n;
    const float semis = 12.0f * log2f(hz / 440.0f) + 57.0f;  // from C0
    const int k = (int)floorf(semis + 0.5f);
    n.cents = 100.0f * (semis - (float)k);
    n.index = ((k % 12) + 12) % 12;
    n.octave = (k - n.index) / 12;
    return n;
  }
};

}  // namespace irdsp
