// nam_micro.cpp - M0 follow-up: loop shapes for A2-Lite's dilated 3x3
// convolution on the Cortex-M7, timed alone (one 6-tap layer, 32 frames).
//
//   pio run -e nammicro -t upload, then read the serial port.
#include <Arduino.h>

constexpr int B = 32, C = 3, K = 6, D = 17;
constexpr int kReach = (K - 1) * D;

static float g_hist[(kReach + B) * C];
static float g_w[K * C * C];  // [tap][out][in]
static float g_z[B * C];
static volatile float g_sink;

static uint32_t g_rng = 1;
static float urand() {
  g_rng = g_rng * 1664525u + 1013904223u;
  return (float)(g_rng >> 8) / 8388608.0f - 1.0f;
}

// A: the engine today: two taps per pass over the block, z in memory.
__attribute__((noinline)) static void tapsA(float* z, const float* w, const float* cur) {
  for (int k = 0; k < K; k += 2) {
    const float* wa = w + k * 9;
    const float* a = cur - (K - 1 - k) * D * C;
    const float* b = cur - (K - 2 - k) * D * C;
    const float a00 = wa[0], a01 = wa[1], a02 = wa[2], a10 = wa[3], a11 = wa[4], a12 = wa[5], a20 = wa[6],
                a21 = wa[7], a22 = wa[8];
    const float b00 = wa[9], b01 = wa[10], b02 = wa[11], b10 = wa[12], b11 = wa[13], b12 = wa[14], b20 = wa[15],
                b21 = wa[16], b22 = wa[17];
    for (int f = 0; f < B; f++) {
      float z0 = z[f * C], z1 = z[f * C + 1], z2 = z[f * C + 2];
      const float x0 = a[f * C], x1 = a[f * C + 1], x2 = a[f * C + 2];
      const float y0 = b[f * C], y1 = b[f * C + 1], y2 = b[f * C + 2];
      z0 += a00 * x0; z1 += a10 * x0; z2 += a20 * x0;
      z0 += a01 * x1; z1 += a11 * x1; z2 += a21 * x1;
      z0 += a02 * x2; z1 += a12 * x2; z2 += a22 * x2;
      z0 += b00 * y0; z1 += b10 * y0; z2 += b20 * y0;
      z0 += b01 * y1; z1 += b11 * y1; z2 += b21 * y1;
      z0 += b02 * y2; z1 += b12 * y2; z2 += b22 * y2;
      z[f * C] = z0; z[f * C + 1] = z1; z[f * C + 2] = z2;
    }
  }
}

// B: frames outer, all taps inner; z in registers, weights loaded per frame.
__attribute__((noinline)) static void tapsB(float* z, const float* w, const float* cur) {
  for (int f = 0; f < B; f++) {
    float z0 = z[f * C], z1 = z[f * C + 1], z2 = z[f * C + 2];
    for (int k = 0; k < K; k++) {
      const float* x = cur - (K - 1 - k) * D * C + f * C;
      const float* v = w + k * 9;
      z0 += v[0] * x[0]; z1 += v[3] * x[0]; z2 += v[6] * x[0];
      z0 += v[1] * x[1]; z1 += v[4] * x[1]; z2 += v[7] * x[1];
      z0 += v[2] * x[2]; z1 += v[5] * x[2]; z2 += v[8] * x[2];
    }
    z[f * C] = z0; z[f * C + 1] = z1; z[f * C + 2] = z2;
  }
}

