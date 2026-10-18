// AudioConvolverIR.h - Teensy Audio Library object wrapping the convolver.
//
// One mono input (guitar), one mono output (connect it to both I2S channels).
// Adds smoothed output gain, a click-free bypass, peak meters, a clip counter
// and CPU measurement of its own update().
#pragma once

#include <Arduino.h>
#include <AudioStream.h>
#include <math.h>

#include "PartitionedConvolver.h"
#include "config.h"

class AudioConvolverIR : public AudioStream {
 public:
  using Engine = irdsp::PartitionedConvolver<AUDIO_BLOCK_SAMPLES, IR_MAX_LENGTH>;

  AudioConvolverIR() : AudioStream(1, inputQueue_) {}

  Engine& engine() { return engine_; }

  void setOutputGainDb(float db) { gainTarget_ = powf(10.0f, db / 20.0f); }
  void setBypass(bool on) { wetTarget_ = on ? 0.0f : 1.0f; }
  bool bypassed() const { return wetTarget_ < 0.5f; }

  // Meters: return the peak (linear, 1.0 = full scale) since the last call.
  float takeInputPeak() { float p = inPeak_; inPeak_ = 0.0f; return p; }
  float takeOutputPeak() { float p = outPeak_; outPeak_ = 0.0f; return p; }
  uint32_t clipCount() const { return clips_; }

  // CPU load of this object, in percent of one core.
  float cpuPercent() const { return cyclesToPercent(cyclesAvg_); }
  float cpuPercentMax() const { return cyclesToPercent(cyclesMax_); }
  void resetCpuMax() { cyclesMax_ = 0; }

  void update(void) override {
    const uint32_t t0 = ARM_DWT_CYCCNT;
    constexpr int B = AUDIO_BLOCK_SAMPLES;
    float x[B], y[B];

    audio_block_t* in = receiveReadOnly(0);
    float pk = inPeak_;
    if (in) {
      for (int i = 0; i < B; i++) {
        x[i] = in->data[i] * (1.0f / 32768.0f);
        pk = fmaxf(pk, fabsf(x[i]));
      }
      release(in);
    } else {
      // No input yet: keep processing silence so the IR tail decays.
      for (int i = 0; i < B; i++) x[i] = 0.0f;
    }
    inPeak_ = pk;

    engine_.process(x, y);

    audio_block_t* out = allocate();
    if (!out) return;

    // Per-sample linear ramps for gain and bypass (no zipper noise).
    const float g0 = gainCur_, g1 = gainTarget_;
    const float w0 = wetCur_;
    const float wStep = (float)B / (AUDIO_SAMPLE_RATE_EXACT * 0.02f);  // 20 ms fade
    float w1 = wetTarget_ > w0 ? fminf(w0 + wStep, wetTarget_) : fmaxf(w0 - wStep, wetTarget_);
    float opk = outPeak_;
    uint32_t clips = 0;
    for (int i = 0; i < B; i++) {
      const float a = (float)(i + 1) / (float)B;
      const float g = g0 + (g1 - g0) * a;
      const float w = w0 + (w1 - w0) * a;
      // Gain applies to the processed signal; bypass passes the dry input.
      float v = w * g * y[i] + (1.0f - w) * x[i];
      float av = fabsf(v);
      opk = fmaxf(opk, av);
      if (av > 0.99997f) {
        clips++;
        v = v > 0 ? 0.99997f : -0.99997f;
      }
      out->data[i] = (int16_t)lrintf(v * 32767.0f);
    }
    gainCur_ = g1;
    wetCur_ = w1;
    outPeak_ = opk;
    clips_ += clips;

    transmit(out, 0);
    release(out);

    const uint32_t c = ARM_DWT_CYCCNT - t0;
    cyclesAvg_ = cyclesAvg_ + (c - cyclesAvg_) * 0.01f;
    if (c > cyclesMax_) cyclesMax_ = c;
  }

 private:
  static float cyclesToPercent(float cycles) {
    const float cyclesPerBlock = (float)F_CPU_ACTUAL * AUDIO_BLOCK_SAMPLES / AUDIO_SAMPLE_RATE_EXACT;
    return 100.0f * cycles / cyclesPerBlock;
  }

  audio_block_t* inputQueue_[1];
  Engine engine_;
  volatile float gainTarget_ = 0.5f, gainCur_ = 0.5f;
  volatile float wetTarget_ = 1.0f, wetCur_ = 1.0f;
  volatile float inPeak_ = 0.0f, outPeak_ = 0.0f;
  volatile uint32_t clips_ = 0;
  volatile float cyclesAvg_ = 0.0f;
  volatile uint32_t cyclesMax_ = 0;
};
