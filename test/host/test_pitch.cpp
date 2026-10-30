// Host-side tests for the tuner's pitch detection (no hardware needed).
//   make -C test/host && ./test/host/test_pitch
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "PitchDetect.h"

static const float kFs = 24000.0f;  // the tuner's rate (48 kHz / 2)
static const int kN = 2048;         // its window
static int failures = 0;

static void check(bool ok, const char* what, double value) {
  std::printf("  %-52s %10.4g  %s\n", what, value, ok ? "OK" : "FAIL");
  if (!ok) failures++;
}

static double cents(double hz, double ref) { return 1200.0 * std::log2(hz / ref); }

// A plucked string (Karplus-Strong with a fractional delay), at full
// brightness, skipped 50 ms in (past the pluck's noise).
static std::vector<float> pluck(double hz, unsigned seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> u(-1.0f, 1.0f);
  const double period = kFs / hz - 0.5;  // the averaging filter's half sample
  const int p = (int)period;
  const float frac = (float)(period - p);
  std::vector<float> line(p + 2);
  for (auto& v : line) v = u(rng);
  std::vector<float> out;
  const int skip = (int)(0.05f * kFs);
  int idx = 0;
  float prev = 0.0f;
  for (int i = 0; i < skip + kN; i++) {
    // Read p + frac behind (linear: an allpass would be exact; this is close).
    const int L = (int)line.size();
    const float a = line[(idx - p + L) % L], b = line[(idx - p - 1 + 2 * L) % L];
    const float v = a + (b - a) * frac;
    const float y = 0.996f * 0.5f * (v + prev);
    prev = v;
    line[idx] = y;
    idx = (idx + 1) % L;
    if (i >= skip) out.push_back(0.5f * y);
  }
  return out;
}

static void testSines() {
  std::printf("Sines, 55 Hz to 1 kHz\n");
  const double notes[] = {55.0, 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 440.0, 659.26, 987.77};
  double worst = 0;
  for (double hz : notes) {
    std::vector<float> x(kN);
    for (int i = 0; i < kN; i++) x[i] = 0.3f * (float)std::sin(2 * M_PI * hz * i / kFs + 0.3);
    const irdsp::Pitch p = irdsp::PitchDetect::detect(x.data(), kN, kFs);
    worst = std::fmax(worst, p.hz > 0 ? std::fabs(cents(p.hz, hz)) : 1e9);
  }
  check(worst < 1.0, "worst error, cents", worst);
}

static void testGuitar() {
  std::printf("Plucked strings (harmonics, as a guitar)\n");
  // Drop A, the six open strings, and the 12th fret of the high E.
  const double notes[] = {55.0, 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 659.26};
  double worst = 0;
  for (int k = 0; k < 8; k++) {
    const std::vector<float> x = pluck(notes[k], 7 + k);
    const irdsp::Pitch p = irdsp::PitchDetect::detect(x.data(), kN, kFs);
    const double c = p.hz > 0 ? cents(p.hz, notes[k]) : 1e9;
    std::printf("    %7.2f Hz -> %8.3f Hz (%+.2f cents, clarity %.2f)\n", notes[k], p.hz, c, p.clarity);
    worst = std::fmax(worst, std::fabs(c));
  }
  // The model's own tuning is only as exact as its linear fractional delay.
  check(worst < 1.5, "worst error, cents", worst);
}

static void testNoPitch() {
  std::printf("No pitch\n");
  std::vector<float> x(kN, 0.0f);
  check(irdsp::PitchDetect::detect(x.data(), kN, kFs).hz == 0.0f, "silence: none", 0);
  std::mt19937 rng(3);
  std::normal_distribution<float> g(0.0f, 0.2f);
  for (auto& v : x) v = g(rng);
  check(irdsp::PitchDetect::detect(x.data(), kN, kFs).hz == 0.0f, "white noise: none", 0);
  for (int i = 0; i < kN; i++) x[i] = 0.0005f * (float)std::sin(2 * M_PI * 110 * i / kFs);
  check(irdsp::PitchDetect::detect(x.data(), kN, kFs).hz == 0.0f, "a tone at -66 dBFS: none", 0);
}

static void testNotes() {
  std::printf("Note names\n");
  irdsp::Note n = irdsp::PitchDetect::note(440.0f);
  check(n.index == 9 && n.octave == 4 && std::fabs(n.cents) < 0.01f, "440 Hz = A4", n.cents);
  n = irdsp::PitchDetect::note(82.41f);
  check(n.index == 4 && n.octave == 2 && std::fabs(n.cents) < 0.1f, "82.41 Hz = E2", n.cents);
  n = irdsp::PitchDetect::note(440.0f * std::pow(2.0f, 20.0f / 1200.0f));
  check(n.index == 9 && std::fabs(n.cents - 20.0f) < 0.01f, "A4 + 20 cents", n.cents);
  n = irdsp::PitchDetect::note(261.63f * std::pow(2.0f, -30.0f / 1200.0f));
  check(n.index == 0 && n.octave == 4 && std::fabs(n.cents + 30.0f) < 0.1f, "C4 - 30 cents", n.cents);
  n = irdsp::PitchDetect::note(246.94f * std::pow(2.0f, 45.0f / 1200.0f));
  check(n.index == 11 && n.octave == 3, "B3 + 45 cents stays B3", n.cents);
}

int main() {
  testSines();
  testGuitar();
  testNoPitch();
  testNotes();
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
