// irdsp_fft.h - small, portable radix-2 complex FFT used by the convolver.
//
// The FFT size is tiny (2 x audio block, e.g. 64 points), so the cost of the
// FFTs is negligible next to the spectral multiply-accumulate. This keeps the
// engine free of CMSIS-DSP version quirks and lets it be unit-tested on a PC.
#pragma once

#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdint.h>

namespace irdsp {

template <int N>
class FFT {
  static_assert(N >= 4 && (N & (N - 1)) == 0, "FFT size must be a power of two");

 public:
  FFT() {
    int bits = 0;
    while ((1 << bits) < N) bits++;
    for (int i = 0; i < N; i++) {
      int r = 0;
      for (int b = 0; b < bits; b++)
        if (i & (1 << b)) r |= 1 << (bits - 1 - b);
      rev_[i] = (uint16_t)r;
    }
    for (int i = 0; i < N / 2; i++) {
      double a = -2.0 * M_PI * i / N;
      cos_[i] = (float)cos(a);
      sin_[i] = (float)sin(a);
    }
  }

  // Forward transform of a real signal x[N]. Writes bins 0..N/2 interleaved
  // (re, im) into out[2*(N/2+1)].
  void forwardReal(const float* x, float* out) {
    for (int i = 0; i < N; i++) {
      re_[i] = x[rev_[i]];
      im_[i] = 0.0f;
    }
    butterflies();
    for (int k = 0; k <= N / 2; k++) {
      out[2 * k] = re_[k];
      out[2 * k + 1] = im_[k];
    }
  }

  // Inverse transform of a Hermitian spectrum given as bins 0..N/2 (re, im
  // interleaved). Writes the real time signal to y[N]. NOT scaled by 1/N
  // (the convolver folds the scale into the IR spectra).
  void inverseReal(const float* spec, float* y) {
    // IFFT(Y) = conj(FFT(conj(Y))); output is real so we only need Re().
    for (int i = 0; i < N; i++) {
      int k = rev_[i];
      if (k <= N / 2) {
        re_[i] = spec[2 * k];
        im_[i] = -spec[2 * k + 1];
      } else {
        int m = N - k;  // Hermitian mirror: Y[k] = conj(Y[N-k])
        re_[i] = spec[2 * m];
        im_[i] = spec[2 * m + 1];
      }
    }
    butterflies();
    for (int i = 0; i < N; i++) y[i] = re_[i];
  }

 private:
  void butterflies() {
    for (int len = 2; len <= N; len <<= 1) {
      int half = len >> 1;
      int step = N / len;
      for (int start = 0; start < N; start += len) {
        for (int j = 0; j < half; j++) {
          float wr = cos_[j * step], wi = sin_[j * step];
          int a = start + j, b = a + half;
          float tr = re_[b] * wr - im_[b] * wi;
          float ti = re_[b] * wi + im_[b] * wr;
          re_[b] = re_[a] - tr;
          im_[b] = im_[a] - ti;
          re_[a] += tr;
          im_[a] += ti;
        }
      }
    }
  }

  float re_[N], im_[N];
  float cos_[N / 2], sin_[N / 2];
  uint16_t rev_[N];
};

}  // namespace irdsp
