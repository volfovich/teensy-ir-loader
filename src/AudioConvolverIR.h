// AudioConvolverIR.h - Teensy Audio Library object wrapping the convolver
// and the delay and plate reverb that follow it.
//
// One mono input (guitar), two outputs (0 = left, 1 = right). The whole chain
// runs in 32-bit float inside update():
//
//   in -> IR convolution -> IR bypass blend x output gain = core (mono)
//   core   -> delay (mono in, stereo out, dry/wet)           -> dL, dR
//             (ping-pong or studio 80s, sharing the same lines)
//   dL, dR -> plate reverb (stereo in, stereo out, dry/wet)  -> out L / R
//   + drum machine (stereo, added to the outputs, never through the effects)
//
// Adds smoothed output gain, click-free IR bypass, on/off for each effect
// (tails ring out when switched off; an idle effect passes its input through
// unchanged and costs no CPU), peak meters, a clip counter and CPU
// measurement of its own update().
#pragma once

#include <Arduino.h>
#include <AudioStream.h>
#include <math.h>

#include "DrumMachine.h"
#include "FxSlot.h"
#include "PartitionedConvolver.h"
#include "PlateReverb.h"
#include "StereoDelay.h"
#include "StudioDelay.h"
#include "config.h"

class AudioConvolverIR : public AudioStream {
 public:
  using Engine = irdsp::PartitionedConvolver<AUDIO_BLOCK_SAMPLES, IR_MAX_LENGTH>;

  AudioConvolverIR()
      : AudioStream(1, inputQueue_),
        delay_(AUDIO_SAMPLE_RATE_EXACT),
        studio_(AUDIO_SAMPLE_RATE_EXACT),
        reverb_(AUDIO_SAMPLE_RATE_EXACT),
        dlSlot_(AUDIO_SAMPLE_RATE_EXACT),
        rvSlot_(AUDIO_SAMPLE_RATE_EXACT),
        drums_(AUDIO_SAMPLE_RATE_EXACT) {}

  Engine& engine() { return engine_; }

  // Hands the reverb its line memory (PlateReverb::kMemFloats floats, may be
  // DMAMEM). Until this is called the reverb stays off.
  void beginReverb(float* mem) {
    __disable_irq();
    reverb_.init(mem);
    __enable_irq();
  }
  // Hands the delay its two lines (len floats each), shared by both delay
  // types. Until this is called the delay stays off.
  void beginDelay(float* lineA, float* lineB, int len) {
    __disable_irq();
    delay_.init(lineA, lineB, len);
    studio_.init(lineA, lineB, len);
    __enable_irq();
  }

  void setOutputGainDb(float db) { gainTarget_ = powf(10.0f, db / 20.0f); }
  void setBypass(bool on) { wetTarget_ = on ? 0.0f : 1.0f; }
  bool bypassed() const { return wetTarget_ < 0.5f; }

  // Delay controls, all 0..1 except on/off and time. Applied at the next block.
  void setDelayOn(bool on) { dlSlot_.setOn(on); }
  void setDelayMix(float m) { dlSlot_.setMix(m); }
  void setDelayTimeMs(float ms) { dlTimeMs_ = ms; }
  void setDelayFeedback(float v) { dlFeedback_ = v; }
  void setDelayTone(float v) { dlTone_ = v; }  // 1 = brightest repeats
  void setDelayMod(float v) { dlMod_ = v; }    // modulation depth
  void setDelayShape(float bassCut, float treble, float bass, float modRate, float inertia) {
    dlBassCut_ = bassCut; dlTreble_ = treble; dlBass_ = bass; dlModRate_ = modRate; dlInertia_ = inertia;
  }
  // 0 = ping-pong, 1 = studio 80s. A switch while repeats ring fades them
  // out (20 ms), clears the lines (about 12 ms) and starts the new type.
  void setDelayType(int type) { dlType_ = type == kStudio ? kStudio : kPingPong; }
  int delayType() const { return dlType_; }
  // Studio 80s only, all 0..1.
  void setDelayDuck(float v) { dlDuck_ = v; }
  void setDelayDrive(float v) { dlDrive_ = v; }
  void setDelayEra(float v) { dlEra_ = v; }
  // Studio 80s fixed voicing. Applied directly (single values; the mod rate
  // and duck release are converted here rather than every block).
  void setDelayStudioShape(float rightRatio, float cross, float bassCut, float modRateHz,
                           float duckThresholdDb, float duckReleaseMs) {
    studio_.setRightRatio(rightRatio);
    studio_.setCross(cross);
    dl80BassCut_ = bassCut;
    studio_.setModRate(modRateHz);
    studio_.setDuckThresholdDb(duckThresholdDb);
    studio_.setDuckReleaseMs(duckReleaseMs);
  }
  // True while the delay is on or its repeats are still ringing out.
  bool delayActive() const { return dlSlot_.running(); }

