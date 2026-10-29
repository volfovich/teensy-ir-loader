// NamA2.h - the Neural Amp Modeler "A2-Lite" network (see NAM.md).
//
// A2 is one fixed WaveNet shape; Lite is its 3-channel size:
//
//   rechannel:   x = r * in                                   (1 -> 3)
//   23 layers:   z = b + sum_k W_k * x(t - (K-1-k) * D) + m * in
//                a = LeakyReLU(z)                             (slope 0.01)
//                h += a                                       (the head's input)
//                x = x + L * a + l                            (1x1, residual)
//   head:        y = (hb + sum_k H_k * h(t - 15 + k)) * scale  (3 -> 1, 16 taps)
//
// with the kernel sizes and dilations in kKernel / kDilation. setWeights()
// takes the flat weight list of a .nam file in its order.
//
// Runs a block of B samples layer by layer: each layer's weights are loaded
// once per block and its taps are read as runs of consecutive frames. Each
// layer's input is kept in a ring buffer (channel-interleaved) as long as its
// taps reach back plus a block, rounded up to whole blocks: a block is
// written without wrapping, and a tap's run that crosses the end is read in
// two pieces. So every block costs the same.
//
// Portable C++, no Arduino. The history (kHistoryFloats) is handed over by the
// caller, so the firmware can put it in DTCM.
#pragma once

#include <math.h>
#include <string.h>

namespace irdsp {

namespace nama2 {

constexpr int kChannels = 3;
constexpr int kLayers = 23;
constexpr int kHeadTaps = 16;
constexpr float kLeakySlope = 0.01f;
constexpr int kKernel[kLayers] = {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6};
constexpr int kDilation[kLayers] = {1, 3, 7, 17, 41, 101, 239, 1, 3,   7,   17, 41,
                                    101, 239, 1, 13, 1, 3, 7, 17, 41, 101, 239};

// Frames a layer's taps reach back before the current one.
constexpr int reach(int l) { return (kKernel[l] - 1) * kDilation[l]; }

// Weights per layer: conv (K x C x C), its bias, the mixin, the 1x1 and its bias.
constexpr int layerWeights(int l) {
  return kKernel[l] * kChannels * kChannels + 3 * kChannels + kChannels * kChannels;
}

constexpr int countWeights() {
  int n = kChannels;  // rechannel
  for (int l = 0; l < kLayers; l++) n += layerWeights(l);
  return n + kHeadTaps * kChannels + 2;  // head, its bias, the scale
}
// The length of a Lite network's weight list in a .nam file (1871).
constexpr int kWeightCount = countWeights();

constexpr int countConv() {
  int n = 0;
  for (int l = 0; l < kLayers; l++) n += kKernel[l] * kChannels * kChannels;
  return n;
}
constexpr int kConvWeights = countConv();

// Multiply-adds per sample, for the CPU figures.
constexpr int countMacs() {
  int n = kChannels;
  for (int l = 0; l < kLayers; l++) n += kKernel[l] * kChannels * kChannels + kChannels + kChannels * kChannels;
  return n + kHeadTaps * kChannels;
}
constexpr int kMacsPerSample = countMacs();

}  // namespace nama2

template <int B>
class NamA2 {
 public:
  static constexpr int C = nama2::kChannels;
  static constexpr int kLayers = nama2::kLayers;

  // A layer's ring, in frames: its reach plus a block, rounded up to whole
  // blocks.
  static constexpr int frames(int l) { return (nama2::reach(l) + 2 * B - 1) / B * B; }
  static constexpr int countHistory() {
    int n = 0;
    for (int l = 0; l < kLayers; l++) n += frames(l) * C;
    return n;
  }
  // Floats of history memory begin() needs.
  static constexpr int kHistoryFloats = countHistory();

  // Hands over the history memory (kHistoryFloats) and clears it.
  void begin(float* history) {
    float* p = history;
    for (int l = 0; l < kLayers; l++) {
      ring_[l] = p;
      p += frames(l) * C;
    }
    reset();
  }