// C: like B, two frames at a time: 6 independent sums, weights loaded once
// per pair.
__attribute__((noinline)) static void tapsC(float* z, const float* w, const float* cur) {
  for (int f = 0; f < B; f += 2) {
    float p0 = z[f * C], p1 = z[f * C + 1], p2 = z[f * C + 2];
    float q0 = z[f * C + 3], q1 = z[f * C + 4], q2 = z[f * C + 5];
    for (int k = 0; k < K; k++) {
      const float* x = cur - (K - 1 - k) * D * C + f * C;
      const float* v = w + k * 9;
      const float w00 = v[0], w01 = v[1], w02 = v[2], w10 = v[3], w11 = v[4], w12 = v[5], w20 = v[6], w21 = v[7],
                  w22 = v[8];
      const float x0 = x[0], x1 = x[1], x2 = x[2], y0 = x[3], y1 = x[4], y2 = x[5];
      p0 += w00 * x0; p1 += w10 * x0; p2 += w20 * x0;
      q0 += w00 * y0; q1 += w10 * y0; q2 += w20 * y0;
      p0 += w01 * x1; p1 += w11 * x1; p2 += w21 * x1;
      q0 += w01 * y1; q1 += w11 * y1; q2 += w21 * y1;
      p0 += w02 * x2; p1 += w12 * x2; p2 += w22 * x2;
      q0 += w02 * y2; q1 += w12 * y2; q2 += w22 * y2;
    }
    z[f * C] = p0; z[f * C + 1] = p1; z[f * C + 2] = p2;
    z[f * C + 3] = q0; z[f * C + 4] = q1; z[f * C + 5] = q2;
  }
}

// D: four frames at a time: 12 independent sums.
__attribute__((noinline)) static void tapsD(float* z, const float* w, const float* cur) {
  for (int f = 0; f < B; f += 4) {
    float s[12];
    for (int i = 0; i < 12; i++) s[i] = z[f * C + i];
    for (int k = 0; k < K; k++) {
      const float* x = cur - (K - 1 - k) * D * C + f * C;
      const float* v = w + k * 9;
      const float w00 = v[0], w01 = v[1], w02 = v[2], w10 = v[3], w11 = v[4], w12 = v[5], w20 = v[6], w21 = v[7],
                  w22 = v[8];
      for (int g = 0; g < 4; g++) {
        const float x0 = x[g * 3], x1 = x[g * 3 + 1], x2 = x[g * 3 + 2];
        s[g * 3 + 0] += w00 * x0; s[g * 3 + 1] += w10 * x0; s[g * 3 + 2] += w20 * x0;
        s[g * 3 + 0] += w01 * x1; s[g * 3 + 1] += w11 * x1; s[g * 3 + 2] += w21 * x1;
        s[g * 3 + 0] += w02 * x2; s[g * 3 + 1] += w12 * x2; s[g * 3 + 2] += w22 * x2;
      }
    }
    for (int i = 0; i < 12; i++) z[f * C + i] = s[i];
  }
}

// E: the floor: B x K x 9 multiply-adds into 12 independent registers, no
// memory traffic.
__attribute__((noinline)) static void fmaFloor(float* z, const float* w, const float*) {
  float s[12];
  for (int i = 0; i < 12; i++) s[i] = z[i];
  const float a = w[0], b = w[1];
  for (int n = 0; n < B * K * 9 / 12; n++)
    for (int i = 0; i < 12; i++) s[i] += a * s[i] + b;
  for (int i = 0; i < 12; i++) z[i] = s[i];
}


// F<N>: N independent multiply-add chains in registers, no memory: shows
// the FMA latency (few chains) and throughput (many chains).
template <int N>
__attribute__((noinline)) static void fmaChains(float* z, const float* w, const float*) {
  float s0 = z[0], s1 = z[1], s2 = z[2], s3 = z[3], s4 = z[4], s5 = z[5], s6 = z[6], s7 = z[7];
  const float a = w[0], x = w[1];
  for (int n = 0; n < B * K * 9 / N; n++) {
    s0 += a * x;
    if (N > 1) s1 += a * x;
    if (N > 2) s2 += a * x;
    if (N > 3) s3 += a * x;
    if (N > 4) s4 += a * x;
    if (N > 5) s5 += a * x;
    if (N > 6) s6 += a * x;
    if (N > 7) s7 += a * x;
    asm volatile("" : "+t"(s0), "+t"(s1), "+t"(s2), "+t"(s3), "+t"(s4), "+t"(s5), "+t"(s6), "+t"(s7));
  }
  z[0] = s0 + s1 + s2 + s3 + s4 + s5 + s6 + s7;
}

