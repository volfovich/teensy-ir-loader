// AudioConvolverIR.h - Teensy Audio Library object wrapping the convolver
// and the plate reverb that follows it.
//
// One mono input (guitar), two outputs (0 = left, 1 = right). The whole chain
// runs in 32-bit float inside update():
//
//   in -> IR convolution -> IR bypass blend x output gain = core
//   core -> dry gain ----------------------------+-> out L / R
//   core -> plate reverb (stereo) -> wet gain ---+
//
// Adds smoothed output gain, click-free IR bypass, a reverb with dry/wet mix
// and on/off (the tail rings out when switched off), peak meters, a clip
// counter and CPU measurement of its own update().
#pragma once

#include <Arduino.h>
#include <AudioStream.h>
#include <math.h>

#include "PartitionedConvolver.h"
#include "PlateReverb.h"
#include "config.h"

class AudioConvolverIR : public AudioStream {
 public:
  using Engine = irdsp::PartitionedConvolver<AUDIO_BLOCK_SAMPLES, IR_MAX_LENGTH>;

  AudioConvolverIR() : AudioStream(1, inputQueue_), reverb_(AUDIO_SAMPLE_RATE_EXACT) {}

  Engine& engine() { return engine_; }

  // Hands the reverb its line memory (PlateReverb::kMemFloats floats, may be
  // DMAMEM). Until this is called the reverb stays off.
  void beginReverb(float* mem) {
    __disable_irq();
    reverb_.init(mem);
    __enable_irq();
  }

  void setOutputGainDb(float db) { gainTarget_ = powf(10.0f, db / 20.0f); }
  void setBypass(bool on) { wetTarget_ = on ? 0.0f : 1.0f; }
  bool bypassed() const { return wetTarget_ < 0.5f; }

  // Reverb controls, all 0..1 except on/off. Applied at the next block.
  void setReverbOn(bool on) { rvOn_ = on; }
  void setReverbMix(float m) {
    float wet, dry;
    irdsp::mixPower(m, &wet, &dry);
    rvWetMix_ = wet;
    rvDryMix_ = dry;
  }
  void setReverbSize(float v) { rvSize_ = v; }
  void setReverbTone(float v) { rvTone_ = v; }  // 1 = brightest tail
  void setReverbShape(float diffusion, float loDamp, float lowpass, float hipass) {
    rvDiffusion_ = diffusion; rvLoDamp_ = loDamp; rvLowpass_ = lowpass; rvHipass_ = hipass;
  }
  // True while the reverb is on or its tail is still ringing out.
  bool reverbActive() const { return rvRunning_; }

  // Meters: return the peak (linear, 1.0 = full scale) since the last call.
  float takeInputPeak() { float p = inPeak_; inPeak_ = 0.0f; return p; }
  float takeOutputPeak() { float p = outPeak_; outPeak_ = 0.0f; return p; }
  uint32_t clipCount() const { return clips_; }

  // CPU load of this object, in percent of one core.
  float cpuPercent() const { return cyclesToPercent(cyclesAvg_); }
  float cpuPercentMax() const { return cyclesToPercent(cyclesMax_); }
  // Share of the above spent in the reverb.
  float reverbCpuPercent() const { return cyclesToPercent(rvCyclesAvg_); }
  void resetCpuMax() { cyclesMax_ = 0; }

  void update(void) override {
    const uint32_t t0 = ARM_DWT_CYCCNT;
    constexpr int B = AUDIO_BLOCK_SAMPLES;
    float x[B], y[B], wl[B], wr[B];

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

    // Per-sample linear ramps for gain and bypass (no zipper noise).
    const float g0 = gainCur_, g1 = gainTarget_;
    const float w0 = wetCur_;
    float w1 = ramp(w0, wetTarget_);
    for (int i = 0; i < B; i++) {
      const float a = (float)(i + 1) / (float)B;
      const float g = g0 + (g1 - g0) * a;
      const float w = w0 + (w1 - w0) * a;
      // Gain applies to the processed signal; bypass passes the dry input.
      y[i] = w * g * y[i] + (1.0f - w) * x[i];
    }
    gainCur_ = g1;
    wetCur_ = w1;

    const uint32_t t1 = ARM_DWT_CYCCNT;
    const bool reverb = processReverb(y, wl, wr);
    const uint32_t rc = ARM_DWT_CYCCNT - t1;
    rvCyclesAvg_ = rvCyclesAvg_ + (rc - rvCyclesAvg_) * 0.01f;

    float opk = outPeak_;
    uint32_t clips = 0;
    audio_block_t* outL = allocate();
    if (!outL) return;
    if (!reverb) {
      // Reverb idle: the dry signal goes to both outputs unchanged.
      toInt16(y, outL->data, opk, clips);
      transmit(outL, 0);
      transmit(outL, 1);
      release(outL);
    } else {
      audio_block_t* outR = allocate();
      toInt16(wl, outL->data, opk, clips);
      transmit(outL, 0);
      if (outR) {
        toInt16(wr, outR->data, opk, clips);
        transmit(outR, 1);
        release(outR);
      } else {
        transmit(outL, 1);  // out of audio memory: mono rather than silence
      }
      release(outL);
    }
    outPeak_ = opk;
    clips_ += clips;

    const uint32_t c = ARM_DWT_CYCCNT - t0;
    cyclesAvg_ = cyclesAvg_ + (c - cyclesAvg_) * 0.01f;
    if (c > cyclesMax_) cyclesMax_ = c;
  }

