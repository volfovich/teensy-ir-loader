// Host-side tests for the convolution engine (no hardware needed).
//   make -C test/host && ./test/host/test_convolver
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>

#include "PartitionedConvolver.h"

static std::vector<double> directConv(const std::vector<float>& x, const std::vector<float>& h) {
  std::vector<double> y(x.size(), 0.0);
  for (size_t n = 0; n < x.size(); n++) {
    double acc = 0.0;
    size_t kmax = std::min(h.size(), n + 1);
    for (size_t k = 0; k < kmax; k++) acc += (double)h[k] * x[n - k];
    y[n] = acc;
  }
  return y;
}

static std::vector<float> randomVec(int n, unsigned seed, float decay = 0.0f) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> d(-1.0f, 1.0f);
  std::vector<float> v(n);
  for (int i = 0; i < n; i++) v[i] = d(rng) * (decay > 0 ? std::exp(-decay * i) : 1.0f);
  return v;
}

static int failures = 0;

static void check(bool ok, const char* what, double err) {
  std::printf("  %-52s max err %.2e  %s\n", what, err, ok ? "OK" : "FAIL");
  if (!ok) failures++;
}

template <int B>
static void testLengths() {
  using Conv = irdsp::PartitionedConvolver<B, 8192>;
  std::printf("Block size %d (partitions up to %d)\n", B, Conv::kMaxParts);
  const int lens[] = {1, 7, B, B + 1, 1000, 4097, 8192};
  for (int len : lens) {
    auto conv = std::make_unique<Conv>();
    auto h = randomVec(len, 100 + len, 4.0f / len);
    conv->setFadeLength(B);  // finish the first switch in one block
    conv->loadIR(h.data(), len);
    // Run one block of silence to complete the crossfade from the identity IR.
    std::vector<float> zeros(B, 0.0f), tmp(B);
    conv->process(zeros.data(), tmp.data());

    const int blocks = (10000 + B - 1) / B + (8192 / B);
    auto x = randomVec(blocks * B, 7);
    std::vector<float> y(x.size());
    for (int b = 0; b < blocks; b++) conv->process(&x[b * B], &y[b * B]);
    auto ref = directConv(x, h);
    double maxErr = 0, maxRef = 0;
    for (size_t i = 0; i < y.size(); i++) {
      maxErr = std::max(maxErr, std::fabs(y[i] - ref[i]));
      maxRef = std::max(maxRef, std::fabs(ref[i]));
    }
    char label[96];
    std::snprintf(label, sizeof label, "IR length %5d vs direct convolution", len);
    check(maxErr <= 2e-5 * std::max(1.0, maxRef), label, maxErr / std::max(1.0, maxRef));
  }
}

template <int B>
static void testCrossfade() {
  using Conv = irdsp::PartitionedConvolver<B, 8192>;
  auto conv = std::make_unique<Conv>();
  const int fade = 512;
  conv->setFadeLength(fade);
  auto hA = randomVec(3000, 1, 0.002f);
  auto hB = randomVec(8192, 2, 0.0008f);
  const int blocks = 600;
  auto x = randomVec(blocks * B, 3);
  std::vector<float> y(x.size());

  int switchA = 0, switchB = 200;  // block indices where loads are requested
  for (int b = 0; b < blocks; b++) {
    if (b == switchA) conv->loadIR(hA.data(), (int)hA.size());
    if (b == switchB) {
      if (!conv->loadIR(hB.data(), (int)hB.size())) {
        std::printf("  load rejected unexpectedly\n");
        failures++;
      }
    }
    conv->process(&x[b * B], &y[b * B]);
  }
  auto refA = directConv(x, hA);
  auto refB = directConv(x, hB);
  double errA = 0, errFade = 0, errB = 0;
  int s0 = (switchA * B) + fade, s1 = switchB * B, s2 = s1 + fade;
  for (int i = s0; i < s1; i++) errA = std::max(errA, std::fabs(y[i] - refA[i]));
  for (int i = s1; i < s2; i++) {
    double g = (double)(i - s1) / fade;
    double ref = refA[i] + g * (refB[i] - refA[i]);
    errFade = std::max(errFade, std::fabs(y[i] - ref));
  }
  for (size_t i = s2; i < y.size(); i++) errB = std::max(errB, std::fabs(y[i] - refB[i]));
  std::printf("Crossfade, block size %d\n", B);
  check(errA < 1e-3, "before switch == conv(x, A)", errA);
  check(errFade < 1e-3, "during switch == linear mix of A and B", errFade);
  check(errB < 1e-3, "after switch == conv(x, B) incl. full history", errB);
  check(conv->readyForLoad(), "ready for next load after fade", 0);
}

int main() {
  testLengths<16>();
  testLengths<32>();
  testLengths<64>();
  testLengths<128>();
  testCrossfade<32>();
  testCrossfade<128>();
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
