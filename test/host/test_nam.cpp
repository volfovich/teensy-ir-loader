// test_nam.cpp - the A2-Lite engine (NamA2.h) against two references:
//
//   - a plain one here, one sample at a time straight from the formulas in
//     NAM.md, reading the weights in .nam file order, with a ring buffer per
//     layer: random weights, every block size, many ring wraps, reset()
//   - NeuralAmpModelerCore, the official implementation: a real A2 model
//     (nam/A2.nam, read with NamFile.h) on a test signal, against its output
//     in nam/A2_lite_ref.f32 (made by nam/make_reference.sh, which runs
//     `test_nam --write-input <file.wav>` for the signal)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "AmpStage.h"
#include "NamA2.h"
#include "NamFile.h"

using namespace irdsp;

static int g_fail = 0;
#define CHECK(cond, ...)                \
  do {                                  \
    if (!(cond)) {                      \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);              \
      printf("\n");                     \
      g_fail++;                         \
    }                                   \
  } while (0)

// ---- Random numbers -------------------------------------------------------------

static uint32_t g_rng = 12345;
static float urand() {  // -1 .. 1
  g_rng = g_rng * 1664525u + 1013904223u;
  return (float)(g_rng >> 8) / 8388608.0f - 1.0f;
}

// Random weights in .nam order, scaled roughly as a trained network's: each
// sum of n products gets about unit gain.
static std::vector<float> randomWeights() {
  constexpr int C = nama2::kChannels;
  std::vector<float> w;
  auto put = [&](int n, float scale) {
    for (int i = 0; i < n; i++) w.push_back(urand() * scale);
  };
  put(C, 1.0f);
  for (int l = 0; l < nama2::kLayers; l++) {
    const int K = nama2::kKernel[l];
    put(K * C * C, 1.0f / sqrtf((float)(K * C)));
    put(C, 0.1f);
    put(C, 0.5f);
    put(C * C, 0.3f);
    put(C, 0.05f);
  }
  put(nama2::kHeadTaps * C, 0.1f);
  put(1, 0.05f);
  put(1, 0.5f);
  return w;
}

// ---- The reference --------------------------------------------------------------

struct Reference {
  static constexpr int C = nama2::kChannels;
  std::vector<float> w;
  std::vector<std::vector<float>> ring;  // per layer: (reach + 1) frames x C
  std::vector<float> headRing;           // 16 frames x C
  long t = 0;

  explicit Reference(const std::vector<float>& weights) : w(weights) {
    for (int l = 0; l < nama2::kLayers; l++) ring.emplace_back((nama2::reach(l) + 1) * C, 0.0f);
    headRing.assign(nama2::kHeadTaps * C, 0.0f);
  }

  float step(float in) {
    const float* p = w.data();
    float x[C];
    for (int c = 0; c < C; c++) x[c] = p[c] * in;
    p += C;
    float hs[C] = {0, 0, 0};
    for (int l = 0; l < nama2::kLayers; l++) {
      const int K = nama2::kKernel[l], D = nama2::kDilation[l], R = nama2::reach(l) + 1;
      std::vector<float>& r = ring[l];
      for (int c = 0; c < C; c++) r[(t % R) * C + c] = x[c];
      float z[C] = {0, 0, 0};
      for (int o = 0; o < C; o++)
        for (int i = 0; i < C; i++)
          for (int k = 0; k < K; k++) {
            const long back = t - (long)(K - 1 - k) * D;
            const float v = back < 0 ? 0.0f : r[(back % R) * C + i];
            z[o] += p[(o * C + i) * K + k] * v;
          }
      p += K * C * C;
      for (int o = 0; o < C; o++) z[o] += p[o];
      p += C;
      for (int o = 0; o < C; o++) z[o] += p[o] * in;
      p += C;
      float a[C];
      for (int o = 0; o < C; o++) {
        a[o] = z[o] > 0.0f ? z[o] : 0.01f * z[o];
        hs[o] += a[o];
      }
      const float* L = p;
      p += C * C;
      for (int o = 0; o < C; o++) {
        float s = p[o];
        for (int i = 0; i < C; i++) s += L[o * C + i] * a[i];
        x[o] += s;
      }
      p += C;
    }
    const int T = nama2::kHeadTaps;
    for (int c = 0; c < C; c++) headRing[(t % T) * C + c] = hs[c];
    float y = p[T * C];
    for (int c = 0; c < C; c++)
      for (int k = 0; k < T; k++) {
        const long back = t - (T - 1 - k);
        if (back >= 0) y += p[c * T + k] * headRing[(back % T) * C + c];
      }
    y *= p[T * C + 1];
    t++;
    return y;
  }
};

