// Host-side tests for the drum machine (no hardware needed).
//   make -C test/host && ./test/host/test_drums
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "DrumKit.h"
#include "DrumMachine.h"

static const float kFs = 44100.0f;
static const int kB = 32;
static int failures = 0;

static void check(bool ok, const char* what, double value) {
  std::printf("  %-52s %10.6g  %s\n", what, value, ok ? "OK" : "FAIL");
  if (!ok) failures++;
}

struct Hit { int voice, vel; uint64_t t; };

struct Rig {
  std::unique_ptr<irdsp::DrumMachine> d{new irdsp::DrumMachine(kFs)};
  std::vector<Hit> hits;
  std::vector<float> out, outR;  // left (out) and right channels
  Rig(const char* style, float bpm, bool countIn, const irdsp::DrumKit* kit = nullptr) {
    d->setKit(kit);
    d->setHumanize(0.0f);       // exact velocities; the humanize tests turn it on
    d->setBusDynamics(false);   // exact levels; the loudness tests turn it on
    d->setStyle(irdsp::findDrumStyle(style));
    d->setTempo(bpm);
    d->setCountIn(countIn ? 1 : 0);
    d->setVolume(1.0f);
    d->setHitHook([](void* ctx, int v, int l, uint64_t t) { ((Rig*)ctx)->hits.push_back({v, l, t}); }, this);
  }
  int maxVoices = 0;  // most sample voices sounding after a block
  int skip = irdsp::DrumMachine::kLatency;  // the bus delay, dropped so out[t] is hit time t
  void run(double seconds) {
    const int n = (int)(seconds * kFs) / kB;
    float buf[kB], bufR[kB];
    for (int i = 0; i < n; i++) {
      if (!d->process(buf, bufR, kB)) {
        std::memset(buf, 0, sizeof(buf));
        std::memset(bufR, 0, sizeof(bufR));
      }
      const int from = std::min(skip, kB);
      skip -= from;
      out.insert(out.end(), buf + from, buf + kB);
      outR.insert(outR.end(), bufR + from, bufR + kB);
      maxVoices = std::max(maxVoices, d->samplesPlaying());
    }
  }
  void runTo(uint64_t sample) {
    if (sample > out.size()) run((double)(sample - out.size() + kB - 1) / kFs);
  }
  std::vector<uint64_t> times(int voice, int minVel = 1) const {
    std::vector<uint64_t> t;
    for (auto& h : hits)
      if (h.voice == voice && h.vel >= minVel) t.push_back(h.t);
    return t;
  }
  std::vector<int> vels(int voice) const {
    std::vector<int> v;
    for (auto& h : hits)
      if (h.voice == voice) v.push_back(h.vel);
    return v;
  }
  double peak() const {
    double p = 0;
    for (size_t i = 0; i < out.size(); i++) p = std::max(p, (double)std::max(std::fabs(out[i]), std::fabs(outR[i])));
    return p;
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

// Two count-in bars; with the trainer, two more at each new tempo.
static void testCountInBars() {
  std::printf("Count-in bars and trainer count-ins\n");
  {
    Rig r("rock", 120, false);
    r.d->setCountIn(2);
    r.d->start();
    r.run(4.5);
    const auto clicks = r.times(irdsp::kClick), kicks = r.times(irdsp::kKick);
    check(clicks.size() == 8 && clicks[7] == 7 * 22050 && !kicks.empty() && kicks[0] == 2 * 88200,
          "2 bars: 8 clicks, then the groove at bar 3 (first kick)", clicks.size());
  }
  // 100 BPM, +10 every 2 bars to 120, 2-bar count-ins, crash every 4.
  Rig r("rock", 100, false);
  r.d->setCountIn(2);
  r.d->setTrainer(true, 10, 2, 120);
  r.d->setCrashEvery(4);
  r.d->start();
  r.run(30.0);
  auto bar = [](double bpm) { return 4 * 60 * 44100.0 / bpm; };
  const double g1 = 2 * bar(100), g2 = 4 * bar(100) + 2 * bar(110), g3 = 4 * bar(100) + 4 * bar(110) + 2 * bar(120);
  // The groove's first kick after each count-in, and no count-in once at the target.
  bool resume = true;
  for (double g : {g1, g2, g3}) {
    bool found = false;
    for (auto t : r.times(irdsp::kKick)) found = found || std::fabs((double)t - g) <= 2.0;
    resume = resume && found;
  }
  const auto accents = r.times(irdsp::kClick, 9);  // count-in bar starts
  bool spacing = accents.size() == 6;
  const double want[] = {0, bar(100), 4 * bar(100), 4 * bar(100) + bar(110), g2 + 2 * bar(110), g2 + 2 * bar(110) + bar(120)};
  for (size_t i = 0; spacing && i < 6; i++) spacing = std::fabs((double)accents[i] - want[i]) <= 2.0;
  check(spacing, "trainer: 2 count-in bars at each new tempo (110, 120), none after", accents.size());
  check(resume, "  ... the groove resumes right after each count-in", resume);
  const auto crashes = r.times(irdsp::kCrash);
  bool crashOk = crashes.size() >= 3;
  for (size_t i = 0; crashOk && i < 3; i++) crashOk = std::fabs((double)crashes[i] - (i == 0 ? g1 : i == 1 ? g2 : g3)) <= 2.0;
  check(crashOk, "  ... crash accents restart after each count-in", crashes.size());
  const int grooveBars = 4 + (int)(((double)r.out.size() - g3) / bar(120));
  check(r.d->bar() == grooveBars, "  ... bar() counts groove bars only", r.d->bar());
}

static void testTrainer() {
  std::printf("Speed trainer (100 BPM, +5 every 2 bars, to 110)\n");
  Rig r("metronome", 100, false);
  r.d->setTrainer(true, 5, 2, 110);
  r.d->start();
  r.run(20.0);
  auto t = r.times(irdsp::kClick, 9);  // accents = bar starts
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
  for (size_t i = r.out.size() - 22050; i < r.out.size(); i++)
    tail = std::max(tail, (double)std::max(std::fabs(r.out[i]), std::fabs(r.outR[i])));
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
      for (int i = 0; i < len; i++) lanesOk = lanesOk && std::strchr(".123456789gxX", st.lanes[v][i]);
    }
    lanesOk = lanesOk && irdsp::findDrumStyle(st.name) == s;
    Rig r(st.name, 120, true);
    r.d->setBusDynamics(true);
    r.d->start();
    r.run(10.0);
    const double pk = r.peak();
    worstPeak = std::max(worstPeak, pk);
    quietest = std::min(quietest, pk);
  }
  check(lanesOk, "every lane fits its style and uses . 1-9 g x X only", lanesOk);
  check(worstPeak <= 1.0, "loudest style at volume 100 stays <= full scale", worstPeak);
  check(quietest > 0.15, "quietest style is still clearly audible (peak)", quietest);
  check(irdsp::findDrumStyle("SHUFFLE") == 6 && irdsp::findDrumStyle("2") == 1 && irdsp::findDrumStyle("polka") == -1,
        "find by name (any case) or number", 0);
}