  // Clears the network's state: silence in, the biases' steady output out.
  void reset() {
    if (!ring_[0]) return;
    for (int l = 0; l < kLayers; l++) {
      memset(ring_[l], 0, sizeof(float) * frames(l) * C);
      pos_[l] = 0;
    }
    memset(headHist_, 0, sizeof(headHist_));
  }

  // Takes a .nam file's weight list (nama2::kWeightCount floats, in its order).
  void setWeights(const float* w) {
    for (int c = 0; c < C; c++) rechannel_[c] = *w++;
    float* conv = conv_;
    for (int l = 0; l < kLayers; l++) {
      const int K = nama2::kKernel[l];
      // The file has them [out][in][tap]; the taps loop wants [tap][out][in].
      for (int o = 0; o < C; o++)
        for (int i = 0; i < C; i++)
          for (int k = 0; k < K; k++) conv[(k * C + o) * C + i] = *w++;
      conv += K * C * C;
      for (int c = 0; c < C; c++) bias_[l][c] = *w++;
      for (int c = 0; c < C; c++) mixin_[l][c] = *w++;
      for (int o = 0; o < C; o++)
        for (int i = 0; i < C; i++) l1x1_[l][o][i] = *w++;
      for (int c = 0; c < C; c++) l1x1Bias_[l][c] = *w++;
    }
    for (int c = 0; c < C; c++)
      for (int k = 0; k < nama2::kHeadTaps; k++) head_[k][c] = *w++;
    headBias_ = *w++;
    scale_ = *w++;
  }

  // One block: B samples in, B out (may be the same buffer).
  void process(const float* in, float* out) {
    float cond[B];  // in may be out
    memcpy(cond, in, sizeof(cond));

    // The first layer's input; each layer then writes the next one's.
    float* x = ring_[0] + pos_[0] * C;
    const float r0 = rechannel_[0], r1 = rechannel_[1], r2 = rechannel_[2];
    for (int f = 0; f < B; f++) {
      x[f * C + 0] = r0 * cond[f];
      x[f * C + 1] = r1 * cond[f];
      x[f * C + 2] = r2 * cond[f];
    }

    float* h = headHist_ + (nama2::kHeadTaps - 1) * C;  // this block's head input
    const float* conv = conv_;
    static_assert(nama2::kKernel[0] == 6 && nama2::kKernel[kLayers - 1] == 6, "first and last layers");
    layer<6, true, false>(0, conv, cond, h);
    for (int l = 1; l < kLayers - 1; l++) {
      conv += nama2::kKernel[l - 1] * C * C;
      if (nama2::kKernel[l] == 6) layer<6, false, false>(l, conv, cond, h);
      else layer<15, false, false>(l, conv, cond, h);
    }
    conv += nama2::kKernel[kLayers - 2] * C * C;
    layer<6, false, true>(kLayers - 1, conv, cond, h);

    head(out);
    memmove(headHist_, headHist_ + B * C, sizeof(float) * (nama2::kHeadTaps - 1) * C);
    for (int l = 0; l < kLayers; l++) {
      pos_[l] += B;
      if (pos_[l] == frames(l)) pos_[l] = 0;
    }
  }

 private:
  // Costs on the Cortex-M7 (measured, test/teensy/nam_micro.cpp): a float
  // multiply-add takes 3 cycles to its result but issues every cycle, so three
  // independent sums keep it busy; loads and stores don't overlap with the
  // arithmetic, so every instruction costs about a cycle. The loops below
  // therefore keep weights in registers and touch memory as little as they
  // can: two taps per pass over the block (18 weights), the bias and mixin
  // folded into the first pass, and one pass for the activation, head sum
  // and residual (18 constants), which writes the next layer's input straight
  // into its ring. Fusing more into one pass is slower: its constants no
  // longer fit in the 32 float registers and are reloaded every frame.