// ---- Tests ----------------------------------------------------------------------

class FileSource : public ByteSource {
 public:
  explicit FileSource(FILE* f) : f_(f) {}
  int read(void* dst, int n) override { return (int)fread(dst, 1, n, f_); }
  bool seek(uint32_t pos) override { return fseek(f_, (long)pos, SEEK_SET) == 0; }

 private:
  FILE* f_;
};

// The golden test's signal, 1 s at 48 kHz: two plucked notes, a sweep and
// noise bursts at three levels.
static std::vector<float> goldenSignal() {
  const int fs = 48000;
  std::vector<float> x(fs, 0.0f);
  const float notes[2][2] = {{0.0f, 82.41f}, {0.25f, 110.0f}};
  for (const auto& n : notes) {
    const int s0 = (int)(n[0] * fs);
    for (int i = 0; s0 + i < fs / 2; i++) {
      const float t = (float)i / fs;
      float v = 0.0f;
      for (int h = 1; h < 8; h++) v += sinf(2.0f * 3.14159265f * n[1] * h * t) / h;
      x[s0 + i] += 0.35f * v * expf(-6.0f * t);
    }
  }
  for (int i = 0; i < fs / 4; i++) {  // 50 Hz .. 8 kHz
    const float t = (float)i / (fs / 4);
    x[fs / 2 + i] += 0.3f * sinf(2.0f * 3.14159265f * 50.0f * (powf(160.0f, t) - 1.0f) / logf(160.0f) * 0.25f);
  }
  uint32_t rng = 7;
  const float levels[3] = {0.02f, 0.2f, 0.8f};
  for (int b = 0; b < 3; b++)
    for (int i = 0; i < fs / 12; i++) {
      rng = rng * 1664525u + 1013904223u;
      x[3 * fs / 4 + b * fs / 12 + i] += levels[b] * ((float)(rng >> 8) / 8388608.0f - 1.0f);
    }
  return x;
}

static bool writeWav(const char* path, const std::vector<float>& x) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  const uint32_t data = (uint32_t)(x.size() * 4), riff = 36 + data, fmtLen = 16, rate = 48000, bytes = rate * 4;
  const uint16_t fmt = 3, ch = 1, align = 4, bits = 32;
  fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmtLen, 4, 1, f);
  fwrite(&fmt, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&rate, 4, 1, f), fwrite(&bytes, 4, 1, f);
  fwrite(&align, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
  fwrite(x.data(), 4, x.size(), f);
  return fclose(f) == 0;
}

