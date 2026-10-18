// Host-side tests for the ping-pong delay (no hardware needed).
//   make -C test/host && ./test/host/test_delay
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

#include "StereoDelay.h"

static const float kFs = 44100.0f;
static const int kB = 32;
static const int kLen = 28000;
static int failures = 0;

static void check(bool ok, const char* what, double value) {
  std::printf("  %-52s %10.4g  %s\n", what, value, ok ? "OK" : "FAIL");
  if (!ok) failures++;
}

struct Rig {
  std::vector<float> a, b;
  std::unique_ptr<irdsp::StereoDelay> d;
  Rig() : a(kLen), b(kLen), d(new irdsp::StereoDelay(kFs)) {
    d->init(a.data(), b.data(), kLen);
    d->setModDepth(0.0f);
  }
  void run(const std::vector<float>& x, std::vector<float>& l, std::vector<float>& r) {
    l.resize(x.size());
    r.resize(x.size());
    for (size_t i = 0; i + kB <= x.size(); i += kB) d->process(&x[i], &l[i], &r[i], kB);
  }
};

static int argmaxAbs(const std::vector<float>& v, int from, int to) {
  int best = from;
  for (int i = from; i < to; i++)
    if (std::fabs(v[i]) > std::fabs(v[best])) best = i;
  return best;
}

static double peakAbs(const std::vector<float>& v, int from, int to) {
  double p = 0;
  for (int i = from; i < to; i++) p = std::max(p, (double)std::fabs(v[i]));
  return p;
}

static void testPingPong() {
  std::printf("Ping-pong impulse response (T = 10000, feedback 0.5)\n");
  Rig rig;
  const int T = 10000;
  rig.d->setDelaySamples(T, true);
  rig.d->setFeedback(0.5f);
  std::vector<float> x(8 * T, 0.0f), l, r;
  x[100] = 1.0f;
  rig.run(x, l, r);
  const int h = T / 2;
  int pL1 = argmaxAbs(l, 100 + T - h, 100 + T + h), pR1 = argmaxAbs(r, 100 + T - h, 100 + T + h);
  int pR2 = argmaxAbs(r, 100 + 2 * T - h, 100 + 2 * T + h);
  check(pL1 == 100 + T, "1st repeat on L at T (offset)", pL1 - 100 - T);
  check(peakAbs(r, 0, 100 + T + h) < 1e-6, "  ... and nothing on R before 1.5 T", peakAbs(r, 0, 100 + T + h));
  check(pR2 == 100 + 2 * T, "2nd repeat on R at 2T (offset)", pR2 - 100 - 2 * T);
  check(peakAbs(l, 100 + T + h, 100 + 3 * T - h) < 1e-6, "  ... and nothing on L between them", 0);
  (void)pR1;
  // Input gain at feedback 0.5 is 1 - 0.6 * 0.125 = 0.925; output gain 1.2.
  double g1 = l[pL1];
  check(std::fabs(g1 - 1.2 * 0.925) < 1e-3, "1st repeat gain (1.2 x input gain)", g1);
  // Each later repeat: x 0.48 (feedback 0.5 x 0.96), filters flat.
  double g3 = l[argmaxAbs(l, 100 + 3 * T - h, 100 + 3 * T + h)];
  double g5 = l[argmaxAbs(l, 100 + 5 * T - h, 100 + 5 * T + h)];
  check(std::fabs(g3 / g1 - 0.48) < 0.01, "repeat-to-repeat ratio (L, 2 steps)", g3 / g1 / 0.48);
  check(std::fabs(g5 / g3 - 0.48) < 0.01, "  ... next pair", g5 / g3 / 0.48);
}

static void testStability() {
  std::printf("Stability\n");
  Rig rig;
  rig.d->setDelaySamples(5000, true);
  rig.d->setFeedback(1.0f);
  rig.d->setModDepth(1.0f);
  rig.d->setModRate(1.0f);
  std::mt19937 rng(1);
  std::uniform_real_distribution<float> u(-1.0f, 1.0f);
  std::vector<float> x((int)(60 * kFs) / kB * kB), l, r;
  for (auto& s : x) s = u(rng);
  rig.run(x, l, r);
  bool finite = true;
  for (size_t i = 0; i < l.size(); i++) finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
  double p = std::max(peakAbs(l, 0, (int)l.size()), peakAbs(r, 0, (int)r.size()));
  check(finite, "60 s noise, max feedback and mod: output finite", finite);
  check(p < 12.0, "  ... and bounded (peak)", p);
}

