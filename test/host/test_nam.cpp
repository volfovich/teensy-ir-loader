// test_nam.cpp - the A2-Lite engine (NamA2.h) against a plain reference.
//
// The reference runs one sample at a time straight from the formulas in
// NAM.md, reading the weights in .nam file order, with a ring buffer per
// layer. The engine must match it at every block size, through many history
// copy-backs, and after reset().
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <vector>

#include "NamA2.h"

using namespace irdsp;

static int g_fail = 0;
#define CHECK(cond, ...)                \
  do {                                  \
    if (!(cond)) {                      \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);              \
      printf("\n");                     \
      g_fail++;                         \
    }                                   \
  } while (0)

// ---- Random numbers -------------------------------------------------------------

static uint32_t g_rng = 12345;
static float urand() {  // -1 .. 1
  g_rng = g_rng * 1664525u + 1013904223u;
  return (float)(g_rng >> 8) / 8388608.0f - 1.0f;
}

// Random weights in .nam order, scaled roughly as a trained network's: each
// sum of n products gets about unit gain.
static std::vector<float> randomWeights() {
  constexpr int C = nama2::kChannels;
  std::vector<float> w;
  auto put = [&](int n, float scale) {
    for (int i = 0; i < n; i++) w.push_back(urand() * scale);
  };
  put(C, 1.0f);
  for (int l = 0; l < nama2::kLayers; l++) {
    const int K = nama2::kKernel[l];
    put(K * C * C, 1.0f / sqrtf((float)(K * C)));
    put(C, 0.1f);
    put(C, 0.5f);
    put(C * C, 0.3f);
    put(C, 0.05f);
  }
  put(nama2::kHeadTaps * C, 0.1f);
  put(1, 0.05f);
  put(1, 0.5f);
  return w;
}

// ---- The reference --------------------------------------------------------------

struct Reference {
  static constexpr int C = nama2::kChannels;
  std::vector<float> w;
  std::vector<std::vector<float>> ring;  // per layer: (reach + 1) frames x C
  std::vector<float> headRing;           // 16 frames x C
  long t = 0;

  explicit Reference(const std::vector<float>& weights) : w(weights) {
    for (int l = 0; l < nama2::kLayers; l++) ring.emplace_back((nama2::reach(l) + 1) * C, 0.0f);
    headRing.assign(nama2::kHeadTaps * C, 0.0f);
  }

  float step(float in) {
    const float* p = w.data();
    float x[C];
    for (int c = 0; c < C; c++) x[c] = p[c] * in;
    p += C;
    float hs[C] = {0, 0, 0};
    for (int l = 0; l < nama2::kLayers; l++) {
      const int K = nama2::kKernel[l], D = nama2::kDilation[l], R = nama2::reach(l) + 1;
      std::vector<float>& r = ring[l];
      for (int c = 0; c < C; c++) r[(t % R) * C + c] = x[c];
      float z[C] = {0, 0, 0};
      for (int o = 0; o < C; o++)
        for (int i = 0; i < C; i++)
          for (int k = 0; k < K; k++) {
            const long back = t - (long)(K - 1 - k) * D;
            const float v = back < 0 ? 0.0f : r[(back % R) * C + i];
            z[o] += p[(o * C + i) * K + k] * v;
          }
      p += K * C * C;
      for (int o = 0; o < C; o++) z[o] += p[o];
      p += C;
      for (int o = 0; o < C; o++) z[o] += p[o] * in;
      p += C;
      float a[C];
      for (int o = 0; o < C; o++) {
        a[o] = z[o] > 0.0f ? z[o] : 0.01f * z[o];
        hs[o] += a[o];
      }
      const float* L = p;
      p += C * C;
      for (int o = 0; o < C; o++) {
        float s = p[o];
        for (int i = 0; i < C; i++) s += L[o * C + i] * a[i];
        x[o] += s;
      }
      p += C;
    }
    const int T = nama2::kHeadTaps;
    for (int c = 0; c < C; c++) headRing[(t % T) * C + c] = hs[c];
    float y = p[T * C];
    for (int c = 0; c < C; c++)
      for (int k = 0; k < T; k++) {
        const long back = t - (T - 1 - k);
        if (back >= 0) y += p[c * T + k] * headRing[(back % T) * C + c];
      }
    y *= p[T * C + 1];
    t++;
    return y;
  }
};