// The amp stage: dry while warming up, then exactly the network (with its
// gains); a new model fades to silence, warms up silent, fades in; off goes
// back to dry and idle. No clicks anywhere.
static void testAmpStage(const std::vector<float>& w1) {
  printf("  amp stage: on, new model, off\n");
  constexpr int B = 32;
  using Stage = AmpStage<B>;
  std::vector<float> w2 = w1;
  for (size_t i = 0; i < w2.size(); i += 7) w2[i] *= -0.8f;  // another model
  std::vector<float> hist(Stage::kHistoryFloats), refHist(Stage::kHistoryFloats);
  Stage* st = new Stage(48000.0f);
  NamA2<B>* ref = new NamA2<B>;
  st->begin(hist.data());
  ref->begin(refHist.data());
  const float gIn = 1.5f, gOut = 0.7f;
  st->setGains(gIn, gOut);

  long t = 0;  // samples
  auto sig = [](long n) { return 0.3f * sinf(2.0f * 3.14159265f * 220.0f * (float)n / 48000.0f); };
  float in[B], out[B], prev = 0.0f, maxStep = 0.0f;
  auto block = [&](bool* idle) {
    for (int i = 0; i < B; i++) in[i] = sig(t + i);
    *idle = !st->process(in, out);
    if (*idle) memcpy(out, in, sizeof(out));
    for (int i = 0; i < B; i++) maxStep = fmaxf(maxStep, fabsf(out[i] - prev)), prev = out[i];
    t += B;
  };
  bool idle;
  block(&idle);
  CHECK(idle, "no model: idle");
  st->loadWeights(w1.data());
  block(&idle);
  CHECK(idle && st->weightsTaken() && st->hasModel(), "a model while off: taken at once, still idle");

  // On: dry for the warm-up (the reference network starts with it).
  st->setOn(true);
  ref->reset();
  ref->setWeights(w1.data());
  bool dry = true;
  float r[B], x[B];
  for (int b = 0; b < Stage::kWarmBlocks; b++) {
    block(&idle);
    for (int i = 0; i < B; i++) dry = dry && out[i] == in[i], x[i] = in[i] * gIn;
    ref->process(x, r);
  }
  CHECK(dry && !idle, "on: the dry signal during the warm-up (%d blocks)", Stage::kWarmBlocks);
  // Then a 20 ms crossfade, then the network times the output gain.
  for (int b = 0; b < 30; b++) {
    block(&idle);
    for (int i = 0; i < B; i++) x[i] = in[i] * gIn;
    ref->process(x, r);
  }
  double err = 0, level = 0;
  for (int b = 0; b < 100; b++) {
    block(&idle);
    for (int i = 0; i < B; i++) x[i] = in[i] * gIn;
    ref->process(x, r);
    for (int i = 0; i < B; i++) err = fmax(err, fabs(out[i] - gOut * r[i])), level = fmax(level, fabs(out[i]));
  }
  CHECK(err < 1e-6 && level > 0.01, "  ... then exactly the network with its gains (error %g)", err);
  const float steadyStep = maxStep;

  // A new model while on: silence within 20 ms, a silent warm-up, then in.
  st->loadWeights(w2.data());
  int silentFrom = -1, blocks = 0;
  for (; blocks < 40 && silentFrom < 0; blocks++) {
    block(&idle);
    bool silent = true;
    for (int i = 0; i < B; i++) silent = silent && out[i] == 0.0f;
    if (silent) silentFrom = blocks;
  }
  CHECK(silentFrom >= 0 && silentFrom <= 31 && st->weightsTaken(), "new model: silent within 20 ms (%d blocks)",
        silentFrom);
  ref->reset();
  ref->setWeights(w2.data());
  int silentBlocks = 1;
  for (;; silentBlocks++) {
    block(&idle);
    for (int i = 0; i < B; i++) x[i] = in[i] * gIn;
    ref->process(x, r);
    bool silent = true;
    for (int i = 0; i < B; i++) silent = silent && out[i] == 0.0f;
    if (!silent || silentBlocks > 400) break;
  }
  CHECK(silentBlocks >= Stage::kWarmBlocks && silentBlocks <= Stage::kWarmBlocks + 2,
        "  ... the warm-up in silence (%d blocks, ~%.0f ms)", silentBlocks, silentBlocks * B / 48.0);
  for (int b = 0; b < 30; b++) {
    block(&idle);
    for (int i = 0; i < B; i++) x[i] = in[i] * gIn;
    ref->process(x, r);
  }
  err = 0;
  for (int b = 0; b < 100; b++) {
    block(&idle);
    for (int i = 0; i < B; i++) x[i] = in[i] * gIn;
    ref->process(x, r);
    for (int i = 0; i < B; i++) err = fmax(err, fabs(out[i] - gOut * r[i]));
  }
  CHECK(err < 1e-6, "  ... then the new model (error %g)", err);

  // Off: back to dry within 20 ms, then idle.
  st->setOn(false);
  int idleAfter = -1;
  for (int b = 0; b < 60 && idleAfter < 0; b++) {
    block(&idle);
    if (idle) idleAfter = b;
  }
  CHECK(idleAfter >= 0 && idleAfter <= 32 && !st->active(), "off: back to dry and idle (%d blocks)", idleAfter);
  CHECK(maxStep < 2.0f * fmaxf(steadyStep, 0.3f * 2.0f * 3.14159265f * 220.0f / 48000.0f) + 0.02f,
        "no clicks at any change (largest step %g, steady %g)", maxStep, steadyStep);
  delete st;
  delete ref;
}

