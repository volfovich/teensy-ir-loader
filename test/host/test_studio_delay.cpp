// Host-side tests for the 80s studio dual delay (no hardware needed).
//   make -C test/host && ./test/host/test_studio_delay
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

#include "StudioDelay.h"

static const float kFs = 48000.0f;
static const int kB = 32;
static const int kLen = 28000;
static int failures = 0;

static void check(bool ok, const char* what, double value) {
  std::printf("  %-52s %10.4g  %s\n", what, value, ok ? "OK" : "FAIL");
  if (!ok) failures++;
}

// Clean, flat settings: no drive, 16-bit era, full tone, no bass cut, no
// modulation, no ducking, no cross-feed.
struct Rig {
  std::vector<float> a, b;
  std::unique_ptr<irdsp::StudioDelay> d;
  Rig() : a(kLen), b(kLen), d(new irdsp::StudioDelay(kFs)) {
    d->init(a.data(), b.data(), kLen);
    d->setModDepth(0.0f);
    d->setDrive(0.0f);
    d->setEra(0.0f);
    d->setTone(1.0f);
    d->setBassCut(0.0f);
    d->setDuck(0.0f);
    d->setCross(0.0f);
    d->setRightRatio(0.75f);
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

static double db(double v) { return 20 * std::log10(v + 1e-30); }

// A 20 ms Hann-windowed 500 Hz burst starting at `at` (the filters are flat
// there, so repeat gains can be measured exactly).
static const int kBurst = 882;
static void addBurst(std::vector<float>& x, int at, float amp) {
  for (int i = 0; i < kBurst; i++) {
    const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / kBurst);
    x[at + i] += (float)(amp * w * std::sin(2 * M_PI * 500.0 * i / kFs));
  }
}
static double burstPeak(const std::vector<float>& v, int at) { return peakAbs(v, at, at + kBurst + 16); }

static void testDualTaps() {
  std::printf("Dual taps (T = 10000, right ratio 0.75, feedback 0.5, no cross-feed)\n");
  {
    Rig rig;
    const int T = 10000, TR = 7500;
    rig.d->setDelaySamples(T, true);
    rig.d->setFeedback(0.5f);
    std::vector<float> x(4 * T, 0.0f), l, r;
    x[100] = 1.0f;
    rig.run(x, l, r);
    const int pL = argmaxAbs(l, 100 + T / 2, 100 + 3 * T / 2);
    const int pR = argmaxAbs(r, 100 + TR / 2, 100 + 3 * TR / 2);
    check(std::abs(pL - 100 - T) <= 1, "1st repeat on L at T (offset)", pL - 100 - T);
    check(std::abs(pR - 100 - TR) <= 1, "1st repeat on R at 0.75 T (offset)", pR - 100 - TR);
    check(peakAbs(l, 0, 100 + T - 8) < 1e-6, "  ... and nothing on L before T", peakAbs(l, 0, 100 + T - 8));
  }
  {
    Rig rig;
    const int T = 10000, TR = 7500, at = 100;
    rig.d->setDelaySamples(T, true);
    rig.d->setFeedback(0.5f);
    std::vector<float> x(6 * T, 0.0f), l, r;
    addBurst(x, at, 1.0f);
    rig.run(x, l, r);
    // Input gain at feedback 0.5 is 1 - 0.6 * 0.125 = 0.925; output gain 1.2.
    const double g1 = burstPeak(l, at + T);
    check(std::fabs(g1 / (1.2 * 0.925) - 1) < 0.01, "1st repeat gain (1.2 x input gain)", g1);
    // Each later repeat: x 0.48 (feedback 0.5 x 0.96).
    const double g2 = burstPeak(l, at + 2 * T), g3 = burstPeak(l, at + 3 * T);
    check(std::fabs(g2 / g1 / 0.48 - 1) < 0.01, "L repeat-to-repeat ratio / 0.48", g2 / g1 / 0.48);
    check(std::fabs(g3 / g2 / 0.48 - 1) < 0.01, "  ... next repeat", g3 / g2 / 0.48);
    const double r1 = burstPeak(r, at + TR), r2 = burstPeak(r, at + 2 * TR);
    check(std::fabs(r1 / g1 - 1) < 0.01, "R 1st repeat as loud as L's", r1 / g1);
    check(std::fabs(r2 / r1 / 0.48 - 1) < 0.01, "R repeat-to-repeat ratio / 0.48", r2 / r1 / 0.48);
    check(burstPeak(r, at + T + TR) < 1e-4, "no cross-feed: L repeat absent from R at T + 0.75 T",
          burstPeak(r, at + T + TR));
  }
}

static void testCrossFeed() {
  std::printf("Cross-feed (0.3)\n");
  Rig rig;
  const int T = 10000, TR = 7500, at = 100;
  rig.d->setDelaySamples(T, true);
  rig.d->setFeedback(0.5f);
  rig.d->setCross(0.3f);
  std::vector<float> x(4 * T, 0.0f), l, r;
  addBurst(x, at, 1.0f);
  rig.run(x, l, r);
  const double g1 = burstPeak(l, at + T);
  // L's 1st repeat enters line B at T x 0.48 x 0.3 and comes out 0.75 T later;
  // R's own 2nd repeat keeps 0.48 x 0.7.
  const double x1 = burstPeak(r, at + T + TR), r2 = burstPeak(r, at + 2 * TR);
  check(std::fabs(x1 / g1 / (0.48 * 0.3) - 1) < 0.02, "L repeat on R at T + 0.75 T / expected", x1 / g1 / (0.48 * 0.3));
  check(std::fabs(r2 / g1 / (0.48 * 0.7) - 1) < 0.02, "R own 2nd repeat / expected", r2 / g1 / (0.48 * 0.7));
}

static void testStability() {
  std::printf("Stability\n");
  Rig rig;
  rig.d->setDelaySamples(5000, true);
  rig.d->setFeedback(1.0f);
  rig.d->setModDepth(1.0f);
  rig.d->setModRate(8.0f);
  rig.d->setDrive(1.0f);
  rig.d->setEra(1.0f);
  rig.d->setCross(0.5f);
  std::mt19937 rng(1);
  std::uniform_real_distribution<float> u(-1.0f, 1.0f);
  std::vector<float> x((int)(60 * kFs) / kB * kB), l, r;
  for (auto& s : x) s = u(rng);
  rig.run(x, l, r);
  bool finite = true;
  for (size_t i = 0; i < l.size(); i++) finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
  double p = std::max(peakAbs(l, 0, (int)l.size()), peakAbs(r, 0, (int)r.size()));
  check(finite, "60 s noise, max feedback/mod/drive/era/cross: finite", finite);
  check(p < 5.0, "  ... and bounded (peak)", p);
}

static void testTailAndClear() {
  std::printf("Tail and clearing\n");
  Rig rig;
  rig.d->setDelaySamples(10000, true);
  rig.d->setFeedback(0.85f);
  rig.d->setEra(1.0f);
  rig.d->setTone(0.5f);
  rig.d->setBassCut(0.2f);
  rig.d->setCross(0.2f);
  rig.d->setModDepth(0.3f);
  std::mt19937 rng(2);
  std::uniform_real_distribution<float> u(-0.5f, 0.5f);
  std::vector<float> x((int)(30 * kFs) / kB * kB, 0.0f), l, r;
  for (int i = 0; i < (int)kFs; i++) x[i] = u(rng);
  rig.run(x, l, r);
  const int n = (int)l.size();
  double tail = std::max(peakAbs(l, n - (int)kFs, n), peakAbs(r, n - (int)kFs, n));
  double line = 0;
  for (int i = 0; i < kLen; i++) line = std::max(line, (double)std::max(std::fabs(rig.a[i]), std::fabs(rig.b[i])));
  check(tail == 0.0, "era 1, high feedback: last second of tail exactly 0", tail);
  check(line == 0.0, "  ... and both lines exactly 0", line);

  for (int i = 0; i < (int)kFs; i++) x[i] = u(rng);
  x.resize((int)kFs);
  rig.run(x, l, r);
  int steps = 1;
  while (!rig.d->clearStep(4096)) steps++;
  double left = 0;
  for (int i = 0; i < kLen; i++) left = std::max(left, (double)std::max(std::fabs(rig.a[i]), std::fabs(rig.b[i])));
  check(left == 0.0 && steps == (2 * kLen + 4095) / 4096, "clearStep(4096) empties both lines (steps)", steps);
}

// Mean and largest deviation of the spacing between rising zero crossings.
static void zeroCrossings(const std::vector<float>& v, int from, int to, double* mean, double* dev) {
  std::vector<double> z;
  for (int i = from + 1; i < to; i++)
    if (v[i - 1] < 0 && v[i] >= 0) z.push_back(i - 1 + v[i - 1] / (double)(v[i - 1] - v[i]));
  double m = (z.back() - z.front()) / (z.size() - 1), d = 0;
  for (size_t i = 1; i < z.size(); i++) d = std::max(d, std::fabs(z[i] - z[i - 1] - m));
  *mean = m;
  *dev = d;
}

static void testTimeChange() {
  std::printf("Delay time change (crossfade, no glide)\n");
  const double w = 2 * M_PI * 220.0 / kFs;
  std::vector<float> x((int)(2 * kFs) / kB * kB);
  for (size_t i = 0; i < x.size(); i++) x[i] = 0.5f * (float)std::sin(w * i);
  auto run = [&](float feedback, std::vector<float>& l) {
    Rig rig;
    rig.d->setDelaySamples(3000, true);
    rig.d->setFeedback(feedback);
    std::vector<float> r(x.size());
    l.assign(x.size(), 0.0f);
    for (size_t i = 0; i + kB <= x.size(); i += kB) {
      if (i == (size_t)(0.5 * kFs) / kB * kB) rig.d->setDelaySamples(20000);
      if (i == (size_t)(1.2 * kFs) / kB * kB) rig.d->setDelaySamples(900);
      rig.d->process(&x[i], &l[i], &r[i], kB);
    }
  };
  std::vector<float> l;
  run(0.3f, l);
  double jump = 0;
  for (size_t i = 1; i < l.size(); i++) jump = std::max(jump, (double)std::fabs(l[i] - l[i - 1]));
  check(jump < 0.15, "largest sample step while changing the time", jump);
  // A glide would bend the pitch for as long as it runs; a crossfade only
  // shifts the phase during its 20 ms. Without feedback the output is just
  // the delayed sine, so between and after the fades its period is exact.
  run(0.0f, l);
  const double spans[3][2] = {{0.2, 0.49}, {0.53, 1.19}, {1.23, 1.9}};
  double worstMean = 0, worstDev = 0;
  for (auto& sp : spans) {
    double mean, dev;
    zeroCrossings(l, (int)(sp[0] * kFs), (int)(sp[1] * kFs), &mean, &dev);
    worstMean = std::max(worstMean, std::fabs(mean - kFs / 220.0));
    worstDev = std::max(worstDev, dev);
  }
  check(worstMean < 0.01, "period error outside the fades (samples)", worstMean);
  check(worstDev < 0.05, "  ... largest period deviation (samples)", worstDev);
}

static void testDucking() {
  std::printf("Ducking (24 dB, -36 dBFS threshold, 200 ms release)\n");
  // Ducking only scales the outputs, so the ducked / unducked output ratio
  // is the duck gain itself.
  std::vector<float> lDuck, lFlat, r;
  const int n = (int)(4 * kFs) / kB * kB, stop = (int)(2 * kFs);
  std::vector<float> x(n, 0.0f);
  for (int i = 0; i < stop; i++) x[i] = 0.3f * (float)std::sin(2 * M_PI * 500.0 * i / kFs);
  for (int duck = 0; duck < 2; duck++) {
    Rig rig;
    rig.d->setDelaySamples(5000, true);
    rig.d->setFeedback(0.9f);
    rig.d->setDuckThresholdDb(-36.0f);
    rig.d->setDuckReleaseMs(200.0f);
    rig.d->setDuck(duck ? 1.0f : 0.0f);
    rig.run(x, duck ? lDuck : lFlat, r);
  }
  auto ratio = [&](double t0, double t1) {
    const int a = (int)(t0 * kFs), b = (int)(t1 * kFs);
    return peakAbs(lDuck, a, b) / peakAbs(lFlat, a, b);
  };
  const double playing = db(ratio(1.0, 2.0)), after = db(ratio(2.95, 3.05));
  check(std::fabs(playing + 24.0) < 1.0, "repeats while playing (dB vs no ducking)", playing);
  check(std::fabs(after) < 0.5, "repeats 1 s after the input stops (dB)", after);
}

static void testVoicing() {
  std::printf("Voicing (drive, era)\n");
  const int T = 3000;
  auto firstRepeat = [&](float amp, float hz, float drive, float era) {
    Rig rig;
    rig.d->setDelaySamples(T, true);
    rig.d->setFeedback(0.0f);
    rig.d->setDrive(drive);
    rig.d->setEra(era);
    std::vector<float> x((int)(0.5 * kFs) / kB * kB), l, r;
    for (size_t i = 0; i < x.size(); i++) x[i] = amp * (float)std::sin(2 * M_PI * hz * i / kFs);
    rig.run(x, l, r);
    return peakAbs(l, T + 4000, (int)x.size());
  };
  const double quiet = firstRepeat(0.1f, 200, 0, 0), loud = firstRepeat(0.8f, 200, 0, 0);
  check(std::fabs(quiet / (1.2 * 0.1) - 1) < 1e-3, "drive 0: 1st repeat = 1.2 x input", quiet / 0.12);
  check(std::fabs(loud / quiet / 8.0 - 1) < 1e-3, "  ... linear (0.8 vs 0.1 input, / 8)", loud / quiet / 8.0);
  const double driven = firstRepeat(0.8f, 200, 1, 0);
  check(driven > 0.12 && driven < 0.16, "drive 1 squashes a 0.8 sine to about 1.2 / 8", driven);
  const double bright = db(firstRepeat(0.5f, 10000, 0, 0)), dark = db(firstRepeat(0.5f, 10000, 0, 1));
  check(dark < bright - 6.0, "era 1 cuts 10 kHz vs era 0 (dB)", dark - bright);
}

int main() {
  testDualTaps();
  testCrossFeed();
  testStability();
  testTailAndClear();
  testTimeChange();
  testDucking();
  testVoicing();
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
