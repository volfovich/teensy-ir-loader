// Host-side tests for FxStage (an effect's on/off, mix and idle clearing),
// the reverb's mode switch and the delay's time jump after a clear.
//   make -C test/host test_fxstage && ./test/host/test_fxstage
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "FxStage.h"
#include "Reverb80.h"
#include "StudioDelay.h"

static const float kFs = 44100.0f;
static const int kB = 32;
static int failures = 0;

static void check(bool ok, const char* what, double value) {
  std::printf("  %-56s %10.4g  %s\n", what, value, ok ? "OK" : "FAIL");
  if (!ok) failures++;
}

// An effect that rings: a one-pole decay of its input (wet = state), with
// the calls FxStage makes counted.
struct FakeFx {
  explicit FakeFx(float) {}
  float state = 0.0f;
  int clears = 0, fullClears = 0, processed = 0;
  int clearLeft = 0;  // floats still to clear in a running clear
  bool ready() const { return true; }
  bool clearStep(int n) {
    if (clearLeft == 0) clearLeft = 10000;
    clears++;
    if (n >= 10000) fullClears++;
    clearLeft = n >= clearLeft ? 0 : clearLeft - n;
    if (clearLeft == 0) state = 0.0f;
    return clearLeft == 0;
  }
  void process(const float* in, const float* inR, float* outL, float* outR, int n) {
    (void)inR;
    processed++;
    for (int i = 0; i < n; i++) {
      state = state * 0.99f + in[i];
      if (std::fabs(state) < 1e-7f) state = 0.0f;  // decays to exact silence
      outL[i] = outR[i] = state;
    }
  }
};

using Stage = irdsp::FxStage<FakeFx, kB, true>;

static bool run(Stage& s, float x, float* l, float* r) {
  float in[kB];
  for (float& v : in) v = x;
  return s.process(in, in, l, r);
}

static void testStage() {
  std::printf("FxStage\n");
  std::unique_ptr<Stage> s(new Stage(kFs));
  float l[kB], r[kB];
  check(!run(*s, 0.5f, l, r) && s->fx().processed == 0, "off from the start: idle, the effect not run", s->fx().processed);
  s->setMix(1.0f);
  s->setOn(true);
  check(run(*s, 0.5f, l, r) && s->running(), "on: runs", s->fx().processed);
  for (int i = 0; i < 100; i++) run(*s, 0.5f, l, r);
  check(l[kB - 1] > 1.0f, "wet only after the ramps: the effect's output", l[kB - 1]);

  s->setOn(false);
  int blocks = 0;
  while (s->running() && blocks < 5000) {
    run(*s, 0.0f, l, r);
    blocks++;
  }
  const double ms = blocks * kB * 1000.0 / kFs;
  check(!s->running() && ms > 500.0, "off: rings out, idle 0.5 s after silence (ms)", ms);
  const int c0 = s->fx().clears;
  int clearBlocks = 0;
  while (!run(*s, 0.0f, l, r) && s->fx().clearLeft != 0 && clearBlocks < 100) clearBlocks++;
  check(s->fx().clears - c0 >= 2 && s->fx().fullClears == 0, "idle: cleared a few KB per block (blocks)", s->fx().clears - c0);
  const int p0 = s->fx().processed;
  for (int i = 0; i < 10; i++) run(*s, 0.0f, l, r);
  check(s->fx().processed == p0, "idle and clear: the effect not run", s->fx().processed - p0);

  // Switched on again in the middle of a clear: finished at once.
  s->setOn(true);
  for (int i = 0; i < 200; i++) run(*s, 0.5f, l, r);
  s->setOn(false);
  while (s->running()) run(*s, 0.0f, l, r);
  run(*s, 0.0f, l, r);  // the first clearing step
  s->setOn(true);
  const bool ran = run(*s, 0.0f, l, r);
  check(ran && s->fx().fullClears == 1 && s->fx().clearLeft == 0, "on during the clear: cleared at once, runs", s->fx().fullClears);
}

static void testDryPath() {
  std::printf("FxStage: dry path\n");
  std::unique_ptr<Stage> s(new Stage(kFs));
  s->setMix(0.0f);
  s->setOn(true);
  float l[kB], r[kB];
  for (int i = 0; i < 100; i++) run(*s, 0.25f, l, r);
  check(std::fabs(l[0] - 0.25f) < 1e-6f && std::fabs(r[kB - 1] - 0.25f) < 1e-6f, "mix 0: the input unchanged", l[0]);
}