// The noise gate: the input's noise floor alone gives silence; a note opens
// it within a block or two; a note dying into the noise is faded out, never
// cut; gate off leaves the amp as it was.
static void testAmpGate(const std::vector<float>& w) {
  printf("  amp stage: the noise gate\n");
  constexpr int B = 32;
  using Stage = AmpStage<B>;
  std::vector<float> hist(Stage::kHistoryFloats);
  Stage* st = new Stage(48000.0f);
  st->begin(hist.data());
  st->loadWeights(w.data());
  st->setGate(powf(10.0f, -64.0f / 20.0f));
  st->setOn(true);
  const float noise = powf(10.0f, -75.0f / 20.0f);
  float in[B], out[B];
  long t = 0;
  auto run = [&](int blocks, float note, float decay, int* openedAt) {
    bool silent = true;
    for (int b = 0; b < blocks; b++) {
      for (int i = 0; i < B; i++, t++)
        in[i] = noise * urand() + note * powf(decay, (float)b) * sinf(2.0f * 3.14159265f * 220.0f * (float)t / 48000.0f);
      const float before = st->gateGain();
      st->process(in, out);
      if (openedAt && *openedAt < 0 && st->gateGain() == 1.0f) *openedAt = b;
      // Closing: never faster than the release (50 ms to -60 dB).
      if (st->gateGain() < before && st->gateGain() > 0.0f)
        silent = silent && st->gateGain() >= before * expf(-6.9078f * (float)B / (48000.0f * 0.05f)) * 0.999f;
      else if (b >= blocks - 150)  // the last 100 ms
        for (int i = 0; i < B; i++) silent = silent && out[i] == 0.0f;
    }
    return silent;
  };
  CHECK(run(Stage::kWarmBlocks + 1500, 0.0f, 1.0f, nullptr), "noise at -75 dBFS alone: silence");
  int opened = -1;
  run(30, 0.1f, 1.0f, &opened);
  CHECK(opened >= 0 && opened <= 1, "a note at -20 dBFS opens it within 2 blocks (%d)", opened);
  bool open = true;
  for (int b = 0; b < 20; b++) open = open && (run(1, 0.1f, 1.0f, nullptr), st->gateGain() == 1.0f);
  CHECK(open, "  ... and it stays open");
  CHECK(run(3000, 0.1f, 0.99f, nullptr) && st->gateGain() == 0.0f,
        "a note dying into the noise: faded out at the release rate, then silence");
  // Closed, with noise peaking near -73 dBFS and short ticks (pick and
  // string noise) to -62 dBFS: it stays shut (the old peak detector
  // chattered here).
  int reopened = 0;
  for (int b = 0; b < 3000; b++) {
    for (int i = 0; i < B; i++, t++) {
      float g = 0.0f;
      for (int k = 0; k < 4; k++) g += urand();
      in[i] = noise * 1.25f * 0.33f * g + (t % 33600 < 24 ? 8e-4f * sinf((float)t * 0.9f) : 0.0f);
    }
    st->process(in, out);
    if (st->gateGain() > 0.0f) reopened++;
  }
  CHECK(reopened == 0, "closed: noise spikes and small ticks don't reopen it (%d blocks open)", reopened);
  // The release: the time from the first fall to -60 dB.
  for (float rel : {10.0f, 200.0f}) {
    st->setGateRelease(rel);
    run(30, 0.1f, 1.0f, nullptr);
    int from = -1, to = -1;
    for (int b = 0; b < 2000 && to < 0; b++) {
      const float before = st->gateGain();
      run(1, 0.0f, 1.0f, nullptr);
      if (from < 0 && st->gateGain() < before) from = b;
      if (from >= 0 && st->gateGain() <= 1e-3f) to = b;
    }
    const float ms = (float)(to - from + 1) * (float)B / 48.0f;
    CHECK(from >= 0 && to >= 0 && fabsf(ms - rel) <= 0.67f * 1.5f, "release %.0f ms: -60 dB after %.1f ms", rel, ms);
  }
  st->setGateRelease(50.0f);
  st->setGate(0.0f);
  run(1, 0.0f, 1.0f, nullptr);
  CHECK(st->gateGain() == 1.0f, "gate off: open, the noise passes");
  delete st;
}

