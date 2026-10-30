// Host-side tests for the doubler (no hardware needed).
//   make -C test/host && ./test/host/test_doubler
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "Doubler.h"

static const float kFs = 48000.0f;
static const int kB = 32;
static int failures = 0;

static void check(bool ok, const char* what, double value) {
  std::printf("  %-52s %10.4g  %s\n", what, value, ok ? "OK" : "FAIL");
  if (!ok) failures++;
}

struct Rig {
  std::vector<float> line = std::vector<float>(irdsp::Doubler::kLen);
  irdsp::Doubler d{kFs};
  Rig() { d.init(line.data()); }
  // Runs x through; returns how many blocks were processed (not idle).
  int run(const std::vector<float>& x, std::vector<float>& l, std::vector<float>& r) {
    l.assign(x.size(), 0.0f);
    r.assign(x.size(), 0.0f);
    int active = 0;
    for (size_t i = 0; i + kB <= x.size(); i += kB) {
      if (d.process(&x[i], &l[i], &r[i], kB)) {
        active++;
      } else {
        std::memcpy(&l[i], &x[i], sizeof(float) * kB);
        std::memcpy(&r[i], &x[i], sizeof(float) * kB);
      }
    }
    return active;
  }
};

static std::vector<float> noise(int n, unsigned seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> u(-0.5f, 0.5f);
  std::vector<float> x(n);
  for (auto& v : x) v = u(rng);
  return x;
}

static void testOff() {
  std::printf("Off\n");
  Rig g;
  std::vector<float> x = noise(48000, 1), l, r;
  check(g.run(x, l, r) == 0, "idle: no block processed", 0);
}

static void testHardPanned() {
  std::printf("On, width 100 %%\n");
  for (float ms : {1.0f, 10.0f, 35.0f}) {
    Rig g;
    g.d.set(ms, 1.0f);
    g.d.setOn(true);
    std::vector<float> x = noise(48000, 2), l, r;
    g.run(x, l, r);
    const int from = (int)(0.04f * kFs), lag = (int)lroundf(ms * 48.0f);  // past the 20 ms ramp
    bool leftSame = true, rightLate = true;
    for (int i = from; i < (int)x.size(); i++) {
      leftSame = leftSame && l[i] == x[i];
      rightLate = rightLate && r[i] == x[i - lag];
    }
    char what[64];
    std::snprintf(what, sizeof(what), "%.0f ms: left the input, right %d samples later, exactly", ms, lag);
    check(leftSame && rightLate, what, lag);
  }
}

static void testTimeChange() {
  std::printf("Time change\n");
  Rig g;
  g.d.set(10.0f, 1.0f);
  g.d.setOn(true);
  const int n = 48000;
  std::vector<float> x(n), l, r;
  for (int i = 0; i < n; i++) x[i] = 0.5f * (float)std::sin(2 * M_PI * 220 * i / kFs);
  l.assign(n, 0.0f);
  r.assign(n, 0.0f);
  const int at = 9600;  // 200 ms in: 10 -> 30 ms
  for (int i = 0; i + kB <= n; i += kB) {
    if (i == at) g.d.set(30.0f, 1.0f);
    g.d.process(&x[i], &l[i], &r[i], kB);
  }
  bool there = true;
  for (int i = at + 1200; i < n; i++) there = there && r[i] == x[i - 1440];
  check(there, "10 -> 30 ms: 25 ms later, exactly 1440 samples late", 1440);
  // The sine's own largest step is 0.5 x 2 pi 220 / 48000 = 0.0144.
  float worst = 0;
  for (int i = at; i < at + 1200; i++) worst = std::fmax(worst, std::fabs(r[i] - r[i - 1]));
  check(worst < 0.0145f, "  ... crossfaded: no click, no pitch bend (largest step)", worst);
}

static void testCentred() {
  std::printf("Width 0\n");
  Rig g;
  g.d.set(10.0f, 0.0f);
  g.d.setOn(true);
  std::vector<float> x = noise(48000, 5), l, r;
  g.run(x, l, r);
  bool same = true;
  for (size_t i = 0; i < x.size(); i++) same = same && l[i] == r[i];
  check(same, "left == right", 0);
}

static void testOnOff() {
  std::printf("On, then off\n");
  Rig g;
  g.d.setOn(true);
  std::vector<float> x = noise(48000, 6), l, r;
  g.run(x, l, r);
  g.d.setOn(false);
  // Off: 20 ms back to the input on both sides, then idle.
  std::vector<float> l2, r2;
  const int active = g.run(x, l2, r2);
  check(active > 0 && active <= (int)(0.02f * kFs / kB) + 1, "the fade out takes 20 ms (blocks)", active);
  float worst = 0;
  for (size_t i = 1; i < 960; i++) worst = std::fmax(worst, std::fabs((l2[i] - x[i]) - (l2[i - 1] - x[i - 1])));
  check(worst < 0.05f, "  ... smoothly (no step in the difference)", worst);
  std::vector<float> l3, r3;
  check(g.run(x, l3, r3) == 0, "  ... then idle", 0);
  g.d.setOn(true);
  std::vector<float> z(4800, 0.0f);
  g.run(z, l3, r3);
  float tail = 0;
  for (float v : r3) tail = std::fmax(tail, std::fabs(v));
  check(tail == 0.0f, "on again: the line starts clear (silence in, silence out)", tail);
}

int main() {
  testOff();
  testHardPanned();
  testTimeChange();
  testCentred();
  testOnOff();
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