// ---- Sampled kits -------------------------------------------------------------------

// A kit built in the test: per lane, samples with their velocities.
struct TestKit {
  struct Layer { int vel; std::vector<int16_t> pcm; };
  std::vector<Layer> lanes[irdsp::kDrumVoices];
  std::vector<irdsp::DrumSample> samples;
  irdsp::DrumKit kit{"test", "test", {}, {}, nullptr};
  // Every lane but the click: a one-sample impulse of the given value, at
  // velocity 9.
  explicit TestKit(int16_t impulse) {
    for (int v = 0; v < irdsp::kDrumVoices; v++)
      if (v != irdsp::kClick) lanes[v] = {{9, {impulse}}};
  }
  TestKit& set(int lane, std::vector<int16_t> x) { lanes[lane] = {{9, std::move(x)}}; return *this; }
  TestKit& layers(int lane, std::vector<Layer> l) { lanes[lane] = std::move(l); return *this; }
  static std::vector<int16_t> dc(int16_t v, int n) { return std::vector<int16_t>(n, v); }
  const irdsp::DrumKit* build() {
    samples.clear();
    for (int v = 0; v < irdsp::kDrumVoices; v++) {
      kit.first[v] = (uint8_t)samples.size();
      kit.count[v] = (uint8_t)lanes[v].size();
      for (auto& l : lanes[v]) samples.push_back({l.pcm.data(), (uint32_t)l.pcm.size(), (uint8_t)l.vel});
    }
    kit.samples = samples.data();
    return &kit;
  }
};

