// Host-side tests for the 80s reverb, hall and gated (no hardware needed).
//   make -C test/host && ./test/host/test_reverb80
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

#include "Reverb80.h"

static const float kFs = 48000.0f;
static const int kS = 48000;  // samples per second
static const int kB = 32;
static int failures = 0;

static void check(bool ok, const char* what, double value) {
  std::printf("  %-56s %10.4g  %s\n", what, value, ok ? "OK" : "FAIL");
  if (!ok) failures++;
}

struct Rig {
  std::vector<float> mem;
  std::unique_ptr<irdsp::Reverb80> rv;
  explicit Rig(int mode) : mem(irdsp::Reverb80::kMemFloats), rv(new irdsp::Reverb80(kFs)) {
    rv->init(mem.data());
    // Firmware defaults (config.h).
    rv->setMode(mode);
    rv->setSize(0.5f);
    rv->setTone(0.5f);
    rv->setPreDelayMs(40.0f);
    rv->setMod(0.4f);
    rv->setEra(0.4f);
    rv->setGateMs(300.0f);
  }
  void run(const std::vector<float>& x, std::vector<float>& l, std::vector<float>& r) {
    l.assign(x.size(), 0.0f);
    r.assign(x.size(), 0.0f);
    for (size_t i = 0; i + kB <= x.size(); i += kB) rv->process(&x[i], &x[i], &l[i], &r[i], kB);
  }
};

static std::vector<float> noise(int n, unsigned seed, float amp) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> d(-amp, amp);
  std::vector<float> v(n);
  for (auto& s : v) s = d(rng);
  return v;
}

