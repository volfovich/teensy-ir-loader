// irtool - runs the exact on-device IR loading pipeline on a PC.
//   irtool <in.wav> <out.f32> [maxLen] [norm:none|energy|peak] [trim:0|1] [mix:0|1] [exact]
// Writes the conditioned IR as raw float32 and prints what was done.
// Handy for checking how the Teensy will interpret an IR file.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "IRLoader.h"

class FileSource : public irdsp::ByteSource {
 public:
  explicit FileSource(FILE* f) : f_(f) {}
  int read(void* dst, int n) override { return (int)fread(dst, 1, n, f_); }
  bool seek(uint32_t pos) override { return fseek(f_, (long)pos, SEEK_SET) == 0; }

 private:
  FILE* f_;
};

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s in.wav out.f32 [maxLen] [none|energy|peak] [trim 0|1] [mix 0|1]\n", argv[0]);
    return 2;
  }
  int maxLen = argc > 3 ? atoi(argv[3]) : 8192;
  irdsp::IRPrepOptions opt;
  if (argc > 4) {
    if (!strcmp(argv[4], "none")) opt.normalize = irdsp::Normalize::kNone;
    if (!strcmp(argv[4], "peak")) opt.normalize = irdsp::Normalize::kPeak;
  }
  if (argc > 5 && atoi(argv[5]) == 0) { opt.trimLeadDb = 0; opt.trimTailDb = 0; }
  if (argc > 6 && atoi(argv[6]) == 1) opt.channels = irdsp::ChannelMode::kMix;

  FILE* f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 1; }
  FileSource src(f);
  // Same buffer sizes as the firmware.
  std::vector<float> scratch(36864), out(maxLen), table(14336);
  if (!(argc > 7 && !strcmp(argv[7], "exact"))) {
    opt.kernelTable = table.data();
    opt.kernelTableCap = (int)table.size();
  }
  irdsp::IRPrepResult res;
  const char* err = "";
  bool ok = irdsp::prepareIR(src, opt, scratch.data(), (int)scratch.size(), out.data(), maxLen, res, &err);
  fclose(f);
  if (!ok) { printf("ERROR: %s\n", err); return 1; }
  printf("format=%u channels=%u rate=%u bits=%u frames=%d -> length=%d lead=%d truncated=%d resampled=%d gain=%.6f\n",
         res.wav.format, res.wav.channels, res.wav.sampleRate, res.wav.bitsPerSample, res.sourceFrames,
         res.length, res.leadTrimmed, res.truncated, res.resampled, res.gain);
  FILE* o = fopen(argv[2], "wb");
  fwrite(out.data(), sizeof(float), res.length, o);
  fclose(o);
  return 0;
}
