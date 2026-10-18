// PartitionedConvolver.h - zero-added-latency, uniformly partitioned
// overlap-save FFT convolution (UPOLS).
//
//   B    = partition size = audio block size (e.g. 32 samples)
//   MAXN = longest supported IR (e.g. 8192 samples)
//
// The partition size equals the audio block size, so the convolution adds no
// latency on top of the audio I/O buffering: the output block t already
// contains h[0] * x[t].
//
// Two IR spectrum sets share one frequency-domain delay line (FDL), so a new
// IR can be crossfaded in without clicks: during the fade both IRs are
// evaluated against the same input history and their outputs are mixed.
//
// Threading model (single producer / single consumer):
//   - process() runs in the audio interrupt.
//   - loadIR() runs in the background (loop()). It only writes to the
//     inactive spectrum set and publishes it through an atomic state flag.
#pragma once

#include <atomic>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "irdsp_fft.h"

namespace irdsp {

template <int B, int MAXN>
class PartitionedConvolver {
  static_assert(B >= 8 && (B & (B - 1)) == 0, "block size must be a power of two");
  static_assert(MAXN % B == 0, "MAXN must be a multiple of the block size");

 public:
  static constexpr int kBlock = B;
  static constexpr int kMaxLen = MAXN;
  static constexpr int kMaxParts = MAXN / B;
  static constexpr int kBins = B + 1;        // bins 0..B of a 2B-point FFT
  static constexpr int kStride = 2 * kBins;  // floats per partition spectrum

  enum State : int { kIdle = 0, kPending = 1, kFading = 2 };

  PartitionedConvolver() { reset(); }

  // Clears all audio history and installs a unit impulse (clean passthrough).
  void reset() {
    memset(fdl_, 0, sizeof(fdl_));
    memset(inBuf_, 0, sizeof(inBuf_));
    head_ = 0;
    active_ = 0;
    fadePos_ = 0;
    fadeLen_ = 1024;
    state_.store(kIdle);
    const float one = 1.0f;
    installIR(0, &one, 1);
  }

  // Crossfade length (samples) used when switching IRs.
  void setFadeLength(int samples) { fadeLen_ = samples < B ? B : samples; }

  // ---- Background side --------------------------------------------------

  // True when a new IR may be loaded (no switch in progress).
  bool readyForLoad() const { return state_.load() == kIdle; }

  // Prepares an IR (already resampled and normalized, length <= MAXN) into
  // the inactive set and schedules a crossfade to it. Returns false if a
  // previous switch is still in progress (try again later).
  bool loadIR(const float* ir, int len) {
    if (state_.load() != kIdle) return false;
    int target = 1 - active_;
    installIR(target, ir, len);
    state_.store(kPending);  // publish (seq_cst store = barrier)
    return true;
  }

  int activePartitions() const { return parts_[active_]; }
  State state() const { return (State)state_.load(); }

  // ---- Audio side ---------------------------------------------------------

  // Processes one block of B samples. in and out may alias.
  void process(const float* in, float* out) {
    // 1) Sliding 2B input window: [previous block | current block].
    memcpy(inBuf_, inBuf_ + B, B * sizeof(float));
    memcpy(inBuf_ + B, in, B * sizeof(float));

    // 2) Spectrum of the window goes into the newest FDL slot.
    head_ = (head_ + 1 == kMaxParts) ? 0 : head_ + 1;
    fftAudio_.forwardReal(inBuf_, &fdl_[head_ * kStride]);

    // 3) Start a pending switch.
    int st = state_.load(std::memory_order_acquire);
    if (st == kPending) {
      fadePos_ = 0;
      state_.store(kFading);
      st = kFading;
    }

    // 4) Convolve with the active IR.
    convolveSet(active_, accA_);
    fftAudio_.inverseReal(accA_, timeA_);

    if (st != kFading) {
      memcpy(out, timeA_ + B, B * sizeof(float));
      return;
    }

    // 5) Crossfade to the new IR (both read the same input history).
    int target = 1 - active_;
    convolveSet(target, accB_);
    fftAudio_.inverseReal(accB_, timeB_);
    const float inv = 1.0f / (float)fadeLen_;
    for (int i = 0; i < B; i++) {
      int p = fadePos_ + i;
      float g = p >= fadeLen_ ? 1.0f : (float)p * inv;
      // equal-gain linear fade is fine: both signals are highly correlated
      out[i] = timeA_[B + i] + g * (timeB_[B + i] - timeA_[B + i]);
    }
    fadePos_ += B;
    if (fadePos_ >= fadeLen_) {
      active_ = target;
      state_.store(kIdle, std::memory_order_release);
    }
  }

