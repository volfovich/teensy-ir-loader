// AudioConvolverIR.h - Teensy Audio Library object wrapping the amp, the
// convolver and the delay and reverb that follow them.
//
// One mono input (guitar), two outputs (0 = left, 1 = right). The whole chain
// runs in 32-bit float inside update():
//
//   in -> amp (NAM A2-Lite, optional, see AmpStage.h) = amped
//   amped -> IR convolution -> IR bypass blend x output gain = core (mono)
//   core   -> doubler (mono in, stereo out, see Doubler.h)    -> cL, cR
//   cL, cR -> studio 80s delay (fed from cL, dry/wet)         -> dL, dR
//   dL, dR -> 80s reverb, hall or gated (stereo, dry/wet)      -> out L / R
//   + drum machine and bass (stereo, added to the outputs, never through
//     the effects)
//
// The tuner taps the input (decimated to 24 kHz, see PitchDetect.h) and
// mutes the guitar, before the delay and reverb so their tails ring out.
//
// Adds smoothed output gain, click-free IR bypass, on/off for each effect
// (tails ring out when switched off; an idle effect passes its input through
// unchanged and costs no CPU, see FxStage.h), peak meters, a clip counter and
// CPU measurement of its own update().
//
// The effects' and the drums' own setters are safe to call from the
// background (see their headers): reach them through delay(), reverb() and
// drums(), and the amp's through amp().
#pragma once

#include <Arduino.h>
#include <AudioStream.h>
#include <math.h>

#include "AmpStage.h"
#include "Doubler.h"
#include "DrumMachine.h"
#include "FxStage.h"
#include "PartitionedConvolver.h"
#include "Reverb80.h"
#include "StudioDelay.h"
#include "config.h"

class AudioConvolverIR : public AudioStream {
 public:
  using Engine = irdsp::PartitionedConvolver<AUDIO_BLOCK_SAMPLES, IR_MAX_LENGTH>;
  using DelayStage = irdsp::FxStage<irdsp::StudioDelay, AUDIO_BLOCK_SAMPLES, false>;
  using ReverbStage = irdsp::FxStage<irdsp::Reverb80, AUDIO_BLOCK_SAMPLES, true>;
  using Amp = irdsp::AmpStage<AUDIO_BLOCK_SAMPLES>;
  static constexpr int kAmpHistoryFloats = Amp::kHistoryFloats;
  static constexpr int kReverbMemFloats = irdsp::Reverb80::kMemFloats;
  static constexpr int kBassBufLen = irdsp::DrumMachine::kBassBufLen;
  static constexpr int kDoublerFloats = irdsp::Doubler::kLen;
  static constexpr int kTunerRing = 4096;  // floats at 24 kHz (170 ms)
  static constexpr float kTunerRate = AUDIO_SAMPLE_RATE_EXACT / 2;

  AudioConvolverIR()
      : AudioStream(1, inputQueue_),
        amp_(AUDIO_SAMPLE_RATE_EXACT),
        delay_(AUDIO_SAMPLE_RATE_EXACT),
        reverb_(AUDIO_SAMPLE_RATE_EXACT),
        doubler_(AUDIO_SAMPLE_RATE_EXACT),
        drums_(AUDIO_SAMPLE_RATE_EXACT) {
    tunerLp_.setLowpass(4000.0f, AUDIO_SAMPLE_RATE_EXACT);
  }

  // Hands over the effects' memory before the audio runs: the reverb's lines
  // (kReverbMemFloats), the delay's two lines (delayLen floats each), the
  // bass's buffer (kBassBufLen), the amp's history (kAmpHistoryFloats), the
  // doubler's line (kDoublerFloats) and the tuner's ring (kTunerRing). Until
  // then the effects stay off.
  void begin(float* reverbMem, float* delayA, float* delayB, int delayLen, float* bassBuf, float* ampHistory,
             float* doublerLine, float* tunerRing);

  Engine& engine() { return engine_; }
  Amp& amp() { return amp_; }
  irdsp::StudioDelay& delay() { return delay_.fx(); }
  irdsp::Reverb80& reverb() { return reverb_.fx(); }
  irdsp::Doubler& doubler() { return doubler_; }
  irdsp::DrumMachine& drums() { return drums_; }
  const irdsp::DrumMachine& drums() const { return drums_; }

  void setOutputGainDb(float db) { gainTarget_ = powf(10.0f, db / 20.0f); }
  void setBypass(bool on) { wetTarget_ = on ? 0.0f : 1.0f; }
  bool bypassed() const { return wetTarget_ < 0.5f; }