// G: 3 chains, each multiply-add fed by a fresh load (the tap loop's ratio
// without z traffic).
__attribute__((noinline)) static void fmaLoads(float* z, const float* w, const float* cur) {
  float s0 = z[0], s1 = z[1], s2 = z[2];
  const float a = w[0];
  const float* p = cur - kReach * C;
  for (int n = 0; n < B * K * 3; n++) {
    s0 += a * p[3 * (n & 63)];
    s1 += a * p[3 * (n & 63) + 1];
    s2 += a * p[3 * (n & 63) + 2];
  }
  z[0] = s0 + s1 + s2;
}


// H: the pair loop in assembly: VLDM/VSTM move three floats and advance the
// pointer in one instruction each; 18 weights in s0-s17.
__attribute__((noinline)) static void pairAsm(float* z, const float* w, const float* a, const float* b, int n) {
  asm volatile(
      "vldmia %[w], {s0-s17}\n"
      "1:\n"
      "vldmia %[z], {s24-s26}\n"
      "vldmia %[a]!, {s18-s20}\n"
      "vldmia %[b]!, {s21-s23}\n"
      "vfma.f32 s24, s0, s18\n"
      "vfma.f32 s25, s3, s18\n"
      "vfma.f32 s26, s6, s18\n"
      "vfma.f32 s24, s1, s19\n"
      "vfma.f32 s25, s4, s19\n"
      "vfma.f32 s26, s7, s19\n"
      "vfma.f32 s24, s2, s20\n"
      "vfma.f32 s25, s5, s20\n"
      "vfma.f32 s26, s8, s20\n"
      "vfma.f32 s24, s9, s21\n"
      "vfma.f32 s25, s12, s21\n"
      "vfma.f32 s26, s15, s21\n"
      "vfma.f32 s24, s10, s22\n"
      "vfma.f32 s25, s13, s22\n"
      "vfma.f32 s26, s16, s22\n"
      "vfma.f32 s24, s11, s23\n"
      "vfma.f32 s25, s14, s23\n"
      "vfma.f32 s26, s17, s23\n"
      "vstmia %[z]!, {s24-s26}\n"
      "subs %[n], %[n], #1\n"
      "bne 1b\n"
      : [z] "+r"(z), [a] "+r"(a), [b] "+r"(b), [n] "+r"(n)
      : [w] "r"(w)
      : "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s12", "s13", "s14", "s15",
        "s16", "s17", "s18", "s19", "s20", "s21", "s22", "s23", "s24", "s25", "s26", "memory", "cc");
}