  // Reverb controls, all 0..1 except on/off. Applied at the next block.
  void setReverbOn(bool on) { rvSlot_.setOn(on); }
  void setReverbMix(float m) { rvSlot_.setMix(m); }
  void setReverbSize(float v) { rvSize_ = v; }
  void setReverbTone(float v) { rvTone_ = v; }  // 1 = brightest tail
  void setReverbShape(float diffusion, float loDamp, float lowpass, float hipass) {
    rvDiffusion_ = diffusion; rvLoDamp_ = loDamp; rvLowpass_ = lowpass; rvHipass_ = hipass;
  }
  // True while the reverb is on or its tail is still ringing out.
  bool reverbActive() const { return rvSlot_.running(); }

  // Drum machine controls (see DrumMachine.h). Style and tempo apply at the
  // next bar / step; volume 0..1.
  void setDrumsPlaying(bool on) { if (on) drums_.start(); else drums_.stop(); }
  void setDrumStyle(int idx) { drums_.setStyle(idx); }
  void setDrumTempo(float bpm) { drums_.setTempo(bpm); }
  void setDrumVolume(float v) { drums_.setVolume(v); }
  void setDrumCountIn(bool on) { drums_.setCountIn(on); }
  void setDrumTrainer(bool on, int step, int every, int target) { drums_.setTrainer(on, step, every, target); }
  void setDrumKit(int idx) { drums_.setKit(&irdsp::drumKit(idx)); }
  void setDrumCrash(int bars) { drums_.setCrashEvery(bars); }
  bool drumsPlaying() const { return drums_.playing(); }
  bool drumsCountingIn() const { return drums_.countingIn(); }
  float drumTempoNow() const { return drums_.tempoNow(); }
  int drumBar() const { return drums_.bar(); }

  // Meters: return the peak (linear, 1.0 = full scale) since the last call.
  float takeInputPeak() { float p = inPeak_; inPeak_ = 0.0f; return p; }
  float takeOutputPeak() { float p = outPeak_; outPeak_ = 0.0f; return p; }
  uint32_t clipCount() const { return clips_; }

  // CPU load of this object, in percent of one core.
  float cpuPercent() const { return cyclesToPercent(cyclesAvg_); }
  float cpuPercentMax() const { return cyclesToPercent(cyclesMax_); }
  // Shares of the above spent in the delay and the reverb.
  float delayCpuPercent() const { return cyclesToPercent(dlSlot_.cyclesAvg()); }
  float reverbCpuPercent() const { return cyclesToPercent(rvSlot_.cyclesAvg()); }
  float drumCpuPercent() const { return cyclesToPercent(drumCyclesAvg_); }
  void resetCpuMax() { cyclesMax_ = 0; }