// The tone controls: flat at 0 dB (untouched), and each knob's gain at its
// ends where it acts most.
static void testToneStack() {
  printf("  tone controls\n");
  const float fs = 48000.0f;
  auto gainDb = [&](const float knobs[4], float hz) {
    ToneStack t(fs);
    t.set(knobs[0], knobs[1], knobs[2], knobs[3]);
    float x[32];
    double in2 = 0, out2 = 0;
    for (int b = 0; b < 600; b++) {
      for (int i = 0; i < 32; i++) x[i] = sinf(2.0f * 3.14159265f * hz * (float)(b * 32 + i) / fs);
      double e = 0;
      for (int i = 0; i < 32; i++) e += (double)x[i] * x[i];
      t.process(x, 32);
      if (b >= 300) {
        in2 += e;
        for (int i = 0; i < 32; i++) out2 += (double)x[i] * x[i];
      }
    }
    return 10.0 * log10(out2 / in2);
  };
  const float flat[4] = {0, 0, 0, 0};
  ToneStack t(fs);
  t.set(0, 0, 0, 0);
  float x[32], y[32];
  for (int i = 0; i < 32; i++) x[i] = y[i] = urand();
  t.process(y, 32);
  CHECK(!memcmp(x, y, sizeof(x)), "all at 0 dB: the signal untouched");
  CHECK(fabs(gainDb(flat, 1000.0f)) < 0.01, "flat at 1 kHz");
  struct Case { int knob; float value, hz, lo, hi; const char* what; } cases[] = {
      {0, 12.0f, 40.0f, 11.0f, 12.5f, "bass +12 dB: +12 dB at 40 Hz"},
      {0, -12.0f, 40.0f, -12.5f, -11.0f, "bass -12 dB: -12 dB at 40 Hz"},
      {1, -10.0f, 650.0f, -10.1f, -9.9f, "middle -10 dB: -10 dB at 650 Hz"},
      {2, 12.0f, 12000.0f, 11.0f, 12.5f, "treble +12 dB: +12 dB at 12 kHz"},
      {3, 10.0f, 5000.0f, 9.9f, 10.1f, "presence +10 dB: +10 dB at 5 kHz"},
      {3, 10.0f, 200.0f, -0.1f, 0.3f, "  ... and nothing at 200 Hz"},
  };
  for (const Case& c : cases) {
    float k[4] = {0, 0, 0, 0};
    k[c.knob] = c.value;
    const double g = gainDb(k, c.hz);
    CHECK(g >= c.lo && g <= c.hi, "%s (%.2f dB)", c.what, g);
  }
}

// NamA2 with A2.nam's Lite network against NeuralAmpModelerCore's output.
// Core warms the network up on reset and NamA2 starts from silence, so they
// agree once the receptive field (6,346 samples) has passed.
static void testGolden() {
  printf("  golden: nam/A2.nam (Lite) against NeuralAmpModelerCore\n");
  FILE* f = fopen("nam/A2.nam", "rb");
  CHECK(f, "can't open nam/A2.nam (run from test/host)");
  if (!f) return;
  FileSource src(f);
  std::vector<float> w(nama2::kWeightCount);
  NamInfo info;
  const char* err = "";
  const bool ok = readNamA2Lite(src, w.data(), &info, &err);
  fclose(f);
  CHECK(ok, "reading A2.nam: %s", err);
  if (!ok) return;

  std::vector<float> ref(48000);
  FILE* r = fopen("nam/A2_lite_ref.f32", "rb");
  CHECK(r && fread(ref.data(), 4, ref.size(), r) == ref.size(), "can't read nam/A2_lite_ref.f32");
  if (r) fclose(r);

  const std::vector<float> x = goldenSignal();
  std::vector<float> hist(NamA2<32>::kHistoryFloats), out(x.size());
  NamA2<32>* nam = new NamA2<32>;
  nam->begin(hist.data());
  nam->setWeights(w.data());
  for (size_t i = 0; i < x.size(); i += 32) nam->process(&x[i], &out[i]);
  delete nam;
  double err2 = 0, sig2 = 0, maxErr = 0, peak = 0;
  for (size_t i = 6400; i < x.size(); i++) {
    const double d = out[i] - ref[i];
    err2 += d * d, sig2 += (double)ref[i] * ref[i];
    maxErr = fmax(maxErr, fabs(d)), peak = fmax(peak, fabs(ref[i]));
  }
  const double db = 10.0 * log10(sig2 / (err2 + 1e-30));
  printf("    max error %.2e (output peak %.3f), error %.0f dB below the signal\n", maxErr, peak, db);
  CHECK(peak > 0.05, "the reference is silent");
  CHECK(maxErr < 1e-5 && db > 100.0, "differs from NeuralAmpModelerCore: max %g, %.1f dB", maxErr, db);
}