// Paul Kellet's economy pink filter on white noise (closer to a guitar).
static std::vector<float> pink(int n, unsigned seed, float amp) {
  auto w = noise(n, seed, 1.0f);
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

static std::vector<float> impulse(int n, float amp = 0.5f) {
  std::vector<float> v(n, 0.0f);
  v[0] = amp;
  return v;
}

static double rms(const std::vector<float>& v, size_t from, size_t to) {
  double s = 0;
  for (size_t i = from; i < to && i < v.size(); i++) s += (double)v[i] * v[i];
  return std::sqrt(s / (double)(to - from));
}
static double rms2(const std::vector<float>& l, const std::vector<float>& r, size_t from, size_t to) {
  return std::sqrt(0.5 * (rms(l, from, to) * rms(l, from, to) + rms(r, from, to) * rms(r, from, to)));
}
static double db(double x) { return 20.0 * std::log10(x + 1e-30); }

// RT60 from the Schroeder energy decay curve, extrapolated from -5..-25 dB.
static double rt60(const std::vector<float>& l, const std::vector<float>& r) {
  std::vector<double> edc(l.size() + 1, 0.0);
  for (size_t i = l.size(); i-- > 0;) edc[i] = edc[i + 1] + (double)l[i] * l[i] + (double)r[i] * r[i];
  size_t t5 = 0, t25 = 0;
  for (size_t i = 0; i < l.size(); i++) {
    const double d = 10.0 * std::log10(edc[i] / edc[0] + 1e-30);
    if (!t5 && d <= -5.0) t5 = i;
    if (d <= -25.0) { t25 = i; break; }
  }
  return t25 > t5 ? 3.0 * (double)(t25 - t5) / kFs : 0.0;
}

// Energy above ~14 kHz (a 2nd-order highpass), in dB.
static double highBandDb(const std::vector<float>& x) {
  irdsp::Biquad hp;
  hp.setHighpass(14000.0f, kFs);
  double e = 0;
  for (float v : x) {
    const float y = hp.process(v);
    e += (double)y * y;
  }
  return 10.0 * std::log10(e + 1e-30);
}

static void testHall() {
  std::printf("Hall\n");
  {
    // Stable at the maximum size: the tail decays after the input stops.
    Rig r(irdsp::Reverb80::kHall);
    r.rv->setSize(1.0f);
    auto x = noise(3 * kS, 1, 0.5f);
    x.resize(30 * kS, 0.0f);
    std::vector<float> l, rr;
    r.run(x, l, rr);
    const double a = rms2(l, rr, 3 * kS, 4 * kS), b = rms2(l, rr, 28 * kS, 29 * kS);
    bool finite = true;
    for (float v : l) finite = finite && std::isfinite(v);
    check(finite && b < a * 0.1, "max size: stable, the tail decays (dB over 25 s)", db(b / a));
  }
  // RT60 follows Size: 0.8 * 10^size seconds.
  for (float size : {0.0f, 0.5f, 1.0f}) {
    Rig r(irdsp::Reverb80::kHall);
    r.rv->setSize(size);
    r.rv->setTone(1.0f);
    r.rv->setPreDelayMs(0.0f);
    std::vector<float> l, rr;
    r.run(impulse(30 * kS), l, rr);
    const double want = 0.8 * std::pow(10.0, size), got = rt60(l, rr);
    char what[64];
    std::snprintf(what, sizeof(what), "size %.1f: RT60 %.2f s (want %.2f s, +-25 %%)", size, got, want);
    check(std::fabs(got / want - 1.0) < 0.25, what, got);
  }
  {
    // Pre-delay: nothing before it, the reverb right after it.
    Rig r(irdsp::Reverb80::kHall);
    r.rv->setPreDelayMs(80.0f);
    std::vector<float> l, rr;
    r.run(impulse(kS), l, rr);
    size_t first = 0;
    while (first < l.size() && std::fabs(l[first]) < 1e-7f && std::fabs(rr[first]) < 1e-7f) first++;
    const size_t pre = (size_t)(0.080f * kFs);
    check(first >= pre && first < pre + kS / 100, "pre-delay 80 ms: first wet sample (ms)", first * 1000.0 / kFs);
  }
  {
    // Stereo: the sides are decorrelated even from a mono input.
    Rig r(irdsp::Reverb80::kHall);
    std::vector<float> l, rr;
    r.run(noise(4 * kS, 2, 0.3f), l, rr);
    double xy = 0, xx = 0, yy = 0;
    for (size_t i = kS; i < l.size(); i++) xy += l[i] * rr[i], xx += l[i] * l[i], yy += rr[i] * rr[i];
    const double c = xy / std::sqrt(xx * yy);
    check(std::fabs(c) < 0.5, "L/R correlation (mono input)", c);
  }
  {
    // Mod changes the tail (chorus), and 0 leaves it static.
    Rig a(irdsp::Reverb80::kHall), b(irdsp::Reverb80::kHall);
    a.rv->setMod(0.0f);
    b.rv->setMod(1.0f);
    std::vector<float> la, ra, lb, rb;
    a.run(impulse(3 * kS), la, ra);
    b.run(impulse(3 * kS), lb, rb);
    double d = 0, e = 0;
    for (size_t i = kS; i < la.size(); i++) d += (la[i] - lb[i]) * (la[i] - lb[i]), e += la[i] * la[i];
    check(d / e > 0.1, "mod 100 vs 0: the tail moves (difference energy)", d / e);
  }
  {
    // Era: the late-80s converters keep the top end, the early ones lose it.
    Rig a(irdsp::Reverb80::kHall), b(irdsp::Reverb80::kHall);
    a.rv->setEra(0.0f);
    b.rv->setEra(1.0f);
    a.rv->setTone(1.0f);
    b.rv->setTone(1.0f);
    std::vector<float> la, ra, lb, rb;
    const auto x = noise(3 * kS, 3, 0.3f);
    a.run(x, la, ra);
    b.run(x, lb, rb);
    const double drop = highBandDb(la) - highBandDb(lb);
    check(drop > 20.0, "era 100 vs 0: energy above 14 kHz (dB less)", drop);
  }
  {
    // Level: 100 % wet about as loud as the input.
    Rig r(irdsp::Reverb80::kHall);
    const auto x = pink(6 * kS, 4, 0.3f);
    std::vector<float> l, rr;
    r.run(x, l, rr);
    const double d = db(rms2(l, rr, 3 * kS, 6 * kS) / rms(x, 3 * kS, 6 * kS));
    check(std::fabs(d) < 3.0, "wet level vs input, pink noise (dB)", d);
  }
}

// The burst's envelope from an impulse: RMS in 10 ms windows.
static std::vector<double> envelope(const std::vector<float>& l, const std::vector<float>& r) {
  std::vector<double> e;
  const size_t w = kS / 100;  // 10 ms
  for (size_t i = 0; i + w <= l.size(); i += w) e.push_back(rms2(l, r, i, i + w));
  return e;
}

static void testGated() {
  std::printf("Gated\n");
  for (float gate : {200.0f, 500.0f}) {
    Rig r(irdsp::Reverb80::kGated);
    r.rv->setGateMs(gate);
    r.rv->setPreDelayMs(0.0f);
    std::vector<float> l, rr;
    r.run(impulse(kS), l, rr);
    const auto e = envelope(l, rr);
    const int end = (int)(gate / 10.0f);  // windows
    double burst = 0;
    for (int k = 2; k < end - 2; k++) burst += e[k];
    burst /= (end - 4);
    double after = 0;
    for (int k = end + 4; k < end + 10; k++) after = std::max(after, e[k]);
    char what[64];
    std::snprintf(what, sizeof(what), "gate %.0f ms: 40-100 ms after it, below the burst (dB)", gate);
    check(db(after / burst) < -40.0, what, db(after / burst));
  }
  // Shape from Size: decaying, flat, rising (first vs last third of 300 ms).
  const float sizes[] = {0.0f, 0.5f, 1.0f};
  for (float size : sizes) {
    Rig r(irdsp::Reverb80::kGated);
    r.rv->setSize(size);
    r.rv->setPreDelayMs(0.0f);
    std::vector<float> l, rr;
    // Noise burst input: each tap then contributes evenly.
    auto x = noise(kS / 100, 5, 0.5f);
    x.resize(kS, 0.0f);
    r.run(x, l, rr);
    const double first = rms2(l, rr, kS * 3 / 100, kS * 10 / 100), last = rms2(l, rr, kS * 20 / 100, kS * 27 / 100);
    const double d = db(last / first);
    char what[64];
    if (size == 0.5f) {
      std::snprintf(what, sizeof(what), "size 50: flat burst (last vs first third, dB)");
      check(std::fabs(d) < 4.0, what, d);
    } else {
      std::snprintf(what, sizeof(what), "size %.0f: %s (last vs first third, dB)", size * 100, size < 0.5f ? "decaying" : "rising");
      check(size < 0.5f ? d < -6.0 : d > 6.0, what, d);
    }
  }
  {
    Rig r(irdsp::Reverb80::kGated);
    const auto x = pink(6 * kS, 6, 0.3f);
    std::vector<float> l, rr;
    r.run(x, l, rr);
    const double d = db(rms2(l, rr, 3 * kS, 6 * kS) / rms(x, 3 * kS, 6 * kS));
    check(std::fabs(d) < 3.0, "wet level vs input, pink noise (dB)", d);
  }
}

static void testBoth() {
  std::printf("Both modes\n");
  for (int mode : {irdsp::Reverb80::kHall, irdsp::Reverb80::kGated}) {
    Rig r(mode);
    std::vector<float> l, rr;
    r.run(noise(60 * kS, 7, 0.9f), l, rr);
    bool finite = true;
    for (size_t i = 0; i < l.size(); i++) finite = finite && std::isfinite(l[i]) && std::fabs(l[i]) < 8.0f;
    while (!r.rv->clearStep(4096)) {}
    std::vector<float> l2, r2;
    r.run(std::vector<float>(kS, 0.0f), l2, r2);
    double tail = 0;
    for (size_t i = 0; i < l2.size(); i++) tail = std::max(tail, (double)std::fabs(l2[i]) + std::fabs(r2[i]));
    check(finite && tail == 0.0, mode == irdsp::Reverb80::kHall ? "hall: 60 s of loud noise, then clear -> silence"
                                                                 : "gated: 60 s of loud noise, then clear -> silence",
          tail);
  }
}

int main() {
  testHall();
  testGated();
  testBoth();
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
