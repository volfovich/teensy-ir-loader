// nam_bench.cpp - milestone M0 of NAM.md: the A2-Lite engine's CPU cost on
// the Teensy 4.0, alone, with random weights.
//
//   pio run -e nambench -t upload, then read the serial port.
//
// Times NamA2<32>::process() with the cycle counter, the history once in
// DTCM (the plan) and once in OCRAM (the fallback), and prints the cost as a
// share of the CPU at 48 and 44.1 kHz.
#include <Arduino.h>

#include "NamA2.h"

constexpr int B = 32;
using Nam = irdsp::NamA2<B>;

static Nam g_namDtcm, g_namOcram;
static float g_histDtcm[Nam::kHistoryFloats];
DMAMEM static float g_histOcram[Nam::kHistoryFloats];
static float g_weights[irdsp::nama2::kWeightCount];

static uint32_t g_rng = 12345;
static float urand() {  // -1 .. 1
  g_rng = g_rng * 1664525u + 1013904223u;
  return (float)(g_rng >> 8) / 8388608.0f - 1.0f;
}

// The same scaling as test/host/test_nam.cpp.
static void randomWeights() {
  constexpr int C = irdsp::nama2::kChannels;
  int n = 0;
  auto put = [&](int count, float scale) {
    for (int i = 0; i < count; i++) g_weights[n++] = urand() * scale;
  };
  put(C, 1.0f);
  for (int l = 0; l < irdsp::nama2::kLayers; l++) {
    const int K = irdsp::nama2::kKernel[l];
    put(K * C * C, 1.0f / sqrtf((float)(K * C)));
    put(C, 0.1f);
    put(C, 0.5f);
    put(C * C, 0.3f);
    put(C, 0.05f);
  }
  put(irdsp::nama2::kHeadTaps * C, 0.1f);
  put(1, 0.05f);
  put(1, 0.5f);
}

static void bench(const char* name, Nam& nam) {
  constexpr int kBlocks = 1500;  // 1 s at 48 kHz
  float in[B], out[B];
  uint64_t total = 0;
  uint32_t lo = 0xFFFFFFFF, hi = 0;
  float sum = 0.0f;
  for (int b = 0; b < kBlocks; b++) {
    for (int i = 0; i < B; i++) in[i] = 0.5f * urand();
    const uint32_t t0 = ARM_DWT_CYCCNT;
    nam.process(in, out);
    const uint32_t c = ARM_DWT_CYCCNT - t0;
    total += c;
    lo = min(lo, c);
    hi = max(hi, c);
    sum += out[0];
  }
  const float avg = (float)total / kBlocks;
  const float per48 = (float)F_CPU_ACTUAL * B / 48000.0f, per44 = (float)F_CPU_ACTUAL * B / 44117.647f;
  Serial.printf("%-6s %6.0f cycles/block (min %lu, max %lu), %.2f cycles/MAC, %.1f %% at 48 kHz, %.1f %% at 44.1 kHz  [%g]\n",
                name, avg, (unsigned long)lo, (unsigned long)hi, avg / (B * irdsp::nama2::kMacsPerSample),
                100.0f * avg / per48, 100.0f * avg / per44, sum);
}

void setup() {
  randomWeights();
  g_namDtcm.begin(g_histDtcm);
  g_namDtcm.setWeights(g_weights);
  g_namOcram.begin(g_histOcram);
  g_namOcram.setWeights(g_weights);
}

void loop() {
  Serial.printf("\nNamA2<%d> at %lu MHz: %d weights, %d MAC/sample, history %d KB\n", B,
                (unsigned long)(F_CPU_ACTUAL / 1000000), irdsp::nama2::kWeightCount, irdsp::nama2::kMacsPerSample,
                (int)(sizeof(g_histDtcm) / 1024));
  bench("DTCM", g_namDtcm);
  bench("OCRAM", g_namOcram);
  delay(3000);
}