  // Effect on/off and mix (0 = dry only, 1 = wet only); "active" while on or
  // still ringing out.
  void setDelayOn(bool on) { delay_.setOn(on); }
  void setDelayMix(float m) { delay_.setMix(m); }
  bool delayActive() const { return delay_.running(); }
  void setReverbOn(bool on) { reverb_.setOn(on); }
  void setReverbMix(float m) { reverb_.setMix(m); }
  bool reverbActive() const { return reverb_.running(); }

  // The tuner: while on, the input is kept for tunerSnapshot() and the
  // guitar is muted.
  void setTuner(bool on) { tunerOn_ = on; }
  bool tunerOn() const { return tunerOn_; }
  // The newest n (<= kTunerRing / 2) samples at kTunerRate; false until
  // that many have come in since the tuner was switched on.
  bool tunerSnapshot(float* dst, int n) {
    for (int tries = 0; tries < 4; tries++) {
      const uint32_t w0 = tunerWritten_;
      if (w0 < (uint32_t)n) return false;
      for (int i = 0; i < n; i++) dst[i] = tunerRing_[(w0 - n + i) & (kTunerRing - 1)];
      if (tunerWritten_ - w0 <= (uint32_t)(kTunerRing - n)) return true;  // none overwritten meanwhile
    }
    return false;
  }

  // Meters: return the peak (linear, 1.0 = full scale) since the last call.
  float takeInputPeak() { float p = inPeak_; inPeak_ = 0.0f; return p; }
  float takeOutputPeak() { float p = outPeak_; outPeak_ = 0.0f; return p; }
  uint32_t clipCount() const { return clips_; }
  // The same for the display, kept apart so the serial meter and the OLED
  // don't take each other's peaks.
  void takeDisplayPeaks(float& in, float& out) {
    in = dispInPeak_;
    out = dispOutPeak_;
    dispInPeak_ = dispOutPeak_ = 0.0f;
  }

  // CPU load of this object, in percent of one core, and the shares of it
  // spent in the amp, the delay, the reverb and the drums.
  float cpuPercent() const { return cyclesToPercent(cyclesAvg_); }
  float cpuPercentMax() const { return cyclesToPercent(cyclesMax_); }
  float ampCpuPercent() const { return cyclesToPercent(amp_.cyclesAvg()); }
  float delayCpuPercent() const { return cyclesToPercent(delay_.cyclesAvg()); }
  float reverbCpuPercent() const { return cyclesToPercent(reverb_.cyclesAvg()); }
  float doublerCpuPercent() const { return cyclesToPercent(doublerCyclesAvg_); }
  float drumCpuPercent() const { return cyclesToPercent(drumCyclesAvg_); }
  void resetCpuMax() { cyclesMax_ = 0; }