// A guitar-like test signal: decaying plucks over a little noise.
static std::vector<float> testSignal(int n, float level) {
  std::vector<float> s(n);
  for (int i = 0; i < n; i++) {
    const int ph = i % 9000;
    s[i] = level * (expf(-ph / 3000.0f) * sinf(2.0f * 3.14159265f * 110.0f * i / 48000.0f) * 0.8f + 0.05f * urand());
  }
  return s;
}

template <int B>
static void testBlockSize(const std::vector<float>& w, const std::vector<float>& sig, const std::vector<float>& ref) {
  std::vector<float> hist(NamA2<B>::kHistoryFloats);
  NamA2<B>* nam = new NamA2<B>;
  nam->begin(hist.data());
  nam->setWeights(w.data());

  const int n = (int)sig.size() / B * B;
  std::vector<float> out(n);
  for (int i = 0; i < n; i += B) nam->process(&sig[i], &out[i]);
  double maxErr = 0, peak = 0;
  for (int i = 0; i < n; i++) {
    maxErr = fmax(maxErr, fabs(out[i] - ref[i]));
    peak = fmax(peak, fabs(ref[i]));
  }
  CHECK(peak > 0.01, "B=%d: the reference is silent (peak %g)", B, peak);
  CHECK(maxErr <= 1e-5 * fmax(1.0, peak), "B=%d: max error %g (peak %g)", B, maxErr, peak);
  printf("  B=%3d: max error %.2e, peak %.3f, history %d KB\n", B, maxErr, peak,
         (int)(NamA2<B>::kHistoryFloats * sizeof(float) / 1024));

  // reset() gives the same output as a fresh network.
  nam->reset();
  const int m = 4096 / B * B;
  std::vector<float> again(m);
  for (int i = 0; i < m; i += B) nam->process(&sig[i], &again[i]);
  double resetErr = 0;
  for (int i = 0; i < m; i++) resetErr = fmax(resetErr, fabs(again[i] - out[i]));
  CHECK(resetErr == 0.0, "B=%d: after reset() the output differs by %g", B, resetErr);
  delete nam;
}

int main(int argc, char** argv) {
  if (argc == 3 && !strcmp(argv[1], "--write-input")) return writeWav(argv[2], goldenSignal()) ? 0 : 1;
  printf("test_nam\n");
  CHECK(nama2::kWeightCount == 1871, "weight count %d, expected 1871", nama2::kWeightCount);
  printf("  %d weights, %d multiply-adds per sample\n", nama2::kWeightCount, nama2::kMacsPerSample);

  const std::vector<float> w = randomWeights();
  CHECK((int)w.size() == nama2::kWeightCount, "random weights: %d", (int)w.size());

  // 1.5 s: every layer's history is copied back many times.
  const std::vector<float> sig = testSignal(72000, 0.5f);
  Reference r(w);
  std::vector<float> ref(sig.size());
  for (size_t i = 0; i < sig.size(); i++) ref[i] = r.step(sig[i]);

  testBlockSize<16>(w, sig, ref);
  testBlockSize<32>(w, sig, ref);
  testBlockSize<48>(w, sig, ref);
  testBlockSize<64>(w, sig, ref);
  testBlockSize<128>(w, sig, ref);

  // 60 s of loud noise stays finite.
  {
    std::vector<float> hist(NamA2<32>::kHistoryFloats);
    NamA2<32>* nam = new NamA2<32>;
    nam->begin(hist.data());
    nam->setWeights(w.data());
    float in[32], out[32];
    bool finite = true;
    for (int b = 0; b < 60 * 48000 / 32; b++) {
      for (int i = 0; i < 32; i++) in[i] = urand();
      nam->process(in, out);
      for (int i = 0; i < 32; i++) finite = finite && isfinite(out[i]);
    }
    CHECK(finite, "loud noise: output not finite");
    delete nam;
  }

  testGolden();
  testAmpStage(w);
  testAmpGate(w);
  testToneStack();

  printf(g_fail ? "test_nam: %d FAILED\n" : "test_nam: all passed\n", g_fail);
  return g_fail ? 1 : 0;
}