 private:
  static float cyclesToPercent(float cycles) {
    const float cyclesPerBlock = (float)F_CPU_ACTUAL * AUDIO_BLOCK_SAMPLES / AUDIO_SAMPLE_RATE_EXACT;
    return 100.0f * cycles / cyclesPerBlock;
  }

  // Moves cur one block-length step toward target (0 -> 1 takes 20 ms).
  static float ramp(float cur, float target) {
    const float step = (float)AUDIO_BLOCK_SAMPLES / (AUDIO_SAMPLE_RATE_EXACT * 0.02f);
    return target > cur ? fminf(cur + step, target) : fmaxf(cur - step, target);
  }

  // Clips to full scale (counting clipped samples) and converts to int16.
  static void toInt16(const float* v, int16_t* out, float& peak, uint32_t& clips) {
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      float s = v[i];
      const float a = fabsf(s);
      peak = fmaxf(peak, a);
      if (a > 0.99997f) {
        clips++;
        s = s > 0 ? 0.99997f : -0.99997f;
      }
      out[i] = (int16_t)lrintf(s * 32767.0f);
    }
  }

  // Mixes the reverb into core (in place of wl/wr). Returns false while the
  // reverb is idle, in which case core is the output for both channels.
  bool processReverb(const float* core, float* wl, float* wr) {
    constexpr int B = AUDIO_BLOCK_SAMPLES;
    const bool on = rvOn_ && reverb_.ready();
    if (!on && !rvRunning_) return false;
    if (!rvRunning_) {  // starting from idle: the tank is already clear
      rvRunning_ = true;
      rvQuietBlocks_ = 0;
    }

    reverb_.setSize(rvSize_);
    reverb_.setHiDamp(1.0f - rvTone_);
    reverb_.setDiffusion(rvDiffusion_);
    reverb_.setLoDamp(rvLoDamp_);
    reverb_.setLowpass(rvLowpass_);
    reverb_.setHipass(rvHipass_);

    // Off fades the tank input out and leaves the tail to ring; the dry
    // signal returns to unity.
    const float in0 = rvInCur_, in1 = ramp(in0, on ? 1.0f : 0.0f);
    const float d0 = rvDryCur_, d1 = ramp(d0, on ? rvDryMix_ : 1.0f);
    const float e0 = rvWetCur_, e1 = ramp(e0, rvWetMix_);
    float rin[B];
    for (int i = 0; i < B; i++) rin[i] = core[i] * (in0 + (in1 - in0) * (float)(i + 1) / (float)B);
    reverb_.process(rin, wl, wr, B);
    float wetPeak = 0.0f;
    for (int i = 0; i < B; i++) {
      const float a = (float)(i + 1) / (float)B;
      const float d = d0 + (d1 - d0) * a, e = e0 + (e1 - e0) * a;
      wetPeak = fmaxf(wetPeak, fmaxf(fabsf(wl[i]), fabsf(wr[i])));
      wl[i] = d * core[i] + e * wl[i];
      wr[i] = d * core[i] + e * wr[i];
    }
    rvInCur_ = in1;
    rvDryCur_ = d1;
    rvWetCur_ = e1;

    // Once off and the tail is inaudible (-100 dBFS for 0.5 s), clear the
    // tank and go idle (no reverb CPU while off).
    constexpr int kQuietBlocks = (int)(AUDIO_SAMPLE_RATE_EXACT * 0.5f / B);
    if (on || wetPeak > 1e-5f) {
      rvQuietBlocks_ = 0;
    } else if (++rvQuietBlocks_ >= kQuietBlocks && d1 == 1.0f) {
      reverb_.reset();
      rvRunning_ = false;
    }
    return true;
  }

  audio_block_t* inputQueue_[1];
  Engine engine_;
  irdsp::PlateReverb reverb_;
  volatile float gainTarget_ = 0.5f, gainCur_ = 0.5f;
  volatile float wetTarget_ = 1.0f, wetCur_ = 1.0f;
  volatile float inPeak_ = 0.0f, outPeak_ = 0.0f;
  volatile uint32_t clips_ = 0;
  volatile float cyclesAvg_ = 0.0f;
  volatile uint32_t cyclesMax_ = 0;

  // Reverb settings (written by setters, read once per block).
  volatile bool rvOn_ = false;
  volatile float rvWetMix_ = 0.0f, rvDryMix_ = 1.0f;
  volatile float rvSize_ = 0.5f, rvTone_ = 0.5f;
  volatile float rvDiffusion_ = 1.0f, rvLoDamp_ = 0.0f, rvLowpass_ = 0.0f, rvHipass_ = 0.0f;
  // Reverb state (audio interrupt only, except reads of rvRunning_).
  volatile bool rvRunning_ = false;
  float rvInCur_ = 0.0f, rvDryCur_ = 1.0f, rvWetCur_ = 0.0f;
  int rvQuietBlocks_ = 0;
  volatile float rvCyclesAvg_ = 0.0f;
};