  // Layer l's ring index of this block's frame 0, back frames ago.
  int at(int l, int back) const {
    const int i = pos_[l] - back;
    return i < 0 ? i + frames(l) : i;
  }

  // z (=|+=) W_a * a (+ W_b * b) for n frames: one or two taps (weights w and
  // w + 9). The first pass starts from the bias and the mixin
  // (z = bias + mix * cond).
  template <bool First, bool Two>
  static void taps(float* z, const float* w, const float* a, const float* b, const float* bias, const float* mix,
                   const float* cond, int n) {
    const float a00 = w[0], a01 = w[1], a02 = w[2], a10 = w[3], a11 = w[4], a12 = w[5];
    const float a20 = w[6], a21 = w[7], a22 = w[8];
    float b00 = 0, b01 = 0, b02 = 0, b10 = 0, b11 = 0, b12 = 0, b20 = 0, b21 = 0, b22 = 0;
    if (Two) {
      b00 = w[9], b01 = w[10], b02 = w[11], b10 = w[12], b11 = w[13], b12 = w[14];
      b20 = w[15], b21 = w[16], b22 = w[17];
    }
    float c0 = 0, c1 = 0, c2 = 0, m0 = 0, m1 = 0, m2 = 0;
    if (First) {
      c0 = bias[0], c1 = bias[1], c2 = bias[2];
      m0 = mix[0], m1 = mix[1], m2 = mix[2];
    }
    for (int f = 0; f < n; f++) {
      const float x0 = a[f * C + 0], x1 = a[f * C + 1], x2 = a[f * C + 2];
      float z0, z1, z2;
      if (First) {
        const float c = cond[f];
        z0 = c0 + m0 * c;
        z1 = c1 + m1 * c;
        z2 = c2 + m2 * c;
      } else {
        z0 = z[f * C + 0];
        z1 = z[f * C + 1];
        z2 = z[f * C + 2];
      }
      z0 += a00 * x0; z1 += a10 * x0; z2 += a20 * x0;
      z0 += a01 * x1; z1 += a11 * x1; z2 += a21 * x1;
      z0 += a02 * x2; z1 += a12 * x2; z2 += a22 * x2;
      if (Two) {
        const float y0 = b[f * C + 0], y1 = b[f * C + 1], y2 = b[f * C + 2];
        z0 += b00 * y0; z1 += b10 * y0; z2 += b20 * y0;
        z0 += b01 * y1; z1 += b11 * y1; z2 += b21 * y1;
        z0 += b02 * y2; z1 += b12 * y2; z2 += b22 * y2;
      }
      z[f * C + 0] = z0;
      z[f * C + 1] = z1;
      z[f * C + 2] = z2;
    }
  }

  // taps() over the block for layer l's taps backA (and backB) frames back,
  // in pieces where a run crosses the end of the ring.
  template <bool First, bool Two>
  void ringTaps(int l, float* z, const float* w, int backA, int backB, const float* cond) const {
    const float* ring = ring_[l];
    const int n = frames(l);
    int ia = at(l, backA), ib = Two ? at(l, backB) : 0;
    for (int f = 0; f < B;) {
      int run = B - f;
      if (n - ia < run) run = n - ia;
      if (Two && n - ib < run) run = n - ib;
      taps<First, Two>(z + f * C, w, ring + ia * C, ring + ib * C, bias_[l], mixin_[l], cond + f, run);
      f += run;
      ia += run;
      if (ia == n) ia = 0;
      ib += run;
      if (ib == n) ib = 0;
    }
  }