static void testSampledTiming() {
  std::printf("Sampled kit: timing and layers\n");
  for (float bpm : {120.0f, 97.0f}) {
    TestKit k(8192);
    Rig r("rock", bpm, false, k.build());
    r.d->start();
    r.run(16 * 4 * 60.0 / bpm);
    // Each hit is one non-zero output sample, at exactly its hit time.
    std::vector<uint64_t> want, got;
    for (auto& h : r.hits) if (want.empty() || want.back() != h.t) want.push_back(h.t);
    for (size_t i = 0; i < r.out.size(); i++) if (r.out[i] != 0.0f) got.push_back(i);
    char what[64];
    std::snprintf(what, sizeof(what), "%.0f BPM: every sample onset on its step (16 bars)", bpm);
    check(got == want && want.size() > 100, what, (double)got.size());
  }
  // Closed hat with 4 layers (velocities 3, 5, 7, 9): each hit plays the
  // nearest (the softer on a tie), scaled by hit / layer velocity.
  TestKit k(0);
  k.layers(irdsp::kHatClosed, {{3, {1000}}, {5, {2000}}, {7, {3000}}, {9, {4000}}});
  Rig r("rock", 120, false, k.build());
  r.d->start();
  r.run(2.0);
  bool ok = !r.hits.empty();
  for (auto& h : r.hits) {
    if (h.voice != irdsp::kHatClosed) continue;
    const int layer = h.vel <= 4 ? 3 : (h.vel <= 6 ? 5 : (h.vel <= 8 ? 7 : 9));
    const double imp = (layer - 1) / 2 * 1000.0 / 32768.0;  // that layer's impulse
    const double expect = 1.1 * imp * h.vel / layer;  // bus gain 1.1
    ok = ok && std::fabs(r.out[h.t] - expect) < 0.01 * expect;
  }
  check(ok, "hat layers: nearest velocity, scaled by hit/layer", r.vels(irdsp::kHatClosed).size());
}

