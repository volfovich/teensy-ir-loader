// Host-side tests for the MIDI drum loader (MidiDrums.h): files built here.
//   make -C test/host && ./test/host/test_midi
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "DrumKit.h"
#include "MidiDrums.h"

static int failures = 0;

static void check(bool ok, const char* what, double value) {
  std::printf("  %-56s %10.4g  %s\n", what, value, ok ? "OK" : "FAIL");
  if (!ok) failures++;
}

// A MidiSource over bytes in memory.
struct MemSource : irdsp::MidiSource {
  const std::vector<uint8_t>& b;
  size_t pos = 0;
  explicit MemSource(const std::vector<uint8_t>& bytes) : b(bytes) {}
  int read(uint8_t* dst, int n) override {
    const int k = (int)std::min((size_t)n, b.size() > pos ? b.size() - pos : 0);
    std::memcpy(dst, b.data() + pos, k);
    pos += k;
    return k;
  }
  bool seek(uint32_t p) override {
    pos = p;
    return p <= b.size();
  }
};

// Builds a track: events as raw bytes with their delta times.
struct Track {
  std::vector<uint8_t> d;
  uint32_t last = 0;
  void delta(uint32_t t) {
    uint32_t v = t - last;
    last = t;
    uint8_t tmp[4];
    int n = 0;
    tmp[n++] = v & 0x7F;
    while (v >>= 7) tmp[n++] = 0x80 | (v & 0x7F);
    while (n) d.push_back(tmp[--n]);
  }
  Track& raw(uint32_t t, std::initializer_list<uint8_t> bytes) {
    delta(t);
    d.insert(d.end(), bytes);
    return *this;
  }
  Track& tempo(uint32_t t, uint32_t us) { return raw(t, {0xFF, 0x51, 3, (uint8_t)(us >> 16), (uint8_t)(us >> 8), (uint8_t)us}); }
  Track& meter(uint32_t t, int num, int den) { return raw(t, {0xFF, 0x58, 4, (uint8_t)num, (uint8_t)den, 24, 8}); }
  Track& note(uint32_t t, int ch, int note, int vel) { return raw(t, {(uint8_t)(0x90 | ch), (uint8_t)note, (uint8_t)vel}); }
  std::vector<uint8_t> bytes() {
    Track t = *this;
    t.raw(last, {0xFF, 0x2F, 0});
    std::vector<uint8_t> out = {'M', 'T', 'r', 'k'};
    const uint32_t n = (uint32_t)t.d.size();
    for (int s = 24; s >= 0; s -= 8) out.push_back((uint8_t)(n >> s));
    out.insert(out.end(), t.d.begin(), t.d.end());
    return out;
  }
};

static std::vector<uint8_t> smf(int format, int ppq, std::vector<Track> tracks) {
  std::vector<uint8_t> f = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, (uint8_t)format, 0, (uint8_t)tracks.size(),
                            (uint8_t)(ppq >> 8), (uint8_t)ppq};
  for (auto& t : tracks) {
    const auto b = t.bytes();
    f.insert(f.end(), b.begin(), b.end());
  }
  return f;
}

struct Loaded {
  std::vector<irdsp::SongEvent> ev = std::vector<irdsp::SongEvent>(4096);
  std::vector<irdsp::SongBar> bars = std::vector<irdsp::SongBar>(512);
  irdsp::DrumSong song{};
  irdsp::MidiResult res;
  Loaded(const std::vector<uint8_t>& file, uint32_t cap = 4096) {
    MemSource src(file);
    res = irdsp::loadMidiDrums(src, ev.data(), cap, bars.data(), 512, song);
  }
  bool has(uint32_t tick, int lane, int vel) const {
    for (uint32_t i = 0; i < song.eventCount; i++)
      if (ev[i].tick() == tick && ev[i].lane() == lane && ev[i].vel() == vel) return true;
    return false;
  }
};