  // A layer: the conv in pairs of taps (a single one last for K odd), then
  // LeakyReLU, the head sum (h = a for the first layer) and the residual
  // into the next layer's input (none after the last layer).
  template <int K, bool First, bool Last>
  void layer(int l, const float* conv, const float* cond, float* h) {
    const int D = nama2::kDilation[l];
    float z[B * C];
    ringTaps<true, true>(l, z, conv, (K - 1) * D, (K - 2) * D, cond);
    int k = 2;
    for (; k + 1 < K; k += 2) ringTaps<false, true>(l, z, conv + k * C * C, (K - 1 - k) * D, (K - 2 - k) * D, cond);
    if (k < K) ringTaps<false, false>(l, z, conv + k * C * C, 0, 0, cond);

    const float* cur = ring_[l] + pos_[l] * C;
    float* next = Last ? nullptr : ring_[l + 1] + pos_[l + 1] * C;
    const float L00 = l1x1_[l][0][0], L01 = l1x1_[l][0][1], L02 = l1x1_[l][0][2];
    const float L10 = l1x1_[l][1][0], L11 = l1x1_[l][1][1], L12 = l1x1_[l][1][2];
    const float L20 = l1x1_[l][2][0], L21 = l1x1_[l][2][1], L22 = l1x1_[l][2][2];
    const float lb0 = l1x1Bias_[l][0], lb1 = l1x1Bias_[l][1], lb2 = l1x1Bias_[l][2];
    for (int f = 0; f < B; f++) {
      // max(z, 0.01 z) is LeakyReLU for a slope below 1.
      const float z0 = z[f * C + 0], z1 = z[f * C + 1], z2 = z[f * C + 2];
      const float a0 = fmaxf(z0, z0 * nama2::kLeakySlope);
      const float a1 = fmaxf(z1, z1 * nama2::kLeakySlope);
      const float a2 = fmaxf(z2, z2 * nama2::kLeakySlope);
      if (First) {
        h[f * C + 0] = a0;
        h[f * C + 1] = a1;
        h[f * C + 2] = a2;
      } else {
        h[f * C + 0] += a0;
        h[f * C + 1] += a1;
        h[f * C + 2] += a2;
      }
      if (!Last) {
        float n0 = cur[f * C + 0] + lb0, n1 = cur[f * C + 1] + lb1, n2 = cur[f * C + 2] + lb2;
        n0 += L00 * a0; n1 += L10 * a0; n2 += L20 * a0;
        n0 += L01 * a1; n1 += L11 * a1; n2 += L21 * a1;
        n0 += L02 * a2; n1 += L12 * a2; n2 += L22 * a2;
        next[f * C + 0] = n0;
        next[f * C + 1] = n1;
        next[f * C + 2] = n2;
      }
    }
  }

  // The head: 16 taps over the summed activations, two taps per pass.
  void head(float* out) const {
    float y[B];
    for (int f = 0; f < B; f++) y[f] = headBias_;
    for (int k = 0; k < nama2::kHeadTaps; k += 2) {
      const float p0 = head_[k][0], p1 = head_[k][1], p2 = head_[k][2];
      const float q0 = head_[k + 1][0], q1 = head_[k + 1][1], q2 = head_[k + 1][2];
      const float* s = headHist_ + k * C;
      for (int f = 0; f < B; f++) {
        float a = y[f] + p0 * s[f * C + 0], b = q0 * s[f * C + 3];
        a += p1 * s[f * C + 1];
        b += q1 * s[f * C + 4];
        a += p2 * s[f * C + 2];
        b += q2 * s[f * C + 5];
        y[f] = a + b;
      }
    }
    for (int f = 0; f < B; f++) out[f] = y[f] * scale_;
  }

  float* ring_[kLayers] = {};
  int pos_[kLayers] = {};  // ring index of this block's frame 0 (a multiple of B)
  float headHist_[(nama2::kHeadTaps - 1 + B) * C] = {};

  float rechannel_[C] = {};
  float conv_[nama2::kConvWeights] = {};  // per layer [tap][out][in]
  float bias_[kLayers][C] = {}, mixin_[kLayers][C] = {};
  float l1x1_[kLayers][C][C] = {}, l1x1Bias_[kLayers][C] = {};
  float head_[nama2::kHeadTaps][C] = {};
  float headBias_ = 0.0f, scale_ = 1.0f;
};

}  // namespace irdsp