// I: the same, two frames per iteration (6 sums, loads further from use).
__attribute__((noinline)) static void pairAsm2(float* z, const float* w, const float* a, const float* b, int n) {
  asm volatile(
      "vldmia %[w], {s0-s17}\n"
      "1:\n"
      "vldmia %[z], {s26-s31}\n"
      "vldmia %[a]!, {s18-s20}\n"
      "vldmia %[b]!, {s21-s23}\n"
      "vfma.f32 s26, s0, s18\n"
      "vfma.f32 s27, s3, s18\n"
      "vfma.f32 s28, s6, s18\n"
      "vfma.f32 s26, s1, s19\n"
      "vfma.f32 s27, s4, s19\n"
      "vfma.f32 s28, s7, s19\n"
      "vfma.f32 s26, s2, s20\n"
      "vfma.f32 s27, s5, s20\n"
      "vfma.f32 s28, s8, s20\n"
      "vldmia %[a]!, {s18-s20}\n"
      "vfma.f32 s26, s9, s21\n"
      "vfma.f32 s27, s12, s21\n"
      "vfma.f32 s28, s15, s21\n"
      "vfma.f32 s26, s10, s22\n"
      "vfma.f32 s27, s13, s22\n"
      "vfma.f32 s28, s16, s22\n"
      "vfma.f32 s26, s11, s23\n"
      "vfma.f32 s27, s14, s23\n"
      "vfma.f32 s28, s17, s23\n"
      "vldmia %[b]!, {s21-s23}\n"
      "vfma.f32 s29, s0, s18\n"
      "vfma.f32 s30, s3, s18\n"
      "vfma.f32 s31, s6, s18\n"
      "vfma.f32 s29, s1, s19\n"
      "vfma.f32 s30, s4, s19\n"
      "vfma.f32 s31, s7, s19\n"
      "vfma.f32 s29, s2, s20\n"
      "vfma.f32 s30, s5, s20\n"
      "vfma.f32 s31, s8, s20\n"
      "vfma.f32 s29, s9, s21\n"
      "vfma.f32 s30, s12, s21\n"
      "vfma.f32 s31, s15, s21\n"
      "vfma.f32 s29, s10, s22\n"
      "vfma.f32 s30, s13, s22\n"
      "vfma.f32 s31, s16, s22\n"
      "vfma.f32 s29, s11, s23\n"
      "vfma.f32 s30, s14, s23\n"
      "vfma.f32 s31, s17, s23\n"
      "vstmia %[z]!, {s26-s31}\n"
      "subs %[n], %[n], #2\n"
      "bne 1b\n"
      : [z] "+r"(z), [a] "+r"(a), [b] "+r"(b), [n] "+r"(n)
      : [w] "r"(w)
      : "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s12", "s13", "s14", "s15",
        "s16", "s17", "s18", "s19", "s20", "s21", "s22", "s23", "s24", "s25", "s26", "s27", "s28", "s29", "s30",
        "s31", "memory", "cc");
}

template <void (*Pair)(float*, const float*, const float*, const float*, int)>
__attribute__((noinline)) static void tapsAsm(float* z, const float* w, const float* cur) {
  for (int k = 0; k < K; k += 2) Pair(z, w + k * 9, cur - (K - 1 - k) * D * C, cur - (K - 2 - k) * D * C, B);
}

typedef void (*Kernel)(float*, const float*, const float*);

static void run(const char* name, Kernel fn) {
  const float* cur = g_hist + kReach * C;
  uint32_t best = 0xFFFFFFFF;
  for (int rep = 0; rep < 200; rep++) {
    for (int i = 0; i < B * C; i++) g_z[i] = 0.0f;
    const uint32_t t0 = ARM_DWT_CYCCNT;
    fn(g_z, g_w, cur);
    const uint32_t c = ARM_DWT_CYCCNT - t0;
    if (c < best) best = c;
  }
  g_sink = g_z[5];
  const float macs = B * K * 9;
  Serial.printf("%-30s %6lu cycles  %.2f cycles/MAC\n", name, (unsigned long)best, best / macs);
}

void setup() {
  for (float& v : g_hist) v = urand();
  for (float& v : g_w) v = 0.1f * urand();
}

void loop() {
  Serial.printf("\n6-tap 3x3 layer, %d frames, %d MACs\n", B, B * K * 9);
  run("A: 2 taps/pass, z in memory", tapsA);
  run("B: frame outer, 3 sums", tapsB);
  run("C: 2 frames, 6 sums", tapsC);
  run("D: 4 frames, 12 sums", tapsD);
  run("H: asm pair, VLDM/VSTM", tapsAsm<pairAsm>);
  run("I: asm pair, 2 frames", tapsAsm<pairAsm2>);
  run("F1: 1 chain", fmaChains<1>);
  run("F2: 2 chains", fmaChains<2>);
  run("F3: 3 chains", fmaChains<3>);
  run("F4: 4 chains", fmaChains<4>);
  run("F6: 6 chains", fmaChains<6>);
  run("F8: 8 chains", fmaChains<8>);
  run("G: 3 chains, a load each", fmaLoads);
  delay(3000);
}