int main() {
  std::printf("MIDI drums\n");
  {
    // Format 0, 96 PPQ, 120 BPM, 4/4: kick, 8th hat, snare on 2, clap,
    // tambourine (no lane), a 32nd and an off-grid note.
    Track t;
    t.tempo(0, 500000).meter(0, 4, 2);
    t.note(0, 9, 36, 127).note(0, 9, 42, 70).note(48, 9, 42, 50).note(96, 9, 38, 64);
    t.note(144, 9, 54, 90).note(192, 9, 39, 100).note(204, 9, 38, 40).note(289, 9, 36, 127);
    Loaded l(smf(0, 96, {t}));
    check(!l.res.error, "format 0 loads", 0);
    check(l.song.bpm == 120 && l.res.ignored == 1, "tempo 120 BPM; the tambourine is skipped", l.song.bpm);
    check(l.has(0, irdsp::kKick, 9) && l.has(0, irdsp::kHatClosed, 5) && l.has(12, irdsp::kHatClosed, 4) &&
              l.has(24, irdsp::kSnare, 5) && l.has(48, irdsp::kSnare, 8),
          "GM map, velocity 1-127 -> 1-9, 96 -> 24 PPQ", l.song.eventCount);
    check(l.has(51, irdsp::kSnare, 3) && l.has(72, irdsp::kKick, 9), "a 32nd (tick 51) and an off-grid hit rounded (72)",
          l.song.eventCount);
    check(l.song.barCount == 1 && l.bars[0].beats == 4 && l.song.endTick == 96, "one bar of 4/4", l.song.barCount);
  }
  {
    // Format 1, 480 PPQ: tempo and 3/4 in track 0, the drums in track 1
    // with running status; a change to 4/4 at bar 3.
    Track t0, t1;
    t0.tempo(0, 600000).meter(0, 3, 2).meter(2 * 1440, 4, 2);
    t1.raw(0, {0x99, 36, 100}).raw(480, {38, 100}).raw(960, {38, 0}).raw(1440, {36, 90}).raw(2880, {36, 90});
    t1.raw(2880 + 1920, {36, 90});
    Loaded l(smf(1, 480, {t0, t1}));
    check(!l.res.error && l.song.bpm == 100, "format 1, running status, 100 BPM", l.song.bpm);
    check(l.has(0, irdsp::kKick, 8) && l.has(24, irdsp::kSnare, 8) && !l.has(48, irdsp::kSnare, 1) &&
              l.has(72, irdsp::kKick, 7),
          "  ... note-on velocity 0 is a note-off", l.song.eventCount);
    check(l.song.barCount == 4 && l.bars[1].tick == 72 && l.bars[2].tick == 144 && l.bars[2].beats == 4 &&
              l.bars[3].tick == 240 && l.bars[3].firstEvent == 4,
          "  ... 3/4, 3/4, then 4/4: bar ticks and first events", l.song.barCount);
  }
  {
    // No channel 10: the drums are on channel 1 of the busiest track.
    Track piano, drums;
    piano.note(0, 0, 60, 80).note(96, 0, 64, 80);
    drums.note(0, 0, 36, 100).note(48, 0, 42, 80).note(96, 0, 38, 100).note(144, 0, 42, 80).note(192, 0, 36, 100);
    Loaded l(smf(1, 96, {piano, drums}));
    check(!l.res.error && l.song.eventCount == 5 && l.has(24, irdsp::kSnare, 8), "no channel 10: the busiest drum track",
          l.song.eventCount);
  }
  {
    // Two hits on the same tick and lane (closed and pedal hat): the louder.
    Track t;
    t.note(0, 9, 42, 50).note(0, 9, 44, 120).note(96, 9, 36, 100);
    Loaded l(smf(0, 96, {t}));
    check(l.song.eventCount == 2 && l.has(0, irdsp::kHatClosed, 9), "same tick and lane: one hit, the louder", l.song.eventCount);
  }
  {
    // Too many notes for the buffer, no drums, garbage and truncated files.
    Track t;
    for (int i = 0; i < 100; i++) t.note(i * 24, 9, 36, 100);
    Loaded over(smf(0, 96, {t}), 50);
    Track p;
    p.note(0, 0, 60, 80);
    Loaded none(smf(0, 96, {p}));
    std::mt19937 rng(1);
    std::vector<uint8_t> junk(2000);
    for (auto& b : junk) b = (uint8_t)rng();
    Loaded garbage(junk);
    auto file = smf(0, 96, {t});
    file.resize(file.size() / 2);
    Loaded cut(file);
    check(over.res.error && none.res.error && garbage.res.error, "too many notes, no drums, garbage: errors", 0);
    check(!cut.res.error && cut.song.eventCount > 0 && cut.song.eventCount < 100, "truncated file: the hits before the cut",
          cut.song.eventCount);
    // Random bytes after a valid header must never crash or overrun.
    bool ok = true;
    for (int k = 0; k < 200; k++) {
      std::vector<uint8_t> f = smf(1, 96, {t});
      for (size_t i = 22; i < f.size(); i++)
        if (rng() % 16 == 0) f[i] = (uint8_t)rng();
      Loaded l(f, 64);
      ok = ok && l.song.eventCount <= 64;
    }
    check(ok, "200 corrupted files: no crash, no overrun", 200);
  }
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
