// Host-side tests for the plate reverb (no hardware needed).
//   make -C test/host && ./test/host/test_reverb
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

#include "PlateReverb.h"

static const float kFs = 44100.0f;
static const int kB = 32;
static int failures = 0;

static void check(bool ok, const char* what, double value) {
  std::printf("  %-52s %10.4g  %s\n", what, value, ok ? "OK" : "FAIL");
  if (!ok) failures++;
}

struct Rig {
  std::vector<float> mem;
  std::unique_ptr<irdsp::PlateReverb> rv;
  Rig() : mem(irdsp::PlateReverb::kMemFloats), rv(new irdsp::PlateReverb(kFs)) {
    rv->init(mem.data());
    // Firmware defaults (config.h).
    rv->setSize(0.5f);
    rv->setHiDamp(0.5f);
    rv->setLoDamp(0.2f);
    rv->setDiffusion(1.0f);
    rv->setLowpass(0.0f);
    rv->setHipass(0.0f);
  }
  // Runs x through the reverb in audio-sized blocks.
  void run(const std::vector<float>& x, std::vector<float>& l, std::vector<float>& r) {
    l.resize(x.size());
    r.resize(x.size());
    for (size_t i = 0; i + kB <= x.size(); i += kB) rv->process(&x[i], &x[i], &l[i], &r[i], kB);
  }
};

static std::vector<float> whiteNoise(int n, unsigned seed, float amp) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> d(-amp, amp);
  std::vector<float> v(n);
  for (auto& s : v) s = d(rng);
  return v;
}

// Paul Kellet's economy pink filter on white noise.
static std::vector<float> pinkNoise(int n, unsigned seed, float amp) {
  auto w = whiteNoise(n, seed, 1.0f);
  std::vector<float> v(n);
  float b0 = 0, b1 = 0, b2 = 0;
  for (int i = 0; i < n; i++) {
    b0 = 0.99765f * b0 + w[i] * 0.0990460f;
    b1 = 0.96300f * b1 + w[i] * 0.2965164f;
    b2 = 0.57000f * b2 + w[i] * 1.0526913f;
    v[i] = (b0 + b1 + b2 + w[i] * 0.1848f) * amp * 0.25f;
  }
  return v;
}

static double rms(const std::vector<float>& v, size_t from, size_t to) {
  double s = 0;
  for (size_t i = from; i < to; i++) s += (double)v[i] * v[i];
  return std::sqrt(s / (double)(to - from));
}

// Time (s) for the energy decay curve (Schroeder integral) to fall by 60 dB,
// extrapolated from the -5..-25 dB range.
static double rt60(const std::vector<float>& l, const std::vector<float>& r) {
  std::vector<double> edc(l.size() + 1, 0.0);
  for (size_t i = l.size(); i-- > 0;) edc[i] = edc[i + 1] + (double)l[i] * l[i] + (double)r[i] * r[i];
  const double e0 = edc[0];
  size_t t5 = 0, t25 = 0;
  for (size_t i = 0; i < l.size(); i++) {
    double db = 10.0 * std::log10(edc[i] / e0 + 1e-30);
    if (!t5 && db <= -5.0) t5 = i;
    if (db <= -25.0) { t25 = i; break; }
  }
  if (!t25) return 1e9;
  return 3.0 * (double)(t25 - t5) / kFs;
}

static void testStability() {
  std::printf("Stability\n");
  Rig rig;
  rig.rv->setSize(1.0f);
  rig.rv->setHiDamp(0.0f);
  rig.rv->setLoDamp(0.0f);
  auto x = whiteNoise((int)(60 * kFs) / kB * kB, 1, 1.0f);
  std::vector<float> l, r;
  rig.run(x, l, r);
  double peak = 0;
  bool finite = true;
  for (size_t i = 0; i < l.size(); i++) {
    finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
    peak = std::max(peak, (double)std::max(std::fabs(l[i]), std::fabs(r[i])));
  }
  check(finite, "60 s full-scale noise, max size: output finite", finite);
  check(peak < 8.0, "  ... and bounded (peak)", peak);
}

static void testDecay() {
  std::printf("Decay\n");
  const float sizes[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
  double prev = 0;
  bool monotonic = true;
  for (float s : sizes) {
    Rig rig;
    rig.rv->setSize(s);
    std::vector<float> x((int)(12 * kFs) / kB * kB, 0.0f), l, r;
    x[0] = 1.0f;
    rig.run(x, l, r);
    double t = rt60(l, r);
    char label[80];
    std::snprintf(label, sizeof label, "size %.2f: RT60 (s)", s);
    check(t > 0.05 && t < 12.0, label, t);
    monotonic = monotonic && t > prev;
    prev = t;
  }
  check(monotonic, "RT60 rises with size", monotonic);

  // Default settings: the tail must die away once the input stops.
  Rig rig;
  auto x = pinkNoise((int)(2 * kFs) / kB * kB, 2, 1.0f);
  x.resize((int)(12 * kFs) / kB * kB, 0.0f);
  std::vector<float> l, r;
  rig.run(x, l, r);
  const size_t n = l.size();
  double tail = 20.0 * std::log10(rms(l, n - 4410, n) / rms(l, 0, (size_t)(2 * kFs)) + 1e-30);
  check(tail < -100.0, "default size: tail 10 s after input stops (dB)", tail);
}

static void testStereo() {
  std::printf("Stereo\n");
  Rig rig;
  std::vector<float> x((int)(3 * kFs) / kB * kB, 0.0f), l, r;
  x[0] = 1.0f;
  rig.run(x, l, r);
  double sl = 0, sr = 0, slr = 0;
  for (size_t i = (size_t)(0.05 * kFs); i < l.size(); i++) {
    sl += (double)l[i] * l[i];
    sr += (double)r[i] * r[i];
    slr += (double)l[i] * r[i];
  }
  double corr = slr / std::sqrt(sl * sr);
  check(std::fabs(corr) < 0.3, "L/R correlation of the tail", corr);
}

static void testWetLevel() {
  std::printf("Wet level\n");
  Rig rig;
  auto x = pinkNoise((int)(10 * kFs) / kB * kB, 3, 0.5f);
  std::vector<float> l, r;
  rig.run(x, l, r);
  const size_t from = (size_t)(3 * kFs), to = x.size();
  double wet = std::sqrt(0.5 * (rms(l, from, to) * rms(l, from, to) + rms(r, from, to) * rms(r, from, to)));
  double db = 20.0 * std::log10(wet / rms(x, from, to));
  check(std::fabs(db) < 3.0, "wet vs dry loudness, pink noise (dB)", db);
}

static void testMixPower() {
  std::printf("Mix law\n");
  float w, d;
  irdsp::mixPower(0.0f, &w, &d);
  check(w == 0.0f && d == 1.0f, "mix 0 -> dry only", w);
  irdsp::mixPower(1.0f, &w, &d);
  check(w == 1.0f && d == 0.0f, "mix 1 -> wet only", d);
  double lo = 1e9, hi = 0;
  for (int i = 0; i <= 100; i++) {
    irdsp::mixPower(i / 100.0f, &w, &d);
    double p = (double)w * w + (double)d * d;
    lo = std::min(lo, p);
    hi = std::max(hi, p);
  }
  check(10 * std::log10(hi / lo) < 0.5, "power wet^2 + dry^2 ripple (dB)", 10 * std::log10(hi / lo));
}

int main() {
  testMixPower();
  testStability();
  testDecay();
  testStereo();
  testWetLevel();
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
