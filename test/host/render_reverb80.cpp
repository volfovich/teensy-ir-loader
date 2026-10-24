// Renders the 80s reverb to stereo WAV files, to listen on a computer:
//   reverb80_hall_guitar.wav    a plucked-string riff through the hall
//   reverb80_gated_guitar.wav   palm-muted chugs through the gated reverb
//   reverb80_gated_drums.wav    the drum machine's rock groove, gated (the
//                               classic 80s snare)
// plus the dry sources (*_dry.wav).
//   make -C test/host render   (writes to test/host/out/)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "DrumKit.h"
#include "DrumMachine.h"
#include "Reverb80.h"

static const int kFs = 44100, kB = 32;

static bool writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  const uint32_t bytes = (uint32_t)l.size() * 4;
  std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16); u16(1); u16(2); u32(kFs); u32(kFs * 4); u16(4); u16(16);
  std::fwrite("data", 1, 4, f); u32(bytes);
  for (size_t i = 0; i < l.size(); i++)
    for (float s : {l[i], r[i]}) {
      const float c = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
      const int16_t v = (int16_t)(c * 32767.0f);
      std::fwrite(&v, 2, 1, f);
    }
  std::fclose(f);
  return true;
}

// Karplus-Strong plucked strings: notes (Hz, start s, length s, damping).
struct Note { float hz, start, len, damp; };
static std::vector<float> pluck(const std::vector<Note>& notes, float seconds) {
  std::vector<float> out((size_t)(seconds * kFs), 0.0f);
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> d(-1.0f, 1.0f);
  for (const Note& n : notes) {
    const int p = (int)(kFs / n.hz);
    std::vector<float> line(p);
    for (auto& v : line) v = d(rng) * 0.3f;
    const size_t s0 = (size_t)(n.start * kFs), s1 = std::min(out.size(), (size_t)((n.start + n.len) * kFs));
    float prev = 0.0f;
    for (size_t i = s0, k = 0; i < s1; i++, k = (k + 1) % p) {
      const float v = line[k];
      line[k] = n.damp * 0.5f * (v + prev);
      prev = v;
      const float fade = s1 - i < 220 ? (float)(s1 - i) / 220.0f : 1.0f;
      out[i] += v * fade;
    }
  }
  return out;
}

static void mixReverb(irdsp::Reverb80& rv, const std::vector<float>& dryL, const std::vector<float>& dryR, float mix,
                      std::vector<float>& l, std::vector<float>& r) {
  float wet, dry;
  irdsp::mixPower(mix, &wet, &dry);
  l.assign(dryL.size(), 0.0f);
  r.assign(dryL.size(), 0.0f);
  for (size_t i = 0; i + kB <= dryL.size(); i += kB) {
    float wl[kB], wr[kB];
    rv.process(&dryL[i], &dryR[i], wl, wr, kB);
    for (int j = 0; j < kB; j++) {
      l[i + j] = dry * dryL[i + j] + wet * wl[j];
      r[i + j] = dry * dryR[i + j] + wet * wr[j];
    }
  }
}

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "out";
  std::vector<float> mem(irdsp::Reverb80::kMemFloats);
  int ok = 0;

  // A slow E-minor arpeggio with a held bend-less lead line on top.
  const float E2 = 82.41f, B2 = 123.47f, E3 = 164.81f, G3 = 196.0f, B3 = 246.94f, E4 = 329.63f, D4 = 293.66f;
  std::vector<Note> arp;
  const float seq[] = {E2, B2, E3, G3, B3, E4, B3, G3};
  for (int bar = 0; bar < 2; bar++)
    for (int k = 0; k < 8; k++) arp.push_back({seq[k], bar * 2.4f + k * 0.3f, 1.6f, 0.998f});
  arp.push_back({D4, 4.8f, 1.2f, 0.999f});
  arp.push_back({E4, 6.0f, 2.0f, 0.999f});
  const auto guitar = pluck(arp, 11.0f);
  {
    irdsp::Reverb80 rv(kFs);
    rv.init(mem.data());
    rv.setMode(irdsp::Reverb80::kHall);
    rv.setSize(0.6f);
    rv.setPreDelayMs(40.0f);
    std::vector<float> l, r;
    mixReverb(rv, guitar, guitar, 0.35f, l, r);
    ok += writeWav(dir + "/reverb80_hall_guitar.wav", l, r);
    ok += writeWav(dir + "/reverb80_hall_guitar_dry.wav", guitar, guitar);
  }
  {
    // Palm-muted 8th-note chugs (short, heavily damped notes) with stops.
    std::vector<Note> chug;
    for (int i = 0; i < 16; i++)
      if (i % 8 != 6) chug.push_back({i % 4 == 3 ? 98.0f : E2, i * 0.25f, 0.18f, 0.985f});
    const auto chugs = pluck(chug, 5.0f);
    irdsp::Reverb80 rv(kFs);
    rv.init(mem.data());
    rv.setMode(irdsp::Reverb80::kGated);
    rv.setPreDelayMs(10.0f);
    rv.setGateMs(250.0f);
    std::vector<float> l, r;
    mixReverb(rv, chugs, chugs, 0.35f, l, r);
    ok += writeWav(dir + "/reverb80_gated_guitar.wav", l, r);
    ok += writeWav(dir + "/reverb80_gated_guitar_dry.wav", chugs, chugs);
  }
  {
    // The drum machine's rock groove (rock kit), 4 bars at 100 BPM.
    std::unique_ptr<irdsp::DrumMachine> d(new irdsp::DrumMachine((float)kFs));
    d->setKit(&irdsp::drumKit(irdsp::drumKitCount() > 1 ? 1 : 0));
    d->setStyle(irdsp::findDrumStyle("rock"));
    d->setTempo(100.0f);
    d->setVolume(0.8f);
    d->setCountIn(0);
    d->start();
    std::vector<float> dl, dr;
    float bl[kB], br[kB];
    for (int i = 0; i < 11 * kFs / kB; i++) {
      if (!d->process(bl, br, kB)) for (int j = 0; j < kB; j++) bl[j] = br[j] = 0.0f;
      dl.insert(dl.end(), bl, bl + kB);
      dr.insert(dr.end(), br, br + kB);
    }
    irdsp::Reverb80 rv(kFs);
    rv.init(mem.data());
    rv.setMode(irdsp::Reverb80::kGated);
    rv.setPreDelayMs(5.0f);
    rv.setGateMs(350.0f);
    std::vector<float> l, r;
    mixReverb(rv, dl, dr, 0.4f, l, r);
    ok += writeWav(dir + "/reverb80_gated_drums.wav", l, r);
    ok += writeWav(dir + "/reverb80_gated_drums_dry.wav", dl, dr);
  }
  std::printf("wrote %d WAVs to %s/\n", ok, dir.c_str());
  return ok == 6 ? 0 : 1;
}
