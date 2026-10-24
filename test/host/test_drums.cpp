// Host-side tests for the drum machine (no hardware needed).
//   make -C test/host && ./test/host/test_drums
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "DrumMachine.h"

static const float kFs = 44100.0f;
static const int kB = 32;
static int failures = 0;

static void check(bool ok, const char* what, double value) {
  std::printf("  %-52s %10.6g  %s\n", what, value, ok ? "OK" : "FAIL");
  if (!ok) failures++;
}

struct Hit { int voice, level; uint64_t t; };

struct Rig {
  std::unique_ptr<irdsp::DrumMachine> d{new irdsp::DrumMachine(kFs)};
  std::vector<Hit> hits;
  std::vector<float> out;
  Rig(const char* style, float bpm, bool countIn) {
    d->setStyle(irdsp::findDrumStyle(style));
    d->setTempo(bpm);
    d->setCountIn(countIn);
    d->setVolume(1.0f);
    d->setHitHook([](void* ctx, int v, int l, uint64_t t) { ((Rig*)ctx)->hits.push_back({v, l, t}); }, this);
  }
  void run(double seconds) {
    const int n = (int)(seconds * kFs) / kB;
    float buf[kB];
    for (int i = 0; i < n; i++) {
      if (!d->process(buf, kB)) std::memset(buf, 0, sizeof(buf));
      out.insert(out.end(), buf, buf + kB);
    }
  }
  std::vector<uint64_t> times(int voice, int minLevel = 1) const {
    std::vector<uint64_t> t;
    for (auto& h : hits)
      if (h.voice == voice && h.level >= minLevel) t.push_back(h.t);
    return t;
  }
};

static void testTiming() {
  std::printf("Timing\n");
  for (float bpm : {120.0f, 97.0f}) {
    Rig r("metronome", bpm, false);
    r.d->start();
    r.run(16 * 4 * 60.0 / bpm + 0.1);
    auto t = r.times(irdsp::kClick);
    const double beat = kFs * 60.0 / bpm;
    double worst = 0;
    for (size_t k = 0; k < t.size(); k++) worst = std::max(worst, std::fabs((double)t[k] - k * beat));
    char what[64];
    std::snprintf(what, sizeof(what), "%.0f BPM: 64 clicks on the beat grid (worst, samples)", bpm);
    check(t.size() >= 64 && worst <= 1.0, what, worst);
  }
}

static void testCountIn() {
  std::printf("Count-in\n");
  Rig r("rock", 120, true);
  r.d->start();
  r.run(2.5);
  const uint64_t bar = 88200;
  std::vector<uint64_t> clicks = r.times(irdsp::kClick), kicks = r.times(irdsp::kKick);
  bool ok = clicks.size() == 4;
  for (size_t k = 0; k < clicks.size() && k < 4; k++) ok = ok && clicks[k] == k * 22050;
  check(ok, "4 clicks on the beats of the count-in bar", clicks.size());
  check(!kicks.empty() && kicks[0] == bar, "first kick exactly one bar later (sample)", kicks.empty() ? -1 : kicks[0]);
  check(r.hits.size() > 4 && r.hits[4].t == bar, "  ... and nothing in between", r.hits.size() > 4 ? r.hits[4].t : 0);
}

static void testTrainer() {
  std::printf("Speed trainer (100 BPM, +5 every 2 bars, to 110)\n");
  Rig r("metronome", 100, false);
  r.d->setTrainer(true, 5, 2, 110);
  r.d->start();
  r.run(20.0);
  auto t = r.times(irdsp::kClick, 3);  // accents = bar starts
  const double expect[] = {100, 100, 105, 105, 110, 110, 110};
  double worst = 0;
  for (int b = 0; b < 7 && b + 1 < (int)t.size(); b++) {
    const double len = (double)(t[b + 1] - t[b]), want = 4 * 60.0 * kFs / expect[b];
    worst = std::max(worst, std::fabs(len - want));
  }
  check(t.size() >= 8 && worst <= 2.0, "bar lengths 100,100,105,105,110,110,110 BPM (worst)", worst);
  check(std::fabs(r.d->tempoNow() - 110.0f) < 1e-3, "  ... holds at the target", r.d->tempoNow());
}

static void testStyleChange() {
  std::printf("Style change and tempo change\n");
  Rig r("metronome", 120, false);
  r.d->start();
  r.run(1.5 * 88200 / kFs);  // halfway through the second bar
  r.d->setStyle(irdsp::findDrumStyle("rock"));
  r.run(3.0);
  auto clicks = r.times(irdsp::kClick), kicks = r.times(irdsp::kKick);
  check(!kicks.empty() && kicks[0] == 2 * 88200, "switches at the next bar (first kick, sample)", kicks.empty() ? -1 : kicks[0]);
  check(!clicks.empty() && clicks.back() == 2 * 88200 - 22050, "  ... metronome played to the end of its bar", clicks.back());

  Rig q("metronome", 120, false);
  q.d->start();
  q.run(0.6);  // 26432 samples: past beat 2 (22050), before beat 3 (44100)
  q.d->setTempo(60);
  q.run(4.0);
  // The next 16th (27563) still comes at 120 BPM; from there steps are
  // 11025 samples apart, so beat 3 (3 steps later) lands at 60638.
  auto t = q.times(irdsp::kClick);
  check(t.size() > 4 && std::llabs((long long)t[2] - 60638) <= 1 && std::llabs((long long)(t[3] - t[2]) - 44100) <= 1,
        "tempo 120 -> 60 from the next step (beat 3, sample)", t.size() > 3 ? (double)t[2] : -1);
}