  void update(void) override {
    const uint32_t t0 = ARM_DWT_CYCCNT;
    constexpr int B = AUDIO_BLOCK_SAMPLES;
    float x[B], y[B], dl[B], dr[B], ol[B], orr[B];

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

    // Delay, then reverb. An idle effect passes its input through, so with
    // both idle the signal stays mono.
    const uint32_t t1 = ARM_DWT_CYCCNT;
    const bool delayed = processDelay(y, dl, dr);
    const uint32_t t2 = ARM_DWT_CYCCNT;
    dlSlot_.addCycles(t2 - t1);
    const float* sl = delayed ? dl : y;
    const float* sr = delayed ? dr : y;
    const bool reverbed = processReverb(sl, sr, ol, orr);
    rvSlot_.addCycles(ARM_DWT_CYCCNT - t2);
    const float* fl = reverbed ? ol : sl;
    const float* fr = reverbed ? orr : sr;

    // Drums (stereo), added to the outputs below (nullptr while idle).
    const uint32_t t3 = ARM_DWT_CYCCNT;
    float drumL[B], drumR[B];
    const bool drums = drums_.process(drumL, drumR, B);
    const float* addL = drums ? drumL : nullptr;
    const float* addR = drums ? drumR : nullptr;
    drumCyclesAvg_ = drumCyclesAvg_ + ((float)(ARM_DWT_CYCCNT - t3) - drumCyclesAvg_) * 0.01f;

    float opk = outPeak_;
    uint32_t clips = 0;
    audio_block_t* outL = allocate();
    if (!outL) return;
    if (!delayed && !reverbed && !drums) {
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

  // An effect that has gone idle is cleared a few KB per block (clearing it
  // all at once would stall this interrupt), or all at once if it is
  // switched on again meanwhile. Returns false while it is still clearing.
  template <class Fx>
  static bool finishClearing(bool& clearing, const irdsp::FxSlot<AUDIO_BLOCK_SAMPLES>& slot, Fx& fx) {
    if (!clearing) return true;
    if (!slot.on()) {
      if (fx.clearStep(4096)) clearing = false;
      return false;
    }
    fx.clearStep(1 << 30);
    clearing = false;
    return true;
  }

  // The delay stage: core -> out L/R. Returns false while the delay is idle
  // (the caller uses core for both channels).
  bool processDelay(const float* core, float* outL, float* outR) {
    // Idle with clean lines: a type change takes effect at once.
    if (!dlClearing_ && !dlSlot_.running()) {
      dlActive_ = dlType_;
      dlSwitchClearing_ = false;
      dlTypeGain_ = 1.0f;
    }
    return dlActive_ == kStudio ? runDelay(studio_, core, outL, outR)
                                : runDelay(delay_, core, outL, outR);
  }

  template <class Fx>
  bool runDelay(Fx& fx, const float* core, float* outL, float* outR) {
    constexpr int B = AUDIO_BLOCK_SAMPLES;
    if (!finishClearing(dlClearing_, dlSlot_, fx)) return false;
    const bool wasRunning = dlSlot_.running();
    if (!dlSlot_.begin(fx.ready())) return false;

    // Time changes glide or crossfade, except when starting from idle or
    // after a type switch: the lines are empty then, so jump straight to
    // the set time.
    applyDelayParams(fx, !wasRunning || dlRestart_);
    dlRestart_ = false;

    // Type switch while running: fade the repeats out, clear the lines a few
    // KB per block, then hand over. The dry signal is not affected. Once the
    // clearing has started it is finished even if the type is switched back.
    const bool switching = dlSwitchClearing_ || dlType_ != dlActive_;
    const float t0 = dlTypeGain_, t1 = ramp(t0, switching ? 0.0f : 1.0f);
    if (switching && t0 == 0.0f) {
      dlSwitchClearing_ = true;
      if (fx.clearStep(4096)) {
        dlSwitchClearing_ = false;
        dlActive_ = dlType_;
        dlRestart_ = true;
      }
      for (int i = 0; i < B; i++) outL[i] = outR[i] = 0.0f;
    } else {
      float in[B];
      dlSlot_.feed(core, in);
      fx.process(in, outL, outR, B);
      if (t0 != 1.0f || t1 != 1.0f) {
        for (int i = 0; i < B; i++) {
          const float g = t0 + (t1 - t0) * (float)(i + 1) / (float)B;
          outL[i] *= g;
          outR[i] *= g;
        }
      }
    }
    dlTypeGain_ = t1;
    dlSlot_.mix(core, outL);
    dlSlot_.mix(core, outR);
    if (dlSlot_.end()) dlClearing_ = true;
    return true;
  }

  void applyDelayParams(irdsp::StereoDelay& d, bool force) {
    d.setDelaySamples(dlTimeMs_ * (AUDIO_SAMPLE_RATE_EXACT / 1000.0f), force);
    d.setFeedback(dlFeedback_);
    d.setTrebleCut(1.0f - dlTone_);
    d.setModDepth(dlMod_);
    d.setBassCut(dlBassCut_);
    d.setTreble(dlTreble_);
    d.setBass(dlBass_);
    d.setModRate(dlModRate_);
    d.setInertia(dlInertia_);
  }

  void applyDelayParams(irdsp::StudioDelay& d, bool force) {
    d.setDelaySamples(dlTimeMs_ * (AUDIO_SAMPLE_RATE_EXACT / 1000.0f), force);
    d.setFeedback(dlFeedback_);
    d.setTone(dlTone_);
    d.setModDepth(dlMod_);
    d.setBassCut(dl80BassCut_);
    d.setDuck(dlDuck_);
    d.setDrive(dlDrive_);
    d.setEra(dlEra_);
  }

  // The reverb stage: in L/R -> out L/R. Returns false while the reverb is
  // idle (the caller passes its input on).
  bool processReverb(const float* inL, const float* inR, float* outL, float* outR) {
    constexpr int B = AUDIO_BLOCK_SAMPLES;
    if (!finishClearing(rvClearing_, rvSlot_, reverb_)) return false;
    if (!rvSlot_.begin(reverb_.ready())) return false;

    reverb_.setSize(rvSize_);
    reverb_.setHiDamp(1.0f - rvTone_);
    reverb_.setDiffusion(rvDiffusion_);
    reverb_.setLoDamp(rvLoDamp_);
    reverb_.setLowpass(rvLowpass_);
    reverb_.setHipass(rvHipass_);

    float rl[B], rr[B];
    rvSlot_.feed(inL, rl);
    rvSlot_.feed(inR, rr);
    reverb_.process(rl, rr, outL, outR, B);
    rvSlot_.mix(inL, outL);
    rvSlot_.mix(inR, outR);
    if (rvSlot_.end()) rvClearing_ = true;
    return true;
  }

  audio_block_t* inputQueue_[1];
  Engine engine_;
  static constexpr int kPingPong = 0, kStudio = 1;
  irdsp::StereoDelay delay_;
  irdsp::StudioDelay studio_;
  irdsp::PlateReverb reverb_;
  irdsp::FxSlot<AUDIO_BLOCK_SAMPLES> dlSlot_, rvSlot_;
  irdsp::DrumMachine drums_;
  volatile float drumCyclesAvg_ = 0.0f;
  bool dlClearing_ = false, rvClearing_ = false;  // audio interrupt only
  // Delay type switching (audio interrupt only, except dlType_).
  volatile int dlType_ = kPingPong;
  int dlActive_ = kPingPong;
  bool dlSwitchClearing_ = false, dlRestart_ = false;
  float dlTypeGain_ = 1.0f;
  volatile float gainTarget_ = 0.5f, gainCur_ = 0.5f;
  volatile float wetTarget_ = 1.0f, wetCur_ = 1.0f;
  volatile float inPeak_ = 0.0f, outPeak_ = 0.0f;
  volatile uint32_t clips_ = 0;
  volatile float cyclesAvg_ = 0.0f;
  volatile uint32_t cyclesMax_ = 0;

  // Effect settings (written by setters, read once per block).
  volatile float dlTimeMs_ = 380.0f, dlFeedback_ = 0.35f, dlTone_ = 0.6f, dlMod_ = 0.0f;
  volatile float dlBassCut_ = 0.0f, dlTreble_ = 1.0f, dlBass_ = 1.0f, dlModRate_ = 0.0f, dlInertia_ = 0.0f;
  volatile float dlDuck_ = 0.0f, dlDrive_ = 0.0f, dlEra_ = 0.0f, dl80BassCut_ = 0.0f;
  volatile float rvSize_ = 0.5f, rvTone_ = 0.5f;
  volatile float rvDiffusion_ = 1.0f, rvLoDamp_ = 0.0f, rvLowpass_ = 0.0f, rvHipass_ = 0.0f;
};
