// DrumMachine.cpp - see DrumMachine.h
#include "DrumMachine.h"

#include <math.h>
#include <string.h>

#include "irdsp_port.h"

namespace irdsp {

namespace {

constexpr float kOff = 1e-4f;  // an envelope below this (-80 dB) is silent

// Per-sample decay factor for a fall of 60 dB in t60 seconds.
float decayK(float t60, float fs) { return expf(-6.9078f / (t60 * fs)); }

// Mix levels of the voices on the bus (before the soft clip and volume).
constexpr float kKickGain = 0.9f, kSnareBody = 0.45f, kSnareNoise = 0.5f, kRimGain = 0.45f;
constexpr float kHatGain = 0.5f, kRideGain = 0.35f, kCrashGain = 0.4f, kClickGain = 0.9f, kBusGain = 0.55f;
// Samples are int16 and already level-matched (kits/*/kit.txt).
constexpr float kSampleGain = 1.0f / 32768.0f;
// Fades when a sample is cut off: retriggered, choked, replaced, kit change.
constexpr float kRetriggerMs = 2.0f, kChokeMs = 5.0f;
// Cymbals ring on under a new hit, up to this many sounding at once.
constexpr int kCymbalVoices = 2;

// TR-808 cymbal / hi-hat oscillator frequencies.
constexpr float kMetalHz[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};

// Velocity of a lane character's level: ghost, normal, accent.
constexpr float kLevelVel[4] = {0.0f, 0.3f, 0.7f, 1.0f};

// Cubic soft clip: unity gain at 0, reaches +-1 at |x| = 1.5.
inline float softClip(float x) {
  if (x > 1.5f) return 1.0f;
  if (x < -1.5f) return -1.0f;
  return x - (4.0f / 27.0f) * x * x * x;
}

}  // namespace

DrumMachine::DrumMachine(float sampleRate)
    : fs_(sampleRate), hzToInc_(4294967296.0f / sampleRate), sin_(lfoSineTable()) {
  init();
}

// GCC can't put a constructor in flash, so its work is done here.
IRDSP_COLD void DrumMachine::init() {
  kickAmpK_ = decayK(0.6f, fs_);
  kickPitchK_ = decayK(0.12f, fs_);
  kickClickK_ = decayK(0.012f, fs_);
  snBodyK_ = decayK(0.2f, fs_);
  snNoiseK_ = decayK(0.3f, fs_);
  snInc_ = inc(185.0f);
  snHp_.setHighpass(1000.0f, fs_);
  snLp_.setLowpass(9000.0f, fs_);
  rimAmpK_ = decayK(0.06f, fs_);
  rimTickK_ = decayK(0.012f, fs_);
  rimInc1_ = inc(1700.0f);
  rimInc2_ = inc(830.0f);
  rimHp_.setHighpass(3000.0f, fs_);
  hatCK_ = decayK(0.09f, fs_);
  hatOK_ = decayK(0.7f, fs_);
  rideK_ = decayK(2.5f, fs_);
  for (int i = 0; i < 6; i++) metalInc_[i] = inc(kMetalHz[i]);
  hatHp1_.setHighpass(7000.0f, fs_);
  hatHp2_.setHighpass(7000.0f, fs_);
  rideBp_.setBandpass(4500.0f, fs_, 0.8f);
  crashK_ = decayK(1.8f, fs_);
  crashHp1_.setHighpass(4000.0f, fs_);
  crashHp2_.setHighpass(4000.0f, fs_);
  clickAmpK_ = decayK(0.05f, fs_);
  clickHi_ = inc(2000.0f);
  clickLo_ = inc(1200.0f);
  setVolume(0.6f);
  setStyle(0);
  swapPattern();
}

// ---- Control (background) ------------------------------------------------------

IRDSP_COLD void DrumMachine::setStyle(int idx) {
  if (idx < 0 || idx >= drumStyleCount()) return;
  // The interrupt only swaps while swapReq_ is set, so clearing it first makes
  // the spare buffer ours until we set it again.
  swapReq_ = false;
  Pattern& p = pat_[1 - cur_];
  const DrumStyle& s = drumStyle(idx);
  p.beats = s.beats;
  p.stepsPerBeat = s.stepsPerBeat;
  int steps = s.beats * s.stepsPerBeat;
  p.steps = (uint8_t)(steps > kMaxSteps ? kMaxSteps : steps);
  p.drums = false;
  for (int v = 0; v < kDrumVoices; v++) {
    const char* lane = s.lanes[v];
    const int len = (int)strlen(lane);
    for (int i = 0; i < kMaxSteps; i++) {
      const char c = i < len && i < p.steps ? lane[i] : '.';
      p.vel[v][i] = c == 'X' ? 3 : (c == 'x' ? 2 : (c == 'g' ? 1 : 0));
    }
    if (v != kClick && len > 0) p.drums = true;
  }
  styleReq_ = idx;
  swapReq_ = true;
}

IRDSP_COLD void DrumMachine::setTempo(float bpm) {
  baseBpm_ = bpm < kMinBpm ? kMinBpm : (bpm > kMaxBpm ? kMaxBpm : bpm);
  tempoReq_ = true;
}

IRDSP_COLD void DrumMachine::setVolume(float n) {
  n = clamp01(n);
  // 0 dB at 1, -30 dB near 0 on a curve that spends more travel at the top.
  const float d = 1.0f - n;
  volTarget_ = n <= 0.0f ? 0.0f : powf(10.0f, -30.0f * d * sqrtf(d) / 20.0f);
}

IRDSP_COLD void DrumMachine::setTrainer(bool on, int stepBpm, int everyBars, int targetBpm) {
  trStep_ = stepBpm < 1 ? 1 : stepBpm;
  trEvery_ = everyBars < 1 ? 1 : everyBars;
  trTarget_ = targetBpm;
  trOn_ = on;
  tempoReq_ = true;  // restart from the set tempo
}

// ---- Audio ---------------------------------------------------------------------

bool DrumMachine::voicesActive() const {
  return kickAmp_ > 0.0f || kickClick_ > 0.0f || snBody_ > 0.0f || snNoise_ > 0.0f ||
         rimAmp_ > 0.0f || rimTick_ > 0.0f || hatC_ > 0.0f || hatO_ > 0.0f || ride_ > 0.0f ||
         crash_ > 0.0f || clickAmp_ > 0.0f || samplesPlaying() > 0;
}

int DrumMachine::samplesPlaying() const {
  int n = 0;
  for (const SampleVoice& v : sv_) n += v.p != nullptr;
  return n;
}

void DrumMachine::swapPattern() {
  cur_ = 1 - cur_;
  swapReq_ = false;
}

void DrumMachine::trigger(int voice, int level, uint64_t sample) {
  if (hook_) hook_(hookCtx_, voice, level, sample);
  // +-4 % per hit so repeated notes don't sound like a machine gun.
  const float jitter = 1.0f + 0.04f * noise();
  if (kit_ && voice != kClick && kit_->count[voice]) {
    startSample(voice, level, jitter);
    return;
  }
  const float vel = kLevelVel[level] * jitter;
  switch (voice) {
    case kKick:
      kickAmp_ = vel;
      kickPitch_ = 1.0f;
      kickClick_ = vel;
      kickPhase_ = 0;
      break;
    case kSnare:
      snBody_ = vel;
      snNoise_ = vel;
      snPhase_ = 0;
      break;
    case kRim:
      rimAmp_ = vel;
      rimTick_ = vel;
      rimPhase1_ = rimPhase2_ = 0;
      break;
    case kHatClosed:
      hatC_ = vel;
      hatO_ = 0.0f;  // choke
      break;
    case kHatOpen:
      hatO_ = vel;
      break;
    case kRide:
      ride_ = vel;
      break;
    case kCrash:
      crash_ = vel;
      break;
    case kClick:
      clickAmp_ = vel;
      clickInc_ = level == 3 ? clickHi_ : clickLo_;
      clickPhase_ = 0;
      break;
  }
}

void DrumMachine::fireStep(uint64_t sample) {
  // A bar ends when the next one starts, so a new tempo or style applies
  // from its first step, not from the old bar's last one.
  if (step_ >= pat_[cur_].steps) {
    step_ = 0;
    endBar();
  }
  const Pattern& p = pat_[cur_];
  if (countIn_) {
    if (step_ % p.stepsPerBeat == 0) trigger(kClick, step_ == 0 ? 3 : 2, sample);
  } else {
    // A crash accent replaces the bar's first hat or ride hit.
    const int every = crashEvery_;
    const bool crash = step_ == 0 && every > 0 && p.drums && bar_ % every == 0;
    if (crash) trigger(kCrash, 3, sample);
    for (int v = 0; v < kDrumVoices; v++) {
      const int lvl = p.vel[v][step_];
      if (!lvl || (crash && (v == kHatClosed || v == kHatOpen || v == kRide || v == kCrash))) continue;
      trigger(v, lvl, sample);
    }
  }
  step_++;
}

void DrumMachine::endBar() {
  if (countIn_) {
    countIn_ = false;
  } else {
    bar_ = bar_ + 1;
    if (trOn_ && bpmNow_ < (float)trTarget_ && ++trBars_ >= trEvery_) {
      trBars_ = 0;
      const float b = bpmNow_ + (float)trStep_;
      bpmNow_ = b > (float)trTarget_ ? (float)trTarget_ : b;
    }
  }
  if (swapReq_) swapPattern();
}

void DrumMachine::renderVoices(float* out, int n) {
  for (int i = 0; i < n; i++) {
    float s = 0.0f;
    if (kickAmp_ > 0.0f || kickClick_ > 0.0f) {
      kickPhase_ += inc(50.0f + 110.0f * kickPitch_);
      kickPitch_ *= kickPitchK_;
      s += kKickGain * (sine(kickPhase_) * kickAmp_ + 0.4f * noise() * kickClick_);
      kickAmp_ *= kickAmpK_;
      kickClick_ *= kickClickK_;
      if (kickAmp_ < kOff) kickAmp_ = 0.0f;
      if (kickClick_ < kOff) kickClick_ = 0.0f;
    }
    if (snBody_ > 0.0f || snNoise_ > 0.0f) {
      snPhase_ += snInc_;
      const float nz = snLp_.process(snHp_.process(noise()));
      s += kSnareBody * sine(snPhase_) * snBody_ + kSnareNoise * 1.6f * nz * snNoise_;
      snBody_ *= snBodyK_;
      snNoise_ *= snNoiseK_;
      if (snBody_ < kOff) snBody_ = 0.0f;
      if (snNoise_ < kOff) snNoise_ = 0.0f;
    }
    if (rimAmp_ > 0.0f || rimTick_ > 0.0f) {
      rimPhase1_ += rimInc1_;
      rimPhase2_ += rimInc2_;
      s += kRimGain * ((sine(rimPhase1_) + 0.6f * sine(rimPhase2_)) * 0.6f * rimAmp_ +
                       rimHp_.process(noise()) * rimTick_);
      rimAmp_ *= rimAmpK_;
      rimTick_ *= rimTickK_;
      if (rimAmp_ < kOff) rimAmp_ = 0.0f;
      if (rimTick_ < kOff) rimTick_ = 0.0f;
    }
    if (hatC_ > 0.0f || hatO_ > 0.0f || ride_ > 0.0f || crash_ > 0.0f) {
      float metal = 0.0f;
      for (int k = 0; k < 6; k++) {
        metalPhase_[k] += metalInc_[k];
        metal += (metalPhase_[k] & 0x80000000u) ? 1.0f : -1.0f;
      }
      metal *= 1.0f / 6.0f;
      const float nz = noise();
      if (hatC_ > 0.0f || hatO_ > 0.0f) {
        const float hat = hatHp2_.process(hatHp1_.process(0.6f * metal + 0.5f * nz));
        s += kHatGain * 1.8f * hat * (hatC_ + hatO_);
        hatC_ *= hatCK_;
        hatO_ *= hatOK_;
        if (hatC_ < kOff) hatC_ = 0.0f;
        if (hatO_ < kOff) hatO_ = 0.0f;
      }
      if (ride_ > 0.0f) {
        s += kRideGain * 1.5f * rideBp_.process(metal + 0.25f * nz) * ride_;
        ride_ *= rideK_;
        if (ride_ < kOff) ride_ = 0.0f;
      }
      if (crash_ > 0.0f) {
        s += kCrashGain * 1.8f * crashHp2_.process(crashHp1_.process(0.5f * metal + 0.8f * nz)) * crash_;
        crash_ *= crashK_;
        if (crash_ < kOff) crash_ = 0.0f;
      }
    }
    if (clickAmp_ > 0.0f) {
      clickPhase_ += clickInc_;
      s += kClickGain * sine(clickPhase_) * clickAmp_;
      clickAmp_ *= clickAmpK_;
      if (clickAmp_ < kOff) clickAmp_ = 0.0f;
    }
    out[i] = s;
  }
  renderSamples(out, n);
  for (int i = 0; i < n; i++) out[i] = softClip(kBusGain * out[i]);
}

// ---- Sample voices ---------------------------------------------------------------

void DrumMachine::fadeVoice(int i, float ms) {
  SampleVoice& v = sv_[i];
  const float step = v.gain / (ms * 0.001f * fs_);
  if (v.fade == 0.0f || step > v.fade) v.fade = step;
}

// Fades out the lane's sounding voices, except the newest `keep` that aren't
// already fading.
void DrumMachine::fadeLane(int voice, float ms, int keep) {
  for (;;) {
    int count = 0, oldest = -1;
    for (int i = 0; i < kSampleVoices; i++) {
      const SampleVoice& v = sv_[i];
      if (!v.p || v.lane != voice || v.fade != 0.0f) continue;
      count++;
      if (oldest < 0 || v.age < sv_[oldest].age) oldest = i;
    }
    if (count <= keep) return;
    fadeVoice(oldest, ms);
  }
}

void DrumMachine::startSample(int voice, int level, float jitter) {
  const DrumKit& k = *kit_;
  // The loudest layer made for this level or below, else the softest.
  const DrumSample* s = &k.samples[k.first[voice]];
  for (int j = 1; j < k.count[voice]; j++) {
    const DrumSample& c = k.samples[k.first[voice] + j];
    if (c.level <= level) s = &c;
  }
  // A drum is re-struck (its last hit cut short); cymbals ring on under it.
  const bool cymbal = voice == kHatOpen || voice == kRide || voice == kCrash;
  fadeLane(voice, cymbal ? kChokeMs : kRetriggerMs, cymbal ? kCymbalVoices - 1 : 0);
  if (voice == kHatClosed) fadeLane(kHatOpen, kChokeMs, 0);

  // A free voice, else the one nearest its end.
  int pick = -1;
  for (int i = 0; i < kSampleVoices && pick < 0; i++)
    if (!sv_[i].p) pick = i;
  if (pick < 0) {
    float most = -1.0f;
    for (int i = 0; i < kSampleVoices; i++) {
      const float done = (float)sv_[i].pos / (float)sv_[i].len;
      if (done > most) most = done, pick = i;
    }
  }
  SampleVoice& v = sv_[pick];
  v.p = s->data;
  v.len = s->len;
  v.pos = 0;
  v.age = ++svAge_;
  v.gain = kSampleGain * kLevelVel[level] / kLevelVel[s->level] * jitter;
  v.fade = 0.0f;
  v.lane = (uint8_t)voice;
}

void DrumMachine::renderSamples(float* out, int n) {
  for (SampleVoice& v : sv_) {
    if (!v.p) continue;
    const int16_t* p = v.p + v.pos;
    const uint32_t left = v.len - v.pos;
    int m = left < (uint32_t)n ? (int)left : n;
    float g = v.gain;
    bool done = (uint32_t)m == left;
    if (v.fade == 0.0f) {
      for (int i = 0; i < m; i++) out[i] += g * (float)p[i];
    } else {
      for (int i = 0; i < m; i++) {
        g -= v.fade;
        if (g <= 0.0f) {
          done = true;
          break;
        }
        out[i] += g * (float)p[i];
      }
      v.gain = g;
    }
    v.pos += (uint32_t)m;
    if (done) v.p = nullptr;
  }
}

bool DrumMachine::process(float* out, int n) {
  if (startReq_) {
    startReq_ = false;
    if (swapReq_) swapPattern();
    step_ = 0;
    bar_ = 0;
    trBars_ = 0;
    countIn_ = countInOn_;
    bpmNow_ = baseBpm_;
    tempoReq_ = false;
    toNext_ = 0.0;
    playing_ = true;
  }
  if (stopReq_) {
    stopReq_ = false;
    playing_ = false;
    countIn_ = false;
  }
  if (!playing_ && swapReq_) swapPattern();
  if (kitReq_ != kit_) {
    for (int i = 0; i < kSampleVoices; i++)
      if (sv_[i].p) fadeVoice(i, kChokeMs);
    kit_ = kitReq_;
  }
  if (tempoReq_) {
    tempoReq_ = false;
    bpmNow_ = baseBpm_;
    trBars_ = 0;
  }
  if (!playing_ && !voicesActive()) {
    active_ = false;
    volCur_ = volTarget_;
    time_ += n;
    return false;
  }
  active_ = true;

  int i = 0;
  while (i < n) {
    if (playing_ && toNext_ <= 0.0) {
      fireStep(time_ + i);
      toNext_ += (double)fs_ * 60.0 / ((double)bpmNow_ * pat_[cur_].stepsPerBeat);
    }
    int run = n - i;
    if (playing_) {
      const int k = (int)ceil(toNext_);
      if (k < run) run = k;
    }
    renderVoices(out + i, run);
    toNext_ -= run;
    i += run;
  }

  time_ += n;

  // Volume, ramped across the block.
  const float v0 = volCur_, v1 = volTarget_;
  for (int j = 0; j < n; j++) out[j] *= v0 + (v1 - v0) * (float)(j + 1) / (float)n;
  volCur_ = v1;
  return true;
}

}  // namespace irdsp