// ---- Tests ----------------------------------------------------------------------

// A guitar-like test signal: decaying plucks over a little noise.
static std::vector<float> testSignal(int n, float level) {
  std::vector<float> s(n);
  for (int i = 0; i < n; i++) {
    const int ph = i % 9000;
    s[i] = level * (expf(-ph / 3000.0f) * sinf(2.0f * 3.14159265f * 110.0f * i / 48000.0f) * 0.8f + 0.05f * urand());
  }
  return s;
}

template <int B>
static void testBlockSize(const std::vector<float>& w, const std::vector<float>& sig, const std::vector<float>& ref) {
  std::vector<float> hist(NamA2<B>::kHistoryFloats);
  NamA2<B>* nam = new NamA2<B>;
  nam->begin(hist.data());
  nam->setWeights(w.data());

  const int n = (int)sig.size() / B * B;
  std::vector<float> out(n);
  for (int i = 0; i < n; i += B) nam->process(&sig[i], &out[i]);
  double maxErr = 0, peak = 0;
  for (int i = 0; i < n; i++) {
    maxErr = fmax(maxErr, fabs(out[i] - ref[i]));
    peak = fmax(peak, fabs(ref[i]));
  }
  CHECK(peak > 0.01, "B=%d: the reference is silent (peak %g)", B, peak);
  CHECK(maxErr <= 1e-5 * fmax(1.0, peak), "B=%d: max error %g (peak %g)", B, maxErr, peak);
  printf("  B=%3d: max error %.2e, peak %.3f, history %d KB\n", B, maxErr, peak,
         (int)(NamA2<B>::kHistoryFloats * sizeof(float) / 1024));

  // reset() gives the same output as a fresh network.
  nam->reset();
  const int m = 4096 / B * B;
  std::vector<float> again(m);
  for (int i = 0; i < m; i += B) nam->process(&sig[i], &again[i]);
  double resetErr = 0;
  for (int i = 0; i < m; i++) resetErr = fmax(resetErr, fabs(again[i] - out[i]));
  CHECK(resetErr == 0.0, "B=%d: after reset() the output differs by %g", B, resetErr);
  delete nam;
}

int main() {
  printf("test_nam\n");
  CHECK(nama2::kWeightCount == 1871, "weight count %d, expected 1871", nama2::kWeightCount);
  printf("  %d weights, %d multiply-adds per sample\n", nama2::kWeightCount, nama2::kMacsPerSample);

  const std::vector<float> w = randomWeights();
  CHECK((int)w.size() == nama2::kWeightCount, "random weights: %d", (int)w.size());

  // 1.5 s: every layer's history is copied back many times.
  const std::vector<float> sig = testSignal(72000, 0.5f);
  Reference r(w);
  std::vector<float> ref(sig.size());
  for (size_t i = 0; i < sig.size(); i++) ref[i] = r.step(sig[i]);

  testBlockSize<16>(w, sig, ref);
  testBlockSize<32>(w, sig, ref);
  testBlockSize<48>(w, sig, ref);
  testBlockSize<64>(w, sig, ref);
  testBlockSize<128>(w, sig, ref);

  // 60 s of loud noise stays finite.
  {
    std::vector<float> hist(NamA2<32>::kHistoryFloats);
    NamA2<32>* nam = new NamA2<32>;
    nam->begin(hist.data());
    nam->setWeights(w.data());
    float in[32], out[32];
    bool finite = true;
    for (int b = 0; b < 60 * 48000 / 32; b++) {
      for (int i = 0; i < 32; i++) in[i] = urand();
      nam->process(in, out);
      for (int i = 0; i < 32; i++) finite = finite && isfinite(out[i]);
    }
    CHECK(finite, "loud noise: output not finite");
    delete nam;
  }

  printf(g_fail ? "test_nam: %d FAILED\n" : "test_nam: all passed\n", g_fail);
  return g_fail ? 1 : 0;
}