static void testShuffle() {
  std::printf("Shuffle\n");
  Rig r("shuffle", 120, false);
  r.d->start();
  r.run(4.0);
  auto h = r.times(irdsp::kHatClosed);
  bool ok = !h.empty();
  for (auto t : h) {
    const uint64_t m = t % 22050;
    ok = ok && (m == 0 || m == 14700);  // on the beat, or 2/3 of the way to the next
  }
  check(ok, "hats on the triplet grid (beat and 2/3 beat)", h.size());
}

// The practice beats: kick on 1 and 3, snare on 2 and 4, and the hat on
// exactly the given offsets within each beat (never on the beat itself).
static void testPracticeBeats() {
  std::printf("Practice beats (120 BPM, beat = 22050 samples)\n");
  struct Case { const char* style; std::vector<uint64_t> hatOffsets; const char* what; };
  const Case cases[] = {
      {"offbeat8", {11025}, "offbeat 8ths: hat only on the '&'"},
      {"offbeat16", {5513, 11025, 16538}, "offbeat 16ths: hat on 'e & a', not the beat"},
      {"triplets", {7350, 14700}, "triplets: kick/snare, hat, hat"},
  };
  for (const Case& c : cases) {
    Rig r(c.style, 120, false);
    r.d->start();
    r.run(3.99);  // just under two bars (2 x 88200 samples)
    bool ok = true;
    std::vector<uint64_t> seen;
    for (auto t : r.times(irdsp::kHatClosed)) {
      const uint64_t m = t % 22050;
      bool expected = false;
      for (uint64_t o : c.hatOffsets) expected = expected || (m + 1 >= o && m <= o + 1);  // +-1 sample
      ok = ok && expected;
      seen.push_back(m);
    }
    const auto kicks = r.times(irdsp::kKick), snares = r.times(irdsp::kSnare);
    for (auto t : kicks) ok = ok && (t % 44100 == 0);              // beats 1 and 3
    for (auto t : snares) ok = ok && (t % 44100 == 22050);         // beats 2 and 4
    ok = ok && seen.size() == 8 * c.hatOffsets.size() && kicks.size() == 4 && snares.size() == 4;
    check(ok, c.what, seen.size());
  }
}

static void testStop() {
  std::printf("Stop\n");
  Rig r("rock", 120, false);
  r.d->start();
  r.run(1.0);
  r.d->stop();
  const size_t at = r.out.size();
  r.run(3.0);
  const size_t hitsAtStop = r.hits.size();
  double tail = 0;
  for (size_t i = r.out.size() - 22050; i < r.out.size(); i++) tail = std::max(tail, (double)std::fabs(r.out[i]));
  check(tail == 0.0 && !r.d->active() && !r.d->playing(), "3 s after stop: silent and idle", tail);
  check(r.hits.size() == hitsAtStop && r.hits.back().t < at, "no hits after stop", r.hits.size());
}

static void testStyles() {
  std::printf("Styles\n");
  bool lanesOk = true;
  double worstPeak = 0, quietest = 1;
  for (int s = 0; s < irdsp::drumStyleCount(); s++) {
    const irdsp::DrumStyle& st = irdsp::drumStyle(s);
    const int steps = st.beats * st.stepsPerBeat;
    for (int v = 0; v < irdsp::kDrumVoices; v++) {
      const int len = (int)std::strlen(st.lanes[v]);
      if (len != 0 && len != steps) {
        std::printf("    %s lane %d: %d steps, want %d\n", st.name, v, len, steps);
        lanesOk = false;
      }
      for (int i = 0; i < len; i++) lanesOk = lanesOk && std::strchr(".gxX", st.lanes[v][i]);
    }
    lanesOk = lanesOk && irdsp::findDrumStyle(st.name) == s;
    Rig r(st.name, 120, true);
    r.d->start();
    r.run(10.0);
    double pk = 0;
    for (float x : r.out) pk = std::max(pk, (double)std::fabs(x));
    worstPeak = std::max(worstPeak, pk);
    quietest = std::min(quietest, pk);
  }
  check(lanesOk, "every lane fits its style and uses . g x X only", lanesOk);
  check(worstPeak <= 1.0, "loudest style at volume 100 stays <= full scale", worstPeak);
  check(quietest > 0.15, "quietest style is still clearly audible (peak)", quietest);
  check(irdsp::findDrumStyle("SHUFFLE") == 6 && irdsp::findDrumStyle("2") == 1 && irdsp::findDrumStyle("polka") == -1,
        "find by name (any case) or number", 0);
}

int main() {
  testTiming();
  testCountIn();
  testTrainer();
  testStyleChange();
  testShuffle();
  testPracticeBeats();
  testStop();
  testStyles();
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