 private:
  // Spectral multiply-accumulate: acc = sum_p X[t-p] * H[p].
  // Loop order is bin-outer / partition-inner so the accumulators stay in
  // registers. On Teensy 4 all of this lives in DTCM (no cache, 1-cycle
  // access), so the strided reads are free.
  void convolveSet(int set, float* acc) {
    const int P = parts_[set];
    const float* H = spec_[set];
    // Partitions 0..n1-1 read FDL slots head, head-1, ..., 0;
    // the rest wrap around to slots kMaxParts-1, kMaxParts-2, ...
    const int n1 = (head_ + 1 < P) ? head_ + 1 : P;
    const int n2 = P - n1;
    for (int k = 0; k < kBins; k++) {
      float acc4[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      const float* h = H + 2 * k;
      mac(fdl_ + head_ * kStride + 2 * k, h, n1, acc4);
      mac(fdl_ + (kMaxParts - 1) * kStride + 2 * k, h + n1 * kStride, n2, acc4);
      acc[2 * k] = acc4[0] + acc4[2];
      acc[2 * k + 1] = acc4[1] + acc4[3];
    }
  }

  // n complex multiply-accumulates walking x backwards and h forwards.
  // Two independent accumulator pairs hide the FPU's FMA latency; each
  // statement compiles to a single fused multiply-add (vfma/vfms).
  static inline void mac(const float* x, const float* h, int n, float* a) {
    float ar0 = a[0], ai0 = a[1], ar1 = a[2], ai1 = a[3];
    int p = 0;
    for (; p + 1 < n; p += 2) {
      const float xr0 = x[0], xi0 = x[1], hr0 = h[0], hi0 = h[1];
      const float xr1 = x[-kStride], xi1 = x[1 - kStride];
      const float hr1 = h[kStride], hi1 = h[kStride + 1];
      ar0 += xr0 * hr0;
      ai0 += xr0 * hi0;
      ar1 += xr1 * hr1;
      ai1 += xr1 * hi1;
      ar0 -= xi0 * hi0;
      ai0 += xi0 * hr0;
      ar1 -= xi1 * hi1;
      ai1 += xi1 * hr1;
      x -= 2 * kStride;
      h += 2 * kStride;
    }
    if (p < n) {
      ar0 += x[0] * h[0];
      ai0 += x[0] * h[1];
      ar0 -= x[1] * h[1];
      ai0 += x[1] * h[0];
    }
    a[0] = ar0; a[1] = ai0; a[2] = ar1; a[3] = ai1;
  }

  void installIR(int set, const float* ir, int len) {
    if (len > MAXN) len = MAXN;
    if (len < 1) len = 1;
    int P = (len + B - 1) / B;
    const float scale = 1.0f / (2.0f * B);  // inverse-FFT normalization
    float tmp[2 * B];
    for (int p = 0; p < P; p++) {
      for (int i = 0; i < B; i++) {
        int n = p * B + i;
        tmp[i] = n < len ? ir[n] * scale : 0.0f;
      }
      memset(tmp + B, 0, B * sizeof(float));
      fftLoad_.forwardReal(tmp, &spec_[set][p * kStride]);
    }
    parts_[set] = P;
  }

  // Large arrays first (DTCM on Teensy 4 when the object is a global).
  float spec_[2][kMaxParts * kStride];
  float fdl_[kMaxParts * kStride];
  float inBuf_[2 * B];
  float accA_[kStride], accB_[kStride];
  float timeA_[2 * B], timeB_[2 * B];
  FFT<2 * B> fftAudio_;  // used only by process()
  FFT<2 * B> fftLoad_;   // used only by installIR()
  int parts_[2];
  int head_;
  int active_;  // written only by process() (and reset())
  int fadePos_;
  int fadeLen_;
  std::atomic<int> state_;
};

}  // namespace irdsp
