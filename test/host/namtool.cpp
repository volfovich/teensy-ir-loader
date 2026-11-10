// namtool - reads a .nam file the way the Teensy does (NamFile.h) and prints
// what it found; with an input, also runs the A2-Lite network on it.
//   namtool <model.nam> [in.f32 out.f32]
//   namtool --dc <model.nam>   the DC the network puts out: on silence and
//                              on a held note (the firmware's 5 Hz DC
//                              blocker, AmpStage.h, takes it out)
// The audio files are raw 32-bit floats, mono, 48 kHz. Handy for checking a
// model file before copying it to the card.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "NamA2.h"
#include "NamFile.h"

class FileSource : public irdsp::ByteSource {
 public:
  explicit FileSource(FILE* f) : f_(f) {}
  int read(void* dst, int n) override { return (int)fread(dst, 1, n, f_); }
  bool seek(uint32_t pos) override { return fseek(f_, (long)pos, SEEK_SET) == 0; }

 private:
  FILE* f_;
};

static void printLevel(const char* what, float v) {
  if (std::isnan(v)) printf("%-15s -\n", what);
  else printf("%-15s %.2f\n", what, v);
}

// The network's mean output over n samples of x (after a warm-up of 1 s of
// the same input), raw and after the firmware's levelling (-12 - loudness).
static void printDc(const char* what, const std::vector<float>& w, float level, float (*sig)(long)) {
  std::vector<float> hist(irdsp::NamA2<32>::kHistoryFloats);
  auto* nam = new irdsp::NamA2<32>;
  nam->begin(hist.data());
  nam->setWeights(w.data());
  double sum = 0.0, pow2 = 0.0;
  long n = 0;
  float x[32], y[32];
  for (long t = 0; t < 3 * 48000; t += 32) {
    for (int i = 0; i < 32; i++) x[i] = sig(t + i);
    nam->process(x, y);
    if (t >= 48000)
      for (int i = 0; i < 32; i++) sum += y[i], pow2 += (double)y[i] * y[i], n++;
  }
  delete nam;
  const double dc = sum / n, rms = std::sqrt(pow2 / n);
  printf("%-15s DC %+.6f (%.1f dBFS, %.1f dBFS levelled), rms %.1f dBFS\n", what, dc,
         20.0 * std::log10(std::fabs(dc) + 1e-12), 20.0 * std::log10(std::fabs(dc) * level + 1e-12),
         20.0 * std::log10(rms + 1e-12));
}

static float silence(long) { return 0.0f; }
static float heldNote(long t) {  // A2 (110 Hz) with harmonics, -14 dBFS peak
  float v = 0.0f;
  for (int h = 1; h < 6; h++) v += std::sin(2.0f * 3.14159265f * 110.0f * h * (float)t / 48000.0f) / h;
  return 0.12f * v;
}

int main(int argc, char** argv) {
  const bool dc = argc == 3 && !strcmp(argv[1], "--dc");
  if (dc) argv++, argc--;
  if (argc != 2 && argc != 4) {
    fprintf(stderr, "usage: %s model.nam [in.f32 out.f32]\n       %s --dc model.nam\n", argv[0], argv[0]);
    return 2;
  }
  FILE* f = fopen(argv[1], "rb");
  if (!f) {
    perror(argv[1]);
    return 1;
  }
  FileSource src(f);
  std::vector<float> w(irdsp::nama2::kWeightCount);
  irdsp::NamInfo info;
  const char* err = "";
  const bool ok = irdsp::readNamA2Lite(src, w.data(), &info, &err);
  fclose(f);
  if (!ok) {
    printf("ERROR: %s\n", err);
    return 1;
  }
  printf("%-15s A2-Lite, %d weights%s\n", "network", irdsp::nama2::kWeightCount,
         info.container ? " (from an A2 container)" : "");
  printf("%-15s %s\n%-15s %s %s\n%-15s %s / %s\n", "name", info.name, "gear", info.gearMake, info.gearModel,
         "type / tone", info.gearType, info.toneType);
  printf("%-15s %.0f Hz\n", "sample rate", info.sampleRate);
  printLevel("loudness dB", info.loudness);
  printLevel("input dBu", info.inputLevelDbu);
  printLevel("output dBu", info.outputLevelDbu);
  if (dc) {
    const float level = std::pow(10.0f, (-12.0f - (std::isnan(info.loudness) ? -12.0f : info.loudness)) / 20.0f);
    printDc("silence", w, level, silence);
    printDc("held note", w, level, heldNote);
  }
  if (argc == 2) return 0;

  FILE* in = fopen(argv[2], "rb");
  if (!in) {
    perror(argv[2]);
    return 1;
  }
  std::vector<float> x;
  float block[32];
  size_t n;
  while ((n = fread(block, 4, 32, in)) > 0) x.insert(x.end(), block, block + n);
  fclose(in);
  x.resize((x.size() + 31) / 32 * 32, 0.0f);
  std::vector<float> hist(irdsp::NamA2<32>::kHistoryFloats), y(x.size());
  auto* nam = new irdsp::NamA2<32>;
  nam->begin(hist.data());
  nam->setWeights(w.data());
  for (size_t i = 0; i < x.size(); i += 32) nam->process(&x[i], &y[i]);
  delete nam;
  FILE* out = fopen(argv[3], "wb");
  if (!out || fwrite(y.data(), 4, y.size(), out) != y.size() || fclose(out)) {
    perror(argv[3]);
    return 1;
  }
  printf("%-15s %zu samples -> %s\n", "rendered", y.size(), argv[3]);
  return 0;
}