// The reverb, 100 % wet, through its stage: a mode switch while it sounds
// fades out, goes silent while the lines clear, then the new mode plays.
static void testReverbModeSwitch() {
  std::printf("Reverb80 mode switch\n");
  using RStage = irdsp::FxStage<irdsp::Reverb80, kB, true>;
  std::vector<float> mem(irdsp::Reverb80::kMemFloats);
  std::unique_ptr<RStage> s(new RStage(kFs));
  s->fx().init(mem.data());
  s->fx().setPreDelayMs(0.0f);
  s->setMix(1.0f);
  s->setOn(true);
  float in[kB], l[kB], r[kB];
  unsigned seed = 1;
  auto noise = [&] {
    for (float& v : in) {
      seed = seed * 1664525u + 1013904223u;
      v = 0.3f * ((float)(int32_t)seed / 2147483648.0f);
    }
  };
  for (int i = 0; i < 2000; i++) {
    noise();
    s->process(in, in, l, r);
  }
  s->fx().setMode(irdsp::Reverb80::kGated);
  // Blocks until silent (the 20 ms fade), then silent blocks (the clear).
  int fadeBlocks = 0, silentBlocks = 0;
  bool silent = false;
  for (int i = 0; i < 200; i++) {
    noise();
    s->process(in, in, l, r);
    double e = 0;
    for (int k = 0; k < kB; k++) e += std::fabs(l[k]) + std::fabs(r[k]);
    if (!silent) {
      if (e == 0.0) silent = true, silentBlocks = 1;
      else fadeBlocks++;
    } else if (e == 0.0) {
      silentBlocks++;
    } else {
      break;
    }
  }
  const double fadeMs = fadeBlocks * kB * 1000.0 / kFs;
  check(fadeMs > 15.0 && fadeMs < 25.0, "hall -> gated while sounding: fades out (ms)", fadeMs);
  const double clearMs = silentBlocks * kB * 1000.0 / kFs;
  check(clearMs > 3.0 && clearMs < 20.0, "  ... then silent while the lines clear (ms)", clearMs);
  check(s->fx().mode() == irdsp::Reverb80::kGated, "  ... then the gated mode plays", s->fx().mode());

  // Idle with clean lines: a mode change applies at once.
  s->setOn(false);
  for (int i = 0; i < 3000 && s->running(); i++) {
    for (float& v : in) v = 0.0f;
    s->process(in, in, l, r);
  }
  for (int i = 0; i < 20; i++) s->process(in, in, l, r);  // the idle clear
  s->fx().setMode(irdsp::Reverb80::kHall);
  s->setOn(true);
  int first = -1;
  for (int i = 0; i < 100 && first < 0; i++) {
    noise();
    s->process(in, in, l, r);
    for (int k = 0; k < kB && first < 0; k++)
      if (l[k] != 0.0f || r[k] != 0.0f) first = i * kB + k;
  }
  const double firstMs = first * 1000.0 / kFs;
  check(first >= 0 && firstMs < 5.0, "idle and clear: gated -> hall at once (first wet, ms)", firstMs);
}

// After a clear the delay jumps to its set time, without crossfading from
// the old one.
static void testDelayJump() {
  std::printf("StudioDelay: time after a clear\n");
  const int len = 30000;
  std::vector<float> a(len), b(len);
  std::unique_ptr<irdsp::StudioDelay> d(new irdsp::StudioDelay(kFs));
  d->init(a.data(), b.data(), len);
  d->setDelaySamples(2000.0f, true);
  d->setFeedback(0.0f);
  d->setModDepth(0.0f);
  d->setDuck(0.0f);
  d->setDrive(0.0f);
  d->setEra(0.0f);
  d->setTone(1.0f);
  d->setDelaySamples(9000.0f);  // would crossfade from 2000 over 20 ms
  while (!d->clearStep(4096)) {}
  std::vector<float> x(12000, 0.0f), l(12000), r(12000);
  x[0] = 1.0f;
  for (int i = 0; i < 12000; i += kB) d->process(&x[i], &l[i], &r[i], kB);
  int peak = 0;
  for (int i = 0; i < 12000; i++)
    if (std::fabs(l[i]) > std::fabs(l[peak])) peak = i;
  double early = 0;
  for (int i = 0; i < 8000; i++) early = std::max(early, (double)std::fabs(l[i]));
  check(std::abs(peak - 9000) <= 4 && early < 1e-4, "first repeat at the new time, none at the old one (samples)", peak);
}

int main() {
  testStage();
  testDryPath();
  testReverbModeSwitch();
  testDelayJump();
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
