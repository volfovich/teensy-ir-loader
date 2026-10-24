// Renders 8 bars of every drum style (with the count-in) to 16-bit WAV
// files, to listen to the sounds on a computer.
//   make -C test/host render_drums && ./test/host/render_drums [out_dir]
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "DrumMachine.h"

static bool writeWav(const std::string& path, const std::vector<float>& x, int fs) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  const uint32_t bytes = (uint32_t)x.size() * 2;
  std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16); u16(1); u16(1); u32(fs); u32(fs * 2); u16(2); u16(16);
  std::fwrite("data", 1, 4, f); u32(bytes);
  for (float s : x) {
    const float c = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
    const int16_t v = (int16_t)(c * 32767.0f);
    std::fwrite(&v, 2, 1, f);  // little-endian hosts
  }
  std::fclose(f);
  return true;
}

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "out";
  const int fs = 44100, B = 32;
  int ok = 0;
  for (int s = 0; s < irdsp::drumStyleCount(); s++) {
    const irdsp::DrumStyle& st = irdsp::drumStyle(s);
    const float bpm = s == 7 ? 60.0f : (s == 3 ? 90.0f : 110.0f);  // ballad, shuffle
    std::unique_ptr<irdsp::DrumMachine> d(new irdsp::DrumMachine((float)fs));
    d->setStyle(s);
    d->setTempo(bpm);
    d->setVolume(0.85f);
    d->setCountIn(true);
    d->start();
    const double seconds = 9 * st.beats * 60.0 / bpm * (st.stepsPerBeat == 3 && st.beats == 2 ? 1 : 1) + 1.0;
    std::vector<float> out;
    float buf[B];
    for (int i = 0; i < (int)(seconds * fs) / B; i++) {
      if (!d->process(buf, B)) for (float& x : buf) x = 0;
      out.insert(out.end(), buf, buf + B);
    }
    char name[64];
    std::snprintf(name, sizeof(name), "/drums_%02d_%s.wav", s + 1, st.name);
    ok += writeWav(dir + name, out, fs);
  }
  std::printf("wrote %d WAVs to %s/\n", ok, dir.c_str());
  return ok == irdsp::drumStyleCount() ? 0 : 1;
}
