// Renders 8 bars of every drum style (with a 2-bar count-in, a crash every
// 4 bars and its bass line in E) with every kit, and every song with its
// bass, to 16-bit stereo WAV files, to listen to the sounds on a computer.
//   make -C test/host render_drums && ./test/host/render_drums [out_dir]
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "DrumKit.h"
#include "DrumMachine.h"
#include "DrumSong.h"

// x: interleaved stereo.
static bool writeWav(const std::string& path, const std::vector<float>& x, int fs) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  const uint32_t bytes = (uint32_t)x.size() * 2;
  std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16); u16(1); u16(2); u32(fs); u32(fs * 4); u16(4); u16(16);
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
  std::vector<float> bassBuf(irdsp::DrumMachine::kBassBufLen);
  for (int k = 0; k < irdsp::drumKitCount(); k++) {
    for (int s = 0; s < irdsp::drumStyleCount(); s++) {
      const irdsp::DrumStyle& st = irdsp::drumStyle(s);
      const float bpm = !std::strcmp(st.name, "ballad") ? 60.0f : (!std::strcmp(st.name, "shuffle") ? 90.0f : 110.0f);
      std::unique_ptr<irdsp::DrumMachine> d(new irdsp::DrumMachine((float)fs));
      d->setBassBuffer(bassBuf.data());  // the style's bass line in E, at 70 %
      d->setBassVolume(0.7f);
      d->setKit(&irdsp::drumKit(k));
      d->setCrashEvery(4);
      d->setStyle(s);
      d->setTempo(bpm);
      d->setVolume(0.85f);
      d->setCountIn(2);
      d->start();
      const double seconds = 10 * st.beats * 60.0 / bpm + 1.0;  // count-in + 8 bars + tail
      std::vector<float> out;
      float bl[B], br[B];
      for (int i = 0; i < (int)(seconds * fs) / B; i++) {
        if (!d->process(bl, br, B)) for (int j = 0; j < B; j++) bl[j] = br[j] = 0;
        for (int j = 0; j < B; j++) out.push_back(bl[j]), out.push_back(br[j]);
      }
      char name[64];
      std::snprintf(name, sizeof(name), "/drums_%02d_%s_%s.wav", s + 1, st.name, irdsp::drumKit(k).name);
      ok += writeWav(dir + name, out, fs);
    }
  }
  // The rock style with the rock kit at three drum-bus compression amounts.
  int comps = 0;
  for (int comp : {0, 40, 100}) {
    std::unique_ptr<irdsp::DrumMachine> d(new irdsp::DrumMachine((float)fs));
    d->setKit(&irdsp::drumKit(irdsp::drumKitCount() > 1 ? 1 : 0));
    d->setStyle(irdsp::findDrumStyle("rock"));
    d->setTempo(110.0f);
    d->setVolume(0.85f);
    d->setBusComp(comp / 100.0f);
    d->setCrashEvery(4);
    d->setCountIn(0);
    d->start();
    std::vector<float> out;
    float bl[B], br[B];
    for (int j = 0; j < (int)(8 * 4 * 60.0 / 110.0 * fs) / B; j++) {
      if (!d->process(bl, br, B)) for (int q = 0; q < B; q++) bl[q] = br[q] = 0;
      for (int q = 0; q < B; q++) out.push_back(bl[q]), out.push_back(br[q]);
    }
    comps += writeWav(dir + "/drums_02_rock_rock_comp" + std::to_string(comp) + ".wav", out, fs);
  }
  ok += comps == 3 ? 0 : -1;

  // Every built-in song with its bass line (at the default 70 %), once
  // through with a 2-bar count-in (rock kit).
  for (int i = 0; i < irdsp::drumSongCount(); i++) {
    const irdsp::DrumSong& sg = irdsp::drumSong(i);
    std::unique_ptr<irdsp::DrumMachine> d(new irdsp::DrumMachine((float)fs));
    d->setBassBuffer(bassBuf.data());
    d->setBassVolume(0.7f);
    d->setKit(&irdsp::drumKit(irdsp::drumKitCount() > 1 ? 1 : 0));
    d->setSong(&sg);
    d->setTempo(sg.bpm);
    d->setVolume(0.85f);
    d->setCountIn(2);
    d->start();
    const double seconds = (sg.endTick + 2.0 * 4 * irdsp::kSongPpq) * 60.0 / (sg.bpm * irdsp::kSongPpq) + 2.0;
    std::vector<float> out;
    float bl[B], br[B];
    for (int j = 0; j < (int)(seconds * fs) / B; j++) {
      if (!d->process(bl, br, B)) for (int q = 0; q < B; q++) bl[q] = br[q] = 0;
      for (int q = 0; q < B; q++) out.push_back(bl[q]), out.push_back(br[q]);
      if (j * B > (int)((seconds - 2.0) * fs)) d->stop();
    }
    ok += writeWav(dir + "/song_" + sg.name + ".wav", out, fs);
  }
  std::printf("wrote %d WAVs to %s/\n", ok, dir.c_str());
  return ok == irdsp::drumStyleCount() * irdsp::drumKitCount() + irdsp::drumSongCount() ? 0 : 1;
}