  void update(void) override {
    const uint32_t t0 = ARM_DWT_CYCCNT;
    constexpr int B = AUDIO_BLOCK_SAMPLES;
    float x[B], a[B], y[B], dl[B], dr[B], ol[B], orr[B];

    audio_block_t* in = receiveReadOnly(0);
    float pk = 0.0f;  // this block's
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
    inPeak_ = fmaxf(inPeak_, pk);
    dispInPeak_ = fmaxf(dispInPeak_, pk);

    // The tuner's tap: low-passed, every second sample.
    if (tunerOn_ && tunerRing_) {
      uint32_t w = tunerWritten_;
      for (int i = 0; i < B; i++) {
        const float v = tunerLp_.process(x[i]);
        if (i & 1) tunerRing_[w++ & (kTunerRing - 1)] = v;
      }
      tunerWritten_ = w;
    } else if (tunerWritten_) {
      tunerWritten_ = 0;
      tunerLp_.clear();
    }

    // The amp (idle: the input as it is).
    const uint32_t ta = ARM_DWT_CYCCNT;
    const bool amped = amp_.process(x, a);
    amp_.addCycles(ARM_DWT_CYCCNT - ta);
    const float* src = amped ? a : x;

    engine_.process(src, y);

    // Per-sample linear ramps for gain and bypass (no zipper noise).
    const float g0 = gainCur_, g1 = gainTarget_;
    const float w0 = wetCur_;
    const float w1 = ramp(w0, wetTarget_);
    const float u0 = playCur_, u1 = ramp(u0, tunerOn_ ? 0.0f : 1.0f);  // the tuner's mute
    for (int i = 0; i < B; i++) {
      const float a = (float)(i + 1) / (float)B;
      const float g = g0 + (g1 - g0) * a;
      const float w = w0 + (w1 - w0) * a;
      const float u = u0 + (u1 - u0) * a;
      // Gain applies to the processed signal; bypass passes the IR's input.
      y[i] = u * (w * g * y[i] + (1.0f - w) * src[i]);
    }
    gainCur_ = g1;
    wetCur_ = w1;
    playCur_ = u1;

    // The doubler, the delay, then the reverb. An idle effect passes its
    // input through, so with all idle the signal stays mono.
    const uint32_t tdb = ARM_DWT_CYCCNT;
    float cl[B], cr[B];
    const bool doubled = doubler_.process(y, cl, cr, B);
    const float* sl0 = doubled ? cl : y;
    const float* sr0 = doubled ? cr : y;
    const uint32_t t1 = ARM_DWT_CYCCNT;
    doublerCyclesAvg_ = doublerCyclesAvg_ + ((float)(t1 - tdb) - doublerCyclesAvg_) * 0.01f;
    const bool delayed = delay_.process(sl0, sr0, dl, dr);
    const uint32_t t2 = ARM_DWT_CYCCNT;
    delay_.addCycles(t2 - t1);
    const float* sl = delayed ? dl : sl0;
    const float* sr = delayed ? dr : sr0;
    const bool reverbed = reverb_.process(sl, sr, ol, orr);
    reverb_.addCycles(ARM_DWT_CYCCNT - t2);
    const float* fl = reverbed ? ol : sl;
    const float* fr = reverbed ? orr : sr;

    // Drums (stereo), added to the outputs below (nullptr while idle).
    const uint32_t t3 = ARM_DWT_CYCCNT;
    float drumL[B], drumR[B];
    const bool drums = drums_.process(drumL, drumR, B);
    const float* addL = drums ? drumL : nullptr;
    const float* addR = drums ? drumR : nullptr;
    drumCyclesAvg_ = drumCyclesAvg_ + ((float)(ARM_DWT_CYCCNT - t3) - drumCyclesAvg_) * 0.01f;

    float opk = 0.0f;  // this block's
    uint32_t clips = 0;
    audio_block_t* outL = allocate();
    if (!outL) return;
    if (!doubled && !delayed && !reverbed && !drums) {
      // Effects and drums idle: the dry signal goes to both outputs.
      toInt16(y, nullptr, outL->data, opk, clips);
      transmit(outL, 0);
      transmit(outL, 1);
      release(outL);
    } else {
      audio_block_t* outR = allocate();
      toInt16(fl, addL, outL->data, opk, clips);
      transmit(outL, 0);
      if (outR) {
        toInt16(fr, addR, outR->data, opk, clips);
        transmit(outR, 1);
        release(outR);
      } else {
        transmit(outL, 1);  // out of audio memory: mono rather than silence
      }
      release(outL);
    }
    outPeak_ = fmaxf(outPeak_, opk);
    dispOutPeak_ = fmaxf(dispOutPeak_, opk);
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

  // Adds add (if any), clips to full scale (counting clipped samples) and
  // converts to int16.
  static void toInt16(const float* v, const float* add, int16_t* out, float& peak, uint32_t& clips) {
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      float s = add ? v[i] + add[i] : v[i];
      const float a = fabsf(s);
      peak = fmaxf(peak, a);
      if (a > 0.99997f) {
        clips++;
        s = s > 0 ? 0.99997f : -0.99997f;
      }
      out[i] = (int16_t)lrintf(s * 32767.0f);
    }
  }

  audio_block_t* inputQueue_[1];
  Amp amp_;
  Engine engine_;
  DelayStage delay_;
  ReverbStage reverb_;
  irdsp::Doubler doubler_;
  irdsp::DrumMachine drums_;
  volatile float drumCyclesAvg_ = 0.0f, doublerCyclesAvg_ = 0.0f;
  volatile bool tunerOn_ = false;
  float* tunerRing_ = nullptr;
  volatile uint32_t tunerWritten_ = 0;  // samples since the tuner went on
  irdsp::Biquad tunerLp_;
  float playCur_ = 1.0f;  // 0 while the tuner mutes
  volatile float gainTarget_ = 0.5f, gainCur_ = 0.5f;
  volatile float wetTarget_ = 1.0f, wetCur_ = 1.0f;
  volatile float inPeak_ = 0.0f, outPeak_ = 0.0f;
  volatile float dispInPeak_ = 0.0f, dispOutPeak_ = 0.0f;
  volatile uint32_t clips_ = 0;
  volatile float cyclesAvg_ = 0.0f;
  volatile uint32_t cyclesMax_ = 0;
};
