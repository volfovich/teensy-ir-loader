// Host-side tests for the drum machine (no hardware needed).
//   make -C test/host && ./test/host/test_drums
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

struct Hit { int voice, level; uint64_t t; };

struct Rig {
  std::unique_ptr<irdsp::DrumMachine> d{new irdsp::DrumMachine(kFs)};
  std::vector<Hit> hits;
  std::vector<float> out;
  Rig(const char* style, float bpm, bool countIn, const irdsp::DrumKit* kit = nullptr) {
    d->setKit(kit);
    d->setStyle(irdsp::findDrumStyle(style));
    d->setTempo(bpm);
    d->setCountIn(countIn);
    d->setVolume(1.0f);
    d->setHitHook([](void* ctx, int v, int l, uint64_t t) { ((Rig*)ctx)->hits.push_back({v, l, t}); }, this);
  }
  int maxVoices = 0;  // most sample voices sounding after a block
  void run(double seconds) {
    const int n = (int)(seconds * kFs) / kB;
    float buf[kB];
    for (int i = 0; i < n; i++) {
      if (!d->process(buf, kB)) std::memset(buf, 0, sizeof(buf));
      out.insert(out.end(), buf, buf + kB);
      maxVoices = std::max(maxVoices, d->samplesPlaying());
    }
  }
  void runTo(uint64_t sample) {
    if (sample > out.size()) run((double)(sample - out.size() + kB - 1) / kFs);
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

// ---- Sampled kits -------------------------------------------------------------------

// A kit built in the test: per lane, up to two samples (normal, accent).
struct TestKit {
  std::vector<int16_t> pcm[irdsp::kDrumVoices][2];
  std::vector<irdsp::DrumSample> samples;
  irdsp::DrumKit kit{"test", "test", {}, {}, nullptr};
  // Every lane but the click: a one-sample impulse of the given value.
  explicit TestKit(int16_t impulse) {
    for (int v = 0; v < irdsp::kDrumVoices; v++)
      if (v != irdsp::kClick) pcm[v][0] = {impulse};
  }
  TestKit& set(int lane, int layer, std::vector<int16_t> x) { pcm[lane][layer] = std::move(x); return *this; }
  static std::vector<int16_t> dc(int16_t v, int n) { return std::vector<int16_t>(n, v); }
  const irdsp::DrumKit* build() {
    samples.clear();
    for (int v = 0; v < irdsp::kDrumVoices; v++) {
      kit.first[v] = (uint8_t)samples.size();
      kit.count[v] = 0;
      for (int l = 0; l < 2; l++) {
        if (pcm[v][l].empty()) continue;
        samples.push_back({pcm[v][l].data(), (uint32_t)pcm[v][l].size(), (uint8_t)(2 + l)});
        kit.count[v]++;
      }
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
  // Closed hat: 'x' plays the normal layer, 'X' the accent layer.
  TestKit k(8192);
  k.set(irdsp::kHatClosed, 0, {4096}).set(irdsp::kHatClosed, 1, {12288});
  Rig r("offbeat16", 120, false, k.build());
  r.d->start();
  r.run(2.0);
  double lo = 1, hi = 0;
  for (auto t : r.times(irdsp::kHatClosed)) {
    const double v = r.out[t];
    if (t % 22050 == 11025) hi = std::max(hi, v), lo = std::min(lo, v / 3.0);  // 'X' on the '&'
  }
  double x = 0;
  for (auto t : r.times(irdsp::kHatClosed)) if (t % 22050 != 11025) x = std::max(x, (double)r.out[t]);
  check(hi > 0 && x > 0 && hi / x > 2.6 && hi / x < 3.4, "hat 'X' plays the accent layer (3x louder)", hi / x);
}

static void testSampledCuts() {
  std::printf("Sampled kit: retrigger, choke, cymbals, kit change, idle\n");
  {
    // Snare (2 s of DC) re-struck at beat 4: the old hit is gone 2 ms later.
    TestKit k(0);
    k.set(irdsp::kSnare, 0, TestKit::dc(8192, 88200));
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
    k.set(irdsp::kHatOpen, 0, TestKit::dc(8192, 88200));
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
    k.set(irdsp::kCrash, 0, TestKit::dc(100, 441000));
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
    k.set(irdsp::kHatOpen, 0, TestKit::dc(8192, 88200));
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
    k.set(irdsp::kCrash, 0, TestKit::dc(100, 3 * 44100));
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
  check(irdsp::findDrumKit("ROCK") == 1 && irdsp::findDrumKit("pearl") == 2 && irdsp::findDrumKit("1") == 0 &&
            irdsp::findDrumKit("jazz") == -1,
        "find a kit by name (any case) or number", 0);

  // Every style with the crash every 2 bars, at volume 100: below the clip
  // knee, and about as loud as the synth kit.
  std::vector<double> energy(irdsp::drumKitCount(), 0.0), peak(irdsp::drumKitCount(), 0.0);
  for (int s = 1; s < irdsp::drumStyleCount(); s++) {
    for (int k = 0; k < irdsp::drumKitCount(); k++) {
      Rig r(irdsp::drumStyle(s).name, 120, false, &irdsp::drumKit(k));
      r.d->setCrashEvery(2);
      r.d->start();
      r.run(8.0);
      for (float x : r.out) energy[k] += x * x, peak[k] = std::max(peak[k], (double)std::fabs(x));
    }
  }
  for (int k = 1; k < irdsp::drumKitCount(); k++) {
    const double ratioDb = 10 * std::log10(energy[k] / energy[0]);
    char what[64];
    std::snprintf(what, sizeof(what), "%s kit, every style, volume 100: below clipping", irdsp::drumKit(k).name);
    check(peak[k] < 0.95, what, peak[k]);
    check(std::fabs(ratioDb) < 3.0, "  ... loudness within 3 dB of the synth kit (dB)", ratioDb);
  }
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
  testSampledTiming();
  testSampledCuts();
  testCrashAccents();
  testRealKits();
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
