// IRLoader.h - portable WAV parsing, resampling and IR conditioning.
//
// Nothing in here depends on Arduino; the firmware wraps an SD File in a
// ByteSource, and the host tests wrap a FILE*.
#pragma once

#include <stdint.h>

namespace irdsp {

// Minimal random-access byte stream.
class ByteSource {
 public:
  virtual ~ByteSource() {}
  virtual int read(void* dst, int n) = 0;  // returns bytes read
  virtual bool seek(uint32_t pos) = 0;
};

struct WavInfo {
  uint16_t format = 0;      // 1 = PCM int, 3 = IEEE float
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;
  uint16_t blockAlign = 0;  // bytes per frame
  uint32_t dataOffset = 0;
  uint32_t dataBytes = 0;
  uint32_t frames = 0;
};

enum class ChannelMode : uint8_t { kFirst = 0, kMix = 1 };
enum class Normalize : uint8_t { kNone = 0, kEnergy = 1, kPeak = 2 };

struct IRPrepOptions {
  double targetRate = 48000.0;
  ChannelMode channels = ChannelMode::kFirst;
  Normalize normalize = Normalize::kEnergy;
  float trimTailDb = -80.0f;     // drop tail below this (relative to peak); 0 = off
  float trimLeadDb = -70.0f;     // drop leading silence below this; 0 = off
  int truncateFade = 256;        // fade-out length when the IR is cut at maxLen
  // Optional buffer for the resampler's kernel table (makes loading ~50x
  // faster). Needs resampleTableSize(fsIn, targetRate) floats; if missing or
  // too small, the slower exact path is used.
  float* kernelTable = nullptr;
  int kernelTableCap = 0;
};

constexpr double kResampleZeroCrossings = 24.0;
constexpr int kResampleTableOS = 128;

// Kernel table size needed for a given conversion.
inline int resampleTableSize(double fsIn, double fsOut) {
  double ratio = fsIn / fsOut;
  double fc = 0.5 * (ratio > 1.0 ? 1.0 / ratio : 1.0) * 0.97;
  return (int)(kResampleZeroCrossings / (2.0 * fc) * kResampleTableOS) + 3;
}

struct IRPrepResult {
  WavInfo wav;
  int sourceFrames = 0;   // frames in the file
  int length = 0;         // final IR length at targetRate
  int leadTrimmed = 0;    // samples removed from the start
  bool truncated = false; // IR was longer than maxLen
  bool resampled = false;
  float gain = 1.0f;      // normalization gain applied
};

// Parses RIFF/WAVE headers (PCM 8/16/24/32-bit, float 32/64, EXTENSIBLE).
// On failure returns false and sets *err.
bool parseWav(ByteSource& src, WavInfo& info, const char** err);

// Decodes up to maxFrames frames of one channel (or a mix) to float.
int readWavSamples(ByteSource& src, const WavInfo& info, ChannelMode mode,
                   float* dst, int maxFrames);

// Windowed-sinc (Kaiser) resampler. Scales the result by fsIn/fsOut so the
// filter gain of an impulse response is preserved. Returns output length.
int resampleIR(const float* in, int inLen, double fsIn, double fsOut,
               float* out, int maxOut, float* table = nullptr, int tableCap = 0);

// Full pipeline: parse, decode, resample, trim, truncate, normalize.
//   scratch      - window the source is streamed through while resampling
//                  (scratchCap floats; 512 covers every supported sample
//                  rate, more means fewer, larger reads)
//   out          - receives the IR (maxLen floats)
// Returns false and sets *err on failure.
bool prepareIR(ByteSource& src, const IRPrepOptions& opt, float* scratch,
               int scratchCap, float* out, int maxLen, IRPrepResult& res,
               const char** err);

}  // namespace irdsp
