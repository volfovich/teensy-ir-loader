// Renders the plucked-string bass to 16-bit WAV files, to listen to it on a
// computer: a scale, a rock line with mutes and dead notes, and the same
// line at three tones.
//   make -C test/host render_bass && ./test/host/render_bass [out_dir]
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "PluckBass.h"

static const int kFs = 48000, kB = 32;

static bool writeWav(const std::string& path, const std::vector<float>& x) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  const uint32_t bytes = (uint32_t)x.size() * 2;
  std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16); u16(1); u16(1); u32(kFs); u32(kFs * 2); u16(2); u16(16);
  std::fwrite("data", 1, 4, f); u32(bytes);
  for (float s : x) {
    const float c = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
    const int16_t v = (int16_t)(c * 32767.0f);
    std::fwrite(&v, 2, 1, f);
  }
  std::fclose(f);
  return true;
}

// A line on a 16th grid: note (MIDI), 0 = hold, -1 = mute, -2 = dead note
// (on the last pitch).
struct Step { int note, vel; };

static std::vector<float> play(irdsp::PluckBass& b, const std::vector<Step>& line, float bpm, int repeats) {
  const double stepSamples = kFs * 60.0 / bpm / 4.0;
  std::vector<float> out;
  int last = 28;
  double next = 0.0;
  size_t step = 0, total = line.size() * repeats;
  float blk[kB];
  for (size_t t = 0; step < total || t < (size_t)(next + kFs * 1.5); t += kB) {
    while (step < total && next <= (double)t) {
      const Step& s = line[step % line.size()];
      if (s.note > 0) b.noteOn(last = s.note, s.vel);
      else if (s.note == -1) b.mute();
      else if (s.note == -2) b.noteOn(last, s.vel, true);
      next += stepSamples;
      step++;
    }
    for (float& v : blk) v = 0.0f;
    b.process(blk, kB);
    out.insert(out.end(), blk, blk + kB);
  }
  return out;
}

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "out";
  std::vector<float> buf(2 * irdsp::PluckBass::kLineLen);
  irdsp::PluckBass b;
  int ok = 0;

  // E major scale from E1 to E2 and back, quarter notes, each muted before
  // the next (a played scale), at 90 BPM.
  std::vector<Step> scale;
  for (int n : {28, 30, 32, 33, 35, 37, 39, 40, 39, 37, 35, 33, 32, 30, 28}) {
    scale.push_back({n, 7});
    scale.push_back({0, 0});
    scale.push_back({0, 0});
    scale.push_back({-1, 0});
  }
  b.init((float)kFs, buf.data());
  ok += writeWav(dir + "/bass_scale.wav", play(b, scale, 90.0f, 1));

  // A rock line in E: driving eighths, a dead-note ghost, a push to G and A.
  const std::vector<Step> rock = {
      {28, 8}, {0, 0}, {28, 6}, {0, 0}, {28, 7}, {0, 0}, {-2, 4}, {28, 6},
      {28, 8}, {0, 0}, {28, 6}, {0, 0}, {31, 7}, {0, 0}, {33, 8}, {-1, 0},
      {28, 8}, {0, 0}, {28, 6}, {0, 0}, {28, 7}, {0, 0}, {-2, 4}, {28, 6},
      {40, 8}, {0, 0}, {38, 6}, {0, 0}, {35, 7}, {0, 0}, {33, 8}, {31, 6},
  };
  const char* names[] = {"dark", "mid", "bright"};
  const float tones[] = {0.0f, 0.5f, 1.0f};
  for (int i = 0; i < 3; i++) {
    b.init((float)kFs, buf.data());
    b.setTone(tones[i]);
    ok += writeWav(dir + "/bass_rock_" + names[i] + ".wav", play(b, rock, 110.0f, 4));
  }

  // Single long notes: B0, E1, A1, E2, to hear the sustain.
  std::vector<Step> longs;
  for (int n : {23, 28, 33, 40}) {
    longs.push_back({n, 7});
    for (int k = 0; k < 15; k++) longs.push_back({0, 0});
  }
  b.init((float)kFs, buf.data());
  ok += writeWav(dir + "/bass_sustain.wav", play(b, longs, 60.0f, 1));

  std::printf("wrote %d WAVs to %s/\n", ok, dir.c_str());
  return ok == 5 ? 0 : 1;
}