static void testFractional() {
  std::printf("Fractional delay accuracy\n");
  Rig rig;
  const float D = 4321.37f;
  rig.d->setDelaySamples(D, true);
  rig.d->setFeedback(0.0f);
  rig.d->setTreble(1.0f);
  const double w = 2 * M_PI * 1000.0 / kFs;
  std::vector<float> x((int)(0.5 * kFs) / kB * kB), l, r;
  for (size_t i = 0; i < x.size(); i++) x[i] = (float)std::sin(w * i);
  rig.run(x, l, r);
  // Feedback 0: input gain settles to 1; L = 1.2 * x[n - D] (through the
  // flat tone filter).
  double err = 0;
  for (size_t i = 8000; i < x.size(); i++) err = std::max(err, std::fabs(l[i] / 1.2 - std::sin(w * (i - D))));
  check(err < 1e-3, "1 kHz sine at D = 4321.37 vs ideal", err);
}

static void testGlide() {
  std::printf("Delay time glide\n");
  Rig rig;
  rig.d->setDelaySamples(3000, true);
  rig.d->setFeedback(0.3f);
  const double w = 2 * M_PI * 220.0 / kFs;
  std::vector<float> x((int)(2 * kFs) / kB * kB), l(x.size()), r(x.size());
  for (size_t i = 0; i < x.size(); i++) x[i] = 0.5f * (float)std::sin(w * i);
  for (size_t i = 0; i + kB <= x.size(); i += kB) {
    if (i == (size_t)(0.5 * kFs) / kB * kB) rig.d->setDelaySamples(20000);
    if (i == (size_t)(1.2 * kFs) / kB * kB) rig.d->setDelaySamples(900);
    rig.d->process(&x[i], &l[i], &r[i], kB);
  }
  double jump = 0;
  for (size_t i = 1; i < l.size(); i++) jump = std::max(jump, (double)std::fabs(l[i] - l[i - 1]));
  // The dry sine's own step is 0.5 * w = 0.016; the wet signal is at most a
  // few times louder and bends in pitch while gliding, but must not jump.
  check(jump < 0.15, "largest sample step while changing the time", jump);
}

static void testTailAndClear() {
  std::printf("Tail and clearing\n");
  Rig rig;
  rig.d->setDelaySamples(16000, true);
  rig.d->setFeedback(0.35f);
  rig.d->setTrebleCut(0.4f);
  rig.d->setBassCut(0.2f);
  std::mt19937 rng(2);
  std::uniform_real_distribution<float> u(-0.5f, 0.5f);
  std::vector<float> x((int)(14 * kFs) / kB * kB, 0.0f), l, r;
  for (int i = 0; i < (int)kFs; i++) x[i] = u(rng);
  rig.run(x, l, r);
  const int n = (int)l.size();
  double tail = 20 * std::log10(std::max(peakAbs(l, n - 4410, n), peakAbs(r, n - 4410, n)) + 1e-30);
  check(tail < -100.0, "default-ish settings: tail 13 s after input stops (dB)", tail);

  // Refill, then clear in 4096-sample steps.
  for (int i = 0; i < (int)kFs; i++) x[i] = u(rng);
  x.resize((int)kFs);
  rig.run(x, l, r);
  int steps = 1;
  while (!rig.d->clearStep(4096)) steps++;
  double left = 0;
  for (int i = 0; i < kLen; i++) left = std::max(left, (double)std::max(std::fabs(rig.a[i]), std::fabs(rig.b[i])));
  check(left == 0.0 && steps == (2 * kLen + 4095) / 4096, "clearStep(4096) empties both lines (steps)", steps);
}

int main() {
  testPingPong();
  testStability();
  testFractional();
  testGlide();
  testTailAndClear();
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
