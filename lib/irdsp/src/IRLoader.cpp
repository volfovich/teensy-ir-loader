// IRLoader.cpp - see IRLoader.h
#include "IRLoader.h"

#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <string.h>

namespace irdsp {

namespace {

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

bool readExact(ByteSource& s, void* dst, int n) { return s.read(dst, n) == n; }

// Decodes one sample of the given container size/format to float.
inline float decode(const uint8_t* p, int bytes, bool isFloat) {
  if (isFloat) {
    if (bytes == 4) {
      uint32_t u = rd32(p);
      float f;
      memcpy(&f, &u, 4);
      return f;
    }
    uint64_t u = (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
    double d;
    memcpy(&d, &u, 8);
    return (float)d;
  }
  switch (bytes) {
    case 1: return ((int)p[0] - 128) * (1.0f / 128.0f);
    case 2: return (int16_t)rd16(p) * (1.0f / 32768.0f);
    case 3: {
      int32_t v = (int32_t)(((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) |
                            ((uint32_t)p[2] << 24));
      return (float)v * (1.0f / 2147483648.0f);
    }
    default: return (float)(int32_t)rd32(p) * (1.0f / 2147483648.0f);
  }
}

// Modified Bessel function of the first kind, order 0 (for the Kaiser window).
double besselI0(double x) {
  double sum = 1.0, term = 1.0, q = x * x / 4.0;
  for (int k = 1; k < 50; k++) {
    term *= q / ((double)k * k);
    sum += term;
    if (term < sum * 1e-12) break;
  }
  return sum;
}

}  // namespace

bool parseWav(ByteSource& src, WavInfo& info, const char** err) {
  info = WavInfo();
  uint8_t h[12];
  if (!src.seek(0) || !readExact(src, h, 12)) { *err = "file too short"; return false; }
  if (memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) {
    *err = "not a RIFF/WAVE file";
    return false;
  }
  uint32_t pos = 12;
  bool haveFmt = false;
  for (int guard = 0; guard < 64; guard++) {
    uint8_t ch[8];
    if (!src.seek(pos) || !readExact(src, ch, 8)) break;
    uint32_t size = rd32(ch + 4);
    uint32_t body = pos + 8;
    if (memcmp(ch, "fmt ", 4) == 0) {
      uint8_t f[40];
      memset(f, 0, sizeof(f));
      int n = size < sizeof(f) ? (int)size : (int)sizeof(f);
      if (n < 16 || !readExact(src, f, n)) { *err = "bad fmt chunk"; return false; }
      info.format = rd16(f);
      info.channels = rd16(f + 2);
      info.sampleRate = rd32(f + 4);
      info.blockAlign = rd16(f + 12);
      info.bitsPerSample = rd16(f + 14);
      if (info.format == 0xFFFE) {  // WAVE_FORMAT_EXTENSIBLE
        if (n < 26) { *err = "bad extensible fmt"; return false; }
        info.format = rd16(f + 24);  // first two bytes of the SubFormat GUID
      }
      haveFmt = true;
    } else if (memcmp(ch, "data", 4) == 0) {
      if (!haveFmt) { *err = "data before fmt"; return false; }
      info.dataOffset = body;
      info.dataBytes = size;
      break;
    }
    pos = body + size + (size & 1);  // chunks are word aligned
  }
  if (!haveFmt) { *err = "no fmt chunk"; return false; }
  if (info.dataOffset == 0) { *err = "no data chunk"; return false; }
  if (info.channels == 0 || info.blockAlign == 0) { *err = "bad format"; return false; }
  int bytes = info.blockAlign / info.channels;
  if (info.blockAlign != bytes * info.channels) { *err = "bad block align"; return false; }
  bool ok = (info.format == 1 && bytes >= 1 && bytes <= 4) ||
            (info.format == 3 && (bytes == 4 || bytes == 8));
  if (!ok) { *err = "unsupported sample format (need PCM int or float)"; return false; }
  if (info.sampleRate < 8000 || info.sampleRate > 384000) { *err = "unsupported sample rate"; return false; }
  info.frames = info.dataBytes / info.blockAlign;
  if (info.frames == 0) { *err = "empty data chunk"; return false; }
  return true;
}

int readWavSamples(ByteSource& src, const WavInfo& info, ChannelMode mode,
                   float* dst, int maxFrames) {
  int frames = (int)info.frames < maxFrames ? (int)info.frames : maxFrames;
  if (!src.seek(info.dataOffset)) return 0;
  const int bytes = info.blockAlign / info.channels;
  const bool isFloat = info.format == 3;
  const int chans = info.channels;
  uint8_t buf[2048];
  const int framesPerRead = (int)sizeof(buf) / info.blockAlign;
  int done = 0;
  while (done < frames) {
    int want = frames - done;
    if (want > framesPerRead) want = framesPerRead;
    int got = src.read(buf, want * info.blockAlign) / info.blockAlign;
    if (got <= 0) break;
    for (int i = 0; i < got; i++) {
      const uint8_t* fp = buf + i * info.blockAlign;
      if (mode == ChannelMode::kMix && chans > 1) {
        float s = 0.0f;
        for (int c = 0; c < chans; c++) s += decode(fp + c * bytes, bytes, isFloat);
        dst[done + i] = s / (float)chans;
      } else {
        dst[done + i] = decode(fp, bytes, isFloat);
      }
    }
    done += got;
  }
  return done;
}

int resampleIR(const float* in, int inLen, double fsIn, double fsOut,
               float* out, int maxOut, float* table, int tableCap) {
  const double ratio = fsIn / fsOut;  // input samples per output sample
  int outLen = (int)ceil(inLen / ratio);
  if (outLen > maxOut) outLen = maxOut;
  if (fabs(ratio - 1.0) < 1e-9) {
    for (int i = 0; i < outLen; i++) out[i] = in[i];
    return outLen;
  }
  // Low-pass at 97% of the lower Nyquist frequency, in cycles/input sample.
  const double fc = 0.5 * (ratio > 1.0 ? 1.0 / ratio : 1.0) * 0.97;
  const double halfWidth = kResampleZeroCrossings / (2.0 * fc);  // input samples
  const double beta = 8.6;  // ~ -90 dB stopband
  const double i0beta = besselI0(beta);
  auto kernel = [&](double d) -> double {
    d = fabs(d);
    if (d >= halfWidth) return 0.0;
    const double x = 2.0 * fc * d;
    const double sinc = x < 1e-12 ? 1.0 : sin(M_PI * x) / (M_PI * x);
    const double r = d / halfWidth;
    const double w = besselI0(beta * sqrt(fmax(0.0, 1.0 - r * r))) / i0beta;
    return 2.0 * fc * sinc * w;
  };
  const float scale = (float)ratio;  // preserve filter gain (sum of taps)

  // Fast path: tabulate the kernel once (kResampleTableOS points per input
  // sample) and interpolate linearly; ~50x faster than evaluating it per tap.
  const int tabLen = (int)ceil(halfWidth * kResampleTableOS) + 2;
  if (table && tabLen <= tableCap) {
    for (int i = 0; i < tabLen; i++) table[i] = (float)kernel((double)i / kResampleTableOS);
    for (int n = 0; n < outLen; n++) {
      const double t = n * ratio;
      int k0 = (int)ceil(t - halfWidth);
      int k1 = (int)floor(t + halfWidth);
      if (k0 < 0) k0 = 0;
      if (k1 > inLen - 1) k1 = inLen - 1;
      float acc = 0.0f;
      const float d0 = (float)(t - k0);  // distance to the first tap
      for (int k = k0; k <= k1; k++) {
        float pos = fabsf(d0 - (float)(k - k0)) * (float)kResampleTableOS;
        int i = (int)pos;
        if (i >= tabLen - 1) continue;
        float fr = pos - (float)i;
        acc += in[k] * (table[i] + fr * (table[i + 1] - table[i]));
      }
      out[n] = acc * scale;
    }
    return outLen;
  }

  // Reference path: evaluate the kernel for every tap.
  for (int n = 0; n < outLen; n++) {
    const double t = n * ratio;
    int k0 = (int)ceil(t - halfWidth);
    int k1 = (int)floor(t + halfWidth);
    if (k0 < 0) k0 = 0;
    if (k1 > inLen - 1) k1 = inLen - 1;
    double acc = 0.0;
    for (int k = k0; k <= k1; k++) acc += in[k] * kernel(t - k);
    out[n] = (float)acc * scale;
  }
  return outLen;
}

bool prepareIR(ByteSource& src, const IRPrepOptions& opt, float* scratch,
               int scratchCap, float* out, int maxLen, IRPrepResult& res,
               const char** err) {
  res = IRPrepResult();
  if (!parseWav(src, res.wav, err)) return false;
  res.sourceFrames = (int)res.wav.frames;
  const double ratio = res.wav.sampleRate / opt.targetRate;
  res.resampled = fabs(ratio - 1.0) > 1e-9;

  // Only decode what can end up in the IR (plus resampler support).
  int needed = (int)ceil(maxLen * ratio) + 64 + (int)ceil(kResampleZeroCrossings / 0.97 * (ratio > 1 ? ratio : 1));
  if (needed > scratchCap) needed = scratchCap;
  if ((double)scratchCap < maxLen * ratio * 0.999 && res.sourceFrames > scratchCap) {
    // Source rate so high that maxLen output samples do not fit the buffer.
    *err = "sample rate too high for the load buffer";
    return false;
  }
  int n = readWavSamples(src, res.wav, opt.channels, scratch, needed);
  if (n <= 0) { *err = "read error"; return false; }

  int len = resampleIR(scratch, n, res.wav.sampleRate, opt.targetRate, out, maxLen,
                       opt.kernelTable, opt.kernelTableCap);
  int fullLen = (int)ceil(res.sourceFrames / ratio);

  // Peak for the relative thresholds.
  float peak = 0.0f;
  for (int i = 0; i < len; i++) peak = fmaxf(peak, fabsf(out[i]));
  if (peak <= 0.0f) { *err = "IR is silent"; return false; }

  // Leading silence (pure latency) can be removed.
  if (opt.trimLeadDb < 0.0f) {
    const float th = peak * powf(10.0f, opt.trimLeadDb / 20.0f);
    int start = 0;
    while (start < len && fabsf(out[start]) < th) start++;
    if (start > 0) {
      memmove(out, out + start, (len - start) * sizeof(float));
      len -= start;
      fullLen -= start;
      res.leadTrimmed = start;
    }
  }

  // Cut at maxLen: smooth the edge to avoid a truncation click.
  if (fullLen > len) {
    res.truncated = true;
    int f = opt.truncateFade < len / 4 ? opt.truncateFade : len / 4;
    for (int i = 0; i < f; i++) {
      float g = 0.5f * (1.0f + cosf((float)M_PI * (i + 1) / (float)f));
      out[len - f + i] *= g;
    }
  }

  // Inaudible tail costs CPU for nothing.
  if (opt.trimTailDb < 0.0f) {
    const float th = peak * powf(10.0f, opt.trimTailDb / 20.0f);
    while (len > 1 && fabsf(out[len - 1]) < th) len--;
  }

  float gain = 1.0f;
  if (opt.normalize == Normalize::kEnergy) {
    double e = 0.0;
    for (int i = 0; i < len; i++) e += (double)out[i] * out[i];
    gain = (float)(1.0 / sqrt(e));
  } else if (opt.normalize == Normalize::kPeak) {
    gain = 1.0f / peak;
  }
  if (gain != 1.0f)
    for (int i = 0; i < len; i++) out[i] *= gain;

  res.gain = gain;
  res.length = len;
  return true;
}

}  // namespace irdsp