static void testSampledCuts() {
  std::printf("Sampled kit: retrigger, choke, cymbals, kit change, idle\n");
  {
    // Snare (2 s of DC) re-struck at beat 4: the old hit is gone 2 ms later.
    TestKit k(0);
    k.set(irdsp::kSnare, TestKit::dc(8192, 88200));
    Rig r("rock", 120, false, k.build());
    r.d->start();
    r.runTo(66150 + 16);
    const int during = r.d->samplesPlaying();
    r.runTo(66150 + 140);
    check(during == 2 && r.d->samplesPlaying() == 1, "re-struck snare: old hit fades out within 2 ms", during);
  }
  {
    // Pop: open hat on step 14, closed hat on the next bar's 1 chokes it.
    TestKit k(0);
    k.set(irdsp::kHatOpen, TestKit::dc(8192, 88200));
    Rig r("pop", 120, false, k.build());
    r.d->start();
    r.run(2.2);
    double step = 0;
    for (size_t i = 88200; i < 88200 + 300; i++) step = std::max(step, (double)std::fabs(r.out[i] - r.out[i - 1]));
    const float before = r.out[88199], after = r.out[88200 + 221];
    check(before > 0.1f && after == 0.0f, "closed hat chokes the open hat within 5 ms", before);
    check(step < 1.5 * before / 220.0, "  ... as a smooth fade (largest step per sample)", step);
  }
  {
    // Crash every 2 bars, 10 s long: at most 2 ring at once (+1 fading).
    TestKit k(0);
    k.set(irdsp::kCrash, TestKit::dc(100, 441000));
    Rig r("rock", 240, false, k.build());
    r.d->setCrashEvery(2);
    r.d->start();
    r.runTo(4 * 44100 + 16);
    const int fading = r.d->samplesPlaying();
    r.run(0.1);
    check(fading == 3 && r.d->samplesPlaying() == 2 && r.maxVoices == 3,
          "cymbals: 2 ring on, the oldest fades on a 3rd hit", r.maxVoices);
  }
  {
    // Changing the kit mid-hit fades its samples out.
    TestKit k(0);
    k.set(irdsp::kHatOpen, TestKit::dc(8192, 88200));
    Rig r("pop", 120, false, k.build());
    r.d->start();
    r.runTo(80000);
    r.d->setKit(&irdsp::drumKit(0));
    r.run(0.001);
    const int fading = r.d->samplesPlaying();
    r.run(0.006);
    check(fading == 1 && r.d->samplesPlaying() == 0, "kit change: old samples fade out within 5 ms", fading);
  }
  {
    // Idle once the last sample has played out (a 3 s crash).
    TestKit k(0);
    k.set(irdsp::kCrash, TestKit::dc(100, 3 * 44100));
    Rig r("rock", 120, false, k.build());
    r.d->setCrashEvery(4);
    r.d->start();
    r.run(0.5);
    r.d->stop();
    r.run(2.4);
    const bool ringing = r.d->active();
    r.run(0.2);
    check(ringing && !r.d->active(), "stopped: idle once the crash sample has ended", ringing);
  }
}

// ---- Crash accents ------------------------------------------------------------------

static std::vector<uint64_t> crashBars(const Rig& r, double barLen) {
  std::vector<uint64_t> b;
  for (auto t : r.times(irdsp::kCrash)) b.push_back((uint64_t)std::llround(t / barLen));
  return b;
}

static void testCrashAccents() {
  std::printf("Crash accents\n");
  const double bar = 88200;  // 4/4 at 120 BPM
  {
    Rig r("rock", 120, false);
    r.d->setCrashEvery(4);
    r.d->start();
    r.run(9.5 * bar / kFs);
    bool onBeat = true, noHat = true, kick = true;
    for (auto t : r.times(irdsp::kCrash)) {
      onBeat = onBeat && (uint64_t)std::llround(t / bar) * (uint64_t)bar == t;
      for (auto h : r.times(irdsp::kHatClosed)) noHat = noHat && h != t;
      bool k = false;
      for (auto h : r.times(irdsp::kKick)) k = k || h == t;
      kick = kick && k;
    }
    check(crashBars(r, bar) == std::vector<uint64_t>({0, 4, 8}) && onBeat, "every 4: crash on beat 1 of bars 1, 5, 9", r.times(irdsp::kCrash).size());
    check(noHat && kick, "  ... in place of the hat; the kick stays", noHat);
  }
  {
    Rig a("rock", 120, false), b("rock", 240, false), c("rock", 120, false);
    a.d->setCrashEvery(2);
    b.d->setCrashEvery(16);
    for (Rig* r : {&a, &b, &c}) r->d->start();
    a.run(6.5 * bar / kFs);
    b.run(17.5 * 44100 / kFs);
    c.run(9.5 * bar / kFs);
    check(crashBars(a, bar) == std::vector<uint64_t>({0, 2, 4, 6}), "every 2: bars 1, 3, 5, 7", crashBars(a, bar).size());
    check(crashBars(b, 44100) == std::vector<uint64_t>({0, 16}), "every 16: bars 1 and 17", crashBars(b, 44100).size());
    check(c.times(irdsp::kCrash).empty(), "off: no crash", c.times(irdsp::kCrash).size());
  }
  {
    Rig r("rock", 120, true), m("metronome", 120, false);
    r.d->setCrashEvery(4);
    m.d->setCrashEvery(4);
    r.d->start();
    m.d->start();
    r.run(2.5 * bar / kFs);
    m.run(4.5 * bar / kFs);
    const auto t = r.times(irdsp::kCrash);
    check(t.size() == 1 && t[0] == (uint64_t)bar, "count-in: first crash on the first pattern bar", t.empty() ? -1 : (double)t[0]);
    check(m.times(irdsp::kCrash).empty(), "no crash in the metronome style", m.times(irdsp::kCrash).size());
  }
  {
    Rig r("rock", 120, false);
    r.d->start();
    r.run(1.5 * bar / kFs);
    r.d->setCrashEvery(2);
    r.run(3.0 * bar / kFs);
    check(crashBars(r, bar) == std::vector<uint64_t>({2, 4}), "turned on mid-bar: from the next phrase start", crashBars(r, bar).size());
  }
  {
    // Synthesized crash: audible on bar 1, then decays to idle.
    Rig on("rock", 120, false), off("rock", 120, false);
    on.d->setCrashEvery(4);
    for (Rig* r : {&on, &off}) {
      r->d->start();
      r->run(0.25);
      r->d->stop();
      r->run(3.0);
    }
    double e1 = 0, e0 = 0;
    for (size_t i = 11025; i < 44100; i++) e1 += on.out[i] * on.out[i], e0 += off.out[i] * off.out[i];
    check(e1 > 4 * e0 && !on.d->active(), "synth crash: rings after the hits, then idle", e1 / (e0 + 1e-12));
  }
}

