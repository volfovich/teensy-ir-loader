// namtool - reads a .nam file the way the Teensy does (NamFile.h) and prints
// what it found; with an input, also runs the A2-Lite network on it.
//   namtool <model.nam> [in.f32 out.f32]
// The audio files are raw 32-bit floats, mono, 48 kHz. Handy for checking a
// model file before copying it to the card.
#include <cmath>
#include <cstdio>
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

int main(int argc, char** argv) {
  if (argc != 2 && argc != 4) {
    fprintf(stderr, "usage: %s model.nam [in.f32 out.f32]\n", argv[0]);
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