// ---- The real kits (kits/*/, generated into DrumKitData.cpp) ----------------------------

static void testRealKits() {
  std::printf("Real kits\n");
  bool ok = irdsp::drumKitCount() >= 2 && irdsp::drumKit(0).samples == nullptr;
  for (int k = 1; k < irdsp::drumKitCount(); k++) {
    const irdsp::DrumKit& kit = irdsp::drumKit(k);
    for (int v = 0; v < irdsp::kDrumVoices; v++) {
      for (int j = 0; j < kit.count[v]; j++) {
        const irdsp::DrumSample& s = kit.samples[kit.first[v] + j];
        int peak = 0;
        for (uint32_t i = 0; i < s.len; i++) peak = std::max(peak, std::abs((int)s.data[i]));
        uint32_t onset = 0;
        while (onset < s.len && std::abs((int)s.data[onset]) < peak / 10) onset++;
        const bool good = s.len > 100 && s.data[0] == 0 && s.data[s.len - 1] == 0 && onset < 88 && v != irdsp::kClick;
        if (!good) std::printf("    %s lane %d sample %d: len %u, onset %u\n", kit.name, v, j, s.len, onset);
        ok = ok && good;
      }
    }
  }
  check(ok, "samples start and end at 0, onset within 2 ms", irdsp::drumKitCount());

  // Layers: 2 kick, 3 snare, 4 closed hat; within a lane the peaks follow
  // the velocities (peak / vel equal within 0.5 dB).
  bool layers = true, levels = true;
  for (int k = 1; k < irdsp::drumKitCount(); k++) {
    const irdsp::DrumKit& kit = irdsp::drumKit(k);
    layers = layers && kit.count[irdsp::kKick] == 2 && kit.count[irdsp::kSnare] == 3 && kit.count[irdsp::kHatClosed] == 4;
    for (int v = 0; v < irdsp::kDrumVoices; v++) {
      double lo = 1e9, hi = 0;
      for (int j = 0; j < kit.count[v]; j++) {
        const irdsp::DrumSample& smp = kit.samples[kit.first[v] + j];
        int pk = 0;
        for (uint32_t i = 0; i < smp.len; i++) pk = std::max(pk, std::abs((int)smp.data[i]));
        lo = std::min(lo, (double)pk / smp.vel), hi = std::max(hi, (double)pk / smp.vel);
      }
      if (kit.count[v]) levels = levels && 20 * std::log10(hi / lo) < 0.5;
    }
  }
  bool toms = true;
  for (int k = 1; k < irdsp::drumKitCount(); k++)
    toms = toms && irdsp::drumKit(k).count[irdsp::kTomHi] == 1 && irdsp::drumKit(k).count[irdsp::kTomLo] == 1;
  check(layers, "velocity layers: 2 kick, 3 snare, 4 closed hat", layers);
  check(toms, "every recorded kit has a rack and a floor tom", toms);
  check(levels, "  ... each layer's peak follows its velocity", levels);

  // Hat balance in rock: the hats sit well under the kick and snare.
  static const int16_t silent[1] = {0};
  for (int k = 1; k < irdsp::drumKitCount(); k++) {
    auto energy = [&](std::initializer_list<int> keep) {
      irdsp::DrumKit kit = irdsp::drumKit(k);
      int n = 0;
      for (int v = 0; v < irdsp::kDrumVoices; v++) n = std::max(n, kit.first[v] + kit.count[v]);
      std::vector<irdsp::DrumSample> smp(kit.samples, kit.samples + n);
      for (int v = 0; v < irdsp::kDrumVoices; v++) {
        if (v == irdsp::kClick || std::find(keep.begin(), keep.end(), v) != keep.end()) continue;
        for (int j = 0; j < kit.count[v]; j++) smp[kit.first[v] + j] = {silent, 1, 9};
      }
      kit.samples = smp.data();
      Rig r("rock", 100, false, &kit);
      r.d->start();
      r.run(8 * 2.4);
      double e = 0;
      for (size_t i = 0; i < r.out.size(); i++) e += r.out[i] * r.out[i] + r.outR[i] * r.outR[i];
      return e;
    };
    const double db = 10 * std::log10(energy({irdsp::kHatClosed, irdsp::kHatOpen}) /
                                      energy({irdsp::kKick, irdsp::kSnare}));
    char what[64];
    std::snprintf(what, sizeof(what), "%s: hats vs kick + snare in rock (dB)", irdsp::drumKit(k).name);
    check(db < -6.0, what, db);
  }
  check(irdsp::findDrumKit("ROCK") == 1 && irdsp::findDrumKit("pearl") == 2 && irdsp::findDrumKit("1") == 0 &&
            irdsp::findDrumKit("jazz") == -1,
        "find a kit by name (any case) or number", 0);

}

// The drum bus (compressor + limiter) at volume 100: every style with each
// kit, crash every 4, sits at a level that holds up next to a guitar, with
// peaks at the limiter's ceiling and some punch left.
static void testBusLoudness() {
  std::printf("Drum bus loudness (volume 100)\n");
  for (int k = 0; k < irdsp::drumKitCount(); k++) {
    double lo = 0, hi = -100, peak = 0, crest = 100;
    for (int s = 1; s < irdsp::drumStyleCount(); s++) {
      Rig r(irdsp::drumStyle(s).name, 120, false, &irdsp::drumKit(k));
      r.d->setBusDynamics(true);
      r.d->setCrashEvery(4);
      r.d->start();
      r.run(12.0);
      double e = 0;
      for (size_t i = 0; i < r.out.size(); i++) e += r.out[i] * r.out[i] + r.outR[i] * r.outR[i];
      const double rms = 10 * std::log10(e / (2.0 * (double)r.out.size()));
      lo = std::min(lo, rms), hi = std::max(hi, rms);
      peak = std::max(peak, r.peak());
      crest = std::min(crest, 20 * std::log10(r.peak()) - rms);
    }
    const bool sampled = irdsp::drumKit(k).samples != nullptr;
    const double minDb = sampled ? -18.5 : -21.0, maxDb = sampled ? -13.0 : -15.0;
    char what[80];
    std::snprintf(what, sizeof(what), "%s kit: RMS of every style %.1f..%.1f dBFS", irdsp::drumKit(k).name, lo, hi);
    check(lo >= minDb && hi <= maxDb, what, hi);
    check(peak <= 0.945, "  ... peaks at the limiter's ceiling (-0.5 dBFS)", peak);
    check(crest >= 12.0, "  ... crest factor >= 12 dB (still punchy)", crest);
  }
  // Stopping: silence is exact and the machine goes idle (compressor reset).
  Rig r("rock", 120, false, &irdsp::drumKit(1));
  r.d->setBusDynamics(true);
  r.d->start();
  r.run(1.0);
  r.d->stop();
  r.run(4.0);
  double tail = 0;
  for (size_t i = r.out.size() - 22050; i < r.out.size(); i++)
    tail = std::max(tail, (double)std::max(std::fabs(r.out[i]), std::fabs(r.outR[i])));
  check(tail == 0.0 && !r.d->active(), "after stop: exact silence and idle", tail);
}

// ---- Dynamics, humanize and panning ------------------------------------------------

static void testDynamics() {
  std::printf("Dynamics, humanize and panning\n");
  {
    Rig r("rock", 120, false);
    r.d->start();
    r.run(0.99 * 88200 / kFs);
    check(r.vels(irdsp::kHatClosed) == std::vector<int>({9, 6, 5, 6, 8, 6, 5, 6}),
          "rock hats: 1 and 3 strong, '&' medium, soft under the snare", 0);
    check(r.vels(irdsp::kKick) == std::vector<int>({9, 8, 7}) && r.vels(irdsp::kSnare) == std::vector<int>({8, 9}),
          "  ... kick 9/8/7, snare 8/9", 0);
  }
  {
    // Humanize: the spread of single-hit amplitudes (relative to their
    // velocity), kick vs closed hat. Triangular +-8 % has a standard
    // deviation of 3.3 %, +-15 % of 6.1 %.
    auto spread = [](int lane, float humanize) {
      TestKit k(0);
      k.set(lane, {1000});
      Rig r("rock", 240, false, k.build());
      r.d->setHumanize(humanize);
      r.d->start();
      r.run(60.0);
      std::vector<double> x;
      for (auto& h : r.hits)
        if (h.voice == lane) x.push_back(r.out[h.t] / h.vel);
      double m = 0, v = 0;
      for (double a : x) m += a;
      m /= (double)x.size();
      for (double a : x) v += (a - m) * (a - m);
      return std::sqrt(v / (double)x.size()) / m;
    };
    const double kick = spread(irdsp::kKick, 1.0f), hat = spread(irdsp::kHatClosed, 1.0f);
    check(kick > 0.025 && kick < 0.042, "humanize: kick velocity spread (std, +-8 % triangular)", kick);
    check(hat > 0.05 && hat < 0.075, "  ... hat spread (std, +-15 %), larger than the kick's", hat);
    check(spread(irdsp::kKick, 0.0f) < 1e-4, "  ... none at humanize 0", spread(irdsp::kKick, 0.0f));
  }
  {
    // Pan, drummer's view: hats left (R = 0.65 L), crash left (R = 0.7 L),
    // kick centered.
    TestKit hat(0), kick(0), crash(0);
    hat.set(irdsp::kHatClosed, {1000});
    kick.set(irdsp::kKick, {1000});
    crash.set(irdsp::kCrash, {1000});
    Rig rh("rock", 120, false, hat.build()), rk("rock", 120, false, kick.build()), rc("rock", 120, false, crash.build());
    rc.d->setCrashEvery(2);
    for (Rig* r : {&rh, &rk, &rc}) {
      r->d->start();
      r->run(2.0);
    }
    const uint64_t th = rh.times(irdsp::kHatClosed)[1], tk = rk.times(irdsp::kKick)[0], tc = rc.times(irdsp::kCrash)[0];
    const double h = rh.outR[th] / rh.out[th], k = rk.outR[tk] / rk.out[tk], c = rc.outR[tc] / rc.out[tc];
    check(std::fabs(h - 0.65) < 0.005 && std::fabs(k - 1.0) < 1e-6 && std::fabs(c - 0.7) < 0.005,
          "pan: hat and crash left (R/L 0.65, 0.7), kick centered", h);
  }
}

int main() {
  testTiming();
  testCountIn();
  testTrainer();
  testCountInBars();
  testStyleChange();
  testShuffle();
  testPracticeBeats();
  testStop();
  testStyles();
  testSampledTiming();
  testSampledCuts();
  testCrashAccents();
  testDynamics();
  testRealKits();
  testBusLoudness();
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
