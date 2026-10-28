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
constexpr float kHatGain = 0.4f, kRideGain = 0.35f, kCrashGain = 0.31f, kClickGain = 0.9f, kBusGain = 1.1f;
// Samples are int16 and already level-matched (kits/*/kit.txt).
constexpr float kSampleGain = 1.0f / 32768.0f;
// Fades when a sample is cut off: retriggered, choked, replaced, kit change.
constexpr float kRetriggerMs = 2.0f, kChokeMs = 5.0f;
// Cymbals ring on under a new hit, up to this many sounding at once.
constexpr int kCymbalVoices = 2;

// TR-808 cymbal / hi-hat oscillator frequencies.
constexpr float kMetalHz[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};

// Random velocity spread per lane (DrumVoice order), +-: hats and cymbals
// vary the most, kick and snare less, the click not at all.
constexpr float kSpread[kDrumVoices] = {0.08f, 0.08f, 0.08f, 0.15f, 0.15f, 0.15f, 0.0f, 0.08f, 0.08f, 0.08f};
// Pan per lane, -1 left .. +1 right, from the drummer's seat: hats, crash
// and rack tom left, ride and floor tom right. Balance law: the near side
// stays at full level.
constexpr float kPan[kDrumVoices] = {0.0f, -0.1f, -0.1f, -0.35f, -0.35f, 0.4f, 0.0f, -0.3f, -0.15f, 0.35f};
// Synth toms: base pitch (Hz; it starts 1.5x higher and falls) and decay.
constexpr float kTomHz[2] = {150.0f, 92.0f}, kTomT60[2] = {0.45f, 0.6f}, kTomGain = 0.8f;
constexpr float panL(int v) { return kPan[v] > 0.0f ? 1.0f - kPan[v] : 1.0f; }
constexpr float panR(int v) { return kPan[v] < 0.0f ? 1.0f + kPan[v] : 1.0f; }
// A sample plays at most this much above its layer's own velocity.
constexpr float kMaxLayerGain = 1.4f;

// Drum-bus compressor: stereo-linked peak detector with a lookahead
// (DrumMachine::kLatency), 1 ms attack, 200 ms release (slow enough not to
// pump), 4:1 above -24 dBFS with a 6 dB soft knee, and +16 dB makeup. It is
// blended in parallel with the natural bus (setBusComp), which brings the
// stick attacks back; a lookahead peak limiter keeps the bus at -0.5 dBFS.
constexpr float kCompThresholdDb = -24.0f, kCompRatio = 4.0f, kCompKneeDb = 6.0f, kCompMakeupDb = 16.0f;
constexpr float kCompAttackMs = 1.0f, kCompReleaseMs = 200.0f;
constexpr float kLimitCeiling = 0.94f, kLimitReleaseMs = 60.0f;  // -0.5 dBFS
constexpr int kCompEvery = 8;  // samples between gain computations (interpolated)

// The compressor's gain (linear, makeup included) for an envelope level.
float compGain(float env) {
  const float db = env > 1e-6f ? 20.0f * log10f(env) : -120.0f;
  const float over = db - kCompThresholdDb;
  float gr = 0.0f;  // gain reduction, dB (<= 0)
  if (over >= 0.5f * kCompKneeDb) {
    gr = over * (1.0f / kCompRatio - 1.0f);
  } else if (over > -0.5f * kCompKneeDb) {
    const float k = over + 0.5f * kCompKneeDb;
    gr = (1.0f / kCompRatio - 1.0f) * k * k / (2.0f * kCompKneeDb);
  }
  return powf(10.0f, (gr + kCompMakeupDb) * (1.0f / 20.0f));
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
  for (int t = 0; t < 2; t++) tomAmpK_[t] = decayK(kTomT60[t], fs_);
  tomPitchK_ = decayK(0.15f, fs_);
  clickHi_ = inc(2000.0f);
  clickLo_ = inc(1200.0f);
  limRel_ = 1.0f - expf(-1.0f / (kLimitReleaseMs * 0.001f * fs_));
  compAtt_ = 1.0f - expf(-1.0f / (kCompAttackMs * 0.001f * fs_));
  compRel_ = 1.0f - expf(-1.0f / (kCompReleaseMs * 0.001f * fs_));
  resetComp();
  setVolume(0.6f);
  setStyle(0);
  swapPattern();
}

// ---- Control (background) ------------------------------------------------------
//
// IRDSP_COLD (flash) also goes on the interrupt's occasional work, a hit, a
// bar line, a bass note, where a cache miss costs a microsecond or so; the
// per-sample loops (renderVoices, bus, PluckBass::process) stay in RAM1.

IRDSP_COLD void DrumMachine::setBassBuffer(float* buf) {
  if (!buf) return;
  bass_.init(fs_, buf);
  bassLa_ = buf + 2 * PluckBass::kLineLen;
  bassBlk_ = bassLa_ + kLatency;
  for (int i = 0; i < kLatency + kMaxBlock; i++) bassLa_[i] = 0.0f;
}

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
      p.vel[v][i] = c >= '1' && c <= '9' ? c - '0' : (c == 'X' ? 9 : (c == 'x' ? 6 : (c == 'g' ? 3 : 0)));
    }
    if (v != kClick && len > 0) p.drums = true;
  }
  const int bl = (int)strlen(s.bass);
  p.bass = bl > 0;
  for (int i = 0; i < kMaxSteps; i++) {
    const char c = i < bl && i < p.steps ? s.bass[i] : '-';
    int8_t code = kBassHold;
    switch (c) {
      case 'R': code = 0; break;
      case '3': code = 3; break;
      case 'M': code = 4; break;
      case '4': code = 5; break;
      case '5': code = 7; break;
      case '6': code = 9; break;
      case '7': code = 10; break;
      case 'O': code = 12; break;
      case '.': code = kBassMute; break;
      case 'x': code = kBassDead; break;
      default: break;
    }
    p.bassStep[i] = code;
  }
  styleReq_ = idx;
  swapReq_ = true;
}

IRDSP_COLD void DrumMachine::setTempo(float bpm) {
  baseBpm_ = bpm < kMinBpm ? kMinBpm : (bpm > kMaxBpm ? kMaxBpm : bpm);
  tempoReq_ = true;
}

// 0 dB at 1, -30 dB near 0 on a curve that spends more travel at the top.
static IRDSP_COLD float volumeGain(float n) {
  n = clamp01(n);
  const float d = 1.0f - n;
  return n <= 0.0f ? 0.0f : powf(10.0f, -30.0f * d * sqrtf(d) / 20.0f);
}

IRDSP_COLD void DrumMachine::setVolume(float n) { volTarget_ = volumeGain(n); }

IRDSP_COLD void DrumMachine::setBassVolume(float n) { bassVolTarget_ = volumeGain(n); }

IRDSP_COLD void DrumMachine::setTrainer(bool on, int stepBpm, int everyBars, int targetBpm) {
  trStep_ = stepBpm < 1 ? 1 : stepBpm;
  trEvery_ = everyBars < 1 ? 1 : everyBars;
  trTarget_ = targetBpm;
  trOn_ = on;
  tempoReq_ = true;  // restart from the set tempo
}

// ---- Audio ---------------------------------------------------------------------

bool DrumMachine::voicesActive() const {
  return kickAmp_ > 0.0f || kickClick_ > 0.0f || tomAmp_[0] > 0.0f || tomAmp_[1] > 0.0f || snBody_ > 0.0f || snNoise_ > 0.0f ||
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

IRDSP_COLD void DrumMachine::trigger(int voice, int code, uint64_t sample) {
  if (hook_) hook_(hookCtx_, voice, code, sample);
  // Humanize: triangular noise, so small changes are common, big ones rare.
  const float t = 0.5f * (noise() + noise());
  float vel = (float)code * (1.0f / 9.0f) * (1.0f + kSpread[voice] * humanize_ * t);
  if (vel < 0.0f) vel = 0.0f;
  if (kit_ && voice != kClick && kit_->count[voice]) {
    startSample(voice, vel);
    return;
  }
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
    case kTomHi:
    case kTomLo: {
      const int t = voice - kTomHi;
      tomAmp_[t] = vel;
      tomPitch_[t] = 1.0f;
      tomClick_[t] = vel;
      tomPhase_[t] = 0;
      break;
    }
    case kClick:
      clickAmp_ = vel;
      clickInc_ = code == 9 ? clickHi_ : clickLo_;
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
    if (step_ % p.stepsPerBeat == 0) trigger(kClick, step_ == 0 ? 9 : 6, sample);
    if (step_ == 0) styleBass(kBassMute, sample);  // count-ins are clicks alone
  } else {
    if (p.bass) styleBass(p.bassStep[step_], sample);
    else if (step_ == 0) styleBass(kBassMute, sample);
    // A crash accent replaces the bar's first hat or ride hit.
    const int every = crashEvery_;
    const bool crash = step_ == 0 && every > 0 && p.drums && phrase_ % every == 0;
    if (crash) trigger(kCrash, 9, sample);
    for (int v = 0; v < kDrumVoices; v++) {
      const int lvl = p.vel[v][step_];
      if (!lvl || (crash && (v == kHatClosed || v == kHatOpen || v == kRide || v == kCrash))) continue;
      trigger(v, lvl, sample);
    }
  }
  step_++;
}

// Song mode: one tick (kSongPpq per quarter note).
void DrumMachine::fireTick(uint64_t sample) {
  const DrumSong& s = *song_;
  if (countIn_) {
    // Clicks on the beats of the bar the song starts from.
    const SongBar& b = s.bars[songBar_];
    const uint32_t beat = (uint32_t)kSongPpq * 4u / b.beatUnit;
    if (ciTick_ >= songBarTicks(b)) {
      ciTick_ = 0;
      if (--countLeft_ <= 0) countIn_ = false;
    }
    if (countIn_) {
      if (ciTick_ % beat == 0) trigger(kClick, ciTick_ == 0 ? 9 : 6, sample);
      ciTick_++;
      return;
    }
  }
  if (tick_ >= barEnd_) {
    // The next bar: the loop's first after its last, the first after the end.
    bar_ = bar_ + 1;
    int next = songBar_ + 1;
    const int a = loopA_ - 1, b = loopB_ - 1;
    if (b >= 0 && (next > b || next < a)) next = a;
    if (next >= (int)s.barCount) next = a >= 0 && a < (int)s.barCount ? a : 0;
    songSeek(next);
  }
  while (evIdx_ < s.eventCount && s.events[evIdx_].tick() <= tick_) {
    const SongEvent e = s.events[evIdx_++];
    if (e.tick() == tick_ && e.lane() < kDrumVoices && e.vel()) trigger(e.lane(), e.vel(), sample);
  }
  while (bassIdx_ < s.bassCount && s.bass[bassIdx_].tick() <= tick_) {
    const BassEvent e = s.bass[bassIdx_++];
    if (e.tick() == tick_) playBass(e, sample);
  }
  tick_++;
}

// A style's bass step: a degree of the key (the root sounds from E1 to D#2),
// a dead note on the last pitch, a mute, or nothing (hold). The first step
// of a bar is accented.
IRDSP_COLD void DrumMachine::styleBass(int code, uint64_t sample) {
  if (code == kBassHold) return;
  if (code == kBassMute) {
    if (bassLa_ && bass_.active()) playBass(bassEvent(0, 0, 0), sample);
    return;
  }
  if (code != kBassDead) bassLast_ = bassRoot(bassKey_) + code;
  const int vel = code == kBassDead ? 5 : (step_ == 0 ? 8 : 7);
  playBass(bassEvent(0, bassLast_, vel, code == kBassDead), sample);
}

// A bass event: a note (dead or not) or a mute (note 0).
IRDSP_COLD void DrumMachine::playBass(const BassEvent& e, uint64_t sample) {
  if (!bassOn_ || !bassLa_) return;
  if (hook_) hook_(hookCtx_, kBassHookVoice, e.note() + (e.dead() ? 128 : 0), sample);
  if (e.note() == 0) bass_.mute();
  else bass_.noteOn(e.note(), e.vel(), e.dead());
}

void DrumMachine::songSeek(int bar) {
  const SongBar& b = song_->bars[bar];
  songBar_ = bar;
  tick_ = b.tick;
  evIdx_ = b.firstEvent;
  bassIdx_ = b.firstBass;
  barEnd_ = b.tick + songBarTicks(b);
}

// Read from the background while the interrupt runs: each field is read
// once, and the song bar is clamped, so a torn read only shows a wrong beat.
IRDSP_COLD void DrumMachine::beatPos(int* beat, int* beats) const {
  const bool playing = playing_;
  const DrumSong* s = playing ? song_ : songReq_;
  if (s && s->barCount) {
    int bar = playing ? songBar_ : 0;
    if (bar < 0 || bar >= (int)s->barCount) bar = 0;
    const SongBar& b = s->bars[bar];
    const uint32_t perBeat = (uint32_t)kSongPpq * 4u / b.beatUnit;
    // tick_ and ciTick_ are one past the tick that sounded last.
    const uint32_t t = countIn_ ? ciTick_ : tick_ - b.tick;
    *beats = b.beats;
    *beat = !playing ? -1 : (t > 0 && t <= songBarTicks(b) ? (int)((t - 1) / perBeat) : 0);
    return;
  }
  const Pattern& p = pat_[cur_];
  const int step = step_;
  *beats = p.beats;
  *beat = !playing ? -1 : (step > 0 && step <= p.steps ? (step - 1) / p.stepsPerBeat : 0);
}

IRDSP_COLD void DrumMachine::endBar() {
  if (countIn_) {
    if (--countLeft_ <= 0) {
      countIn_ = false;
      phrase_ = 0;  // the groove (re)starts: crash accents count from here
    }
  } else {
    bar_ = bar_ + 1;
    phrase_++;
    if (trOn_ && bpmNow_ < (float)trTarget_ && ++trBars_ >= trEvery_) {
      trBars_ = 0;
      const float b = bpmNow_ + (float)trStep_;
      bpmNow_ = b > (float)trTarget_ ? (float)trTarget_ : b;
      // A count-in at the new tempo before the groove goes on.
      countLeft_ = countInBars_;
      countIn_ = countLeft_ > 0;
    }
  }
  if (swapReq_) swapPattern();
}

void DrumMachine::renderVoices(float* outL, float* outR, int n) {
  for (int i = 0; i < n; i++) {
    float l = 0.0f, r = 0.0f;
    if (kickAmp_ > 0.0f || kickClick_ > 0.0f) {
      kickPhase_ += inc(50.0f + 110.0f * kickPitch_);
      kickPitch_ *= kickPitchK_;
      const float x = kKickGain * (sine(kickPhase_) * kickAmp_ + 0.4f * noise() * kickClick_);
      l += panL(kKick) * x;
      r += panR(kKick) * x;
      kickAmp_ *= kickAmpK_;
      kickClick_ *= kickClickK_;
      if (kickAmp_ < kOff) kickAmp_ = 0.0f;
      if (kickClick_ < kOff) kickClick_ = 0.0f;
    }
    for (int t = 0; t < 2; t++) {
      if (tomAmp_[t] <= 0.0f) continue;
      tomPhase_[t] += inc(kTomHz[t] * (1.0f + 0.5f * tomPitch_[t]));
      tomPitch_[t] *= tomPitchK_;
      const float x = kTomGain * (sine(tomPhase_[t]) * tomAmp_[t] + 0.3f * noise() * tomClick_[t]);
      l += panL(kTomHi + t) * x;
      r += panR(kTomHi + t) * x;
      tomAmp_[t] *= tomAmpK_[t];
      tomClick_[t] *= kickClickK_;
      if (tomAmp_[t] < kOff) tomAmp_[t] = tomClick_[t] = 0.0f;
    }
    if (snBody_ > 0.0f || snNoise_ > 0.0f) {
      snPhase_ += snInc_;
      const float nz = snLp_.process(snHp_.process(noise()));
      const float x = kSnareBody * sine(snPhase_) * snBody_ + kSnareNoise * 1.6f * nz * snNoise_;
      l += panL(kSnare) * x;
      r += panR(kSnare) * x;
      snBody_ *= snBodyK_;
      snNoise_ *= snNoiseK_;
      if (snBody_ < kOff) snBody_ = 0.0f;
      if (snNoise_ < kOff) snNoise_ = 0.0f;
    }
    if (rimAmp_ > 0.0f || rimTick_ > 0.0f) {
      rimPhase1_ += rimInc1_;
      rimPhase2_ += rimInc2_;
      const float x = kRimGain * ((sine(rimPhase1_) + 0.6f * sine(rimPhase2_)) * 0.6f * rimAmp_ +
                                  rimHp_.process(noise()) * rimTick_);
      l += panL(kRim) * x;
      r += panR(kRim) * x;
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
        const float x = kHatGain * 1.8f * hat * (hatC_ + hatO_);
        l += panL(kHatClosed) * x;
        r += panR(kHatClosed) * x;
        hatC_ *= hatCK_;
        hatO_ *= hatOK_;
        if (hatC_ < kOff) hatC_ = 0.0f;
        if (hatO_ < kOff) hatO_ = 0.0f;
      }
      if (ride_ > 0.0f) {
        const float x = kRideGain * 1.5f * rideBp_.process(metal + 0.25f * nz) * ride_;
        l += panL(kRide) * x;
        r += panR(kRide) * x;
        ride_ *= rideK_;
        if (ride_ < kOff) ride_ = 0.0f;
      }
      if (crash_ > 0.0f) {
        const float x = kCrashGain * 1.8f * crashHp2_.process(crashHp1_.process(0.5f * metal + 0.8f * nz)) * crash_;
        l += panL(kCrash) * x;
        r += panR(kCrash) * x;
        crash_ *= crashK_;
        if (crash_ < kOff) crash_ = 0.0f;
      }
    }
    if (clickAmp_ > 0.0f) {
      const float x = kClickGain * sine(clickPhase_ += clickInc_) * clickAmp_;
      l += x;  // centered
      r += x;
      clickAmp_ *= clickAmpK_;
      if (clickAmp_ < kOff) clickAmp_ = 0.0f;
    }
    outL[i] = l;
    outR[i] = r;
  }
  renderSamples(outL, outR, n);
}

// Bus: compressor, volume, the bass, peak limiter. The compressor's detector
// hears the drums kLatency samples before they play (a lookahead delay), so
// its gain is already coming down when a hit arrives; the bass, delayed the
// same, joins after it (the kick doesn't pump it) and the limiter, also
// looking ahead, keeps the sum at the ceiling. Volumes ramp across the
// block. The compressor's gain is recomputed every few samples and ramped
// in between.
void DrumMachine::bus(float* outL, float* outR, int n) {
  const float v0 = volCur_, vStep = (volTarget_ - v0) / (float)n;
  const float b0 = bassVolCur_, bStep = (bassVolTarget_ - b0) / (float)n;
  volCur_ = volTarget_;
  bassVolCur_ = bassVolTarget_;
  const float* bassIn = bassBlk_;
  float* bassLa = bassLa_;
  if (!busDynamics_) {  // tests: the plain bus, still delayed
    for (int i = 0; i < n; i++) {
      const float inL = kBusGain * outL[i], inR = kBusGain * outR[i];
      const float bIn = bassIn ? bassIn[i] : 0.0f;
      laQuiet_ = inL == 0.0f && inR == 0.0f && bIn == 0.0f ? (laQuiet_ < kLatency ? laQuiet_ + 1 : kLatency) : 0;
      const float vol = v0 + vStep * (float)(i + 1), bv = b0 + bStep * (float)(i + 1);
      const float bd = bassLa ? bv * bassLa[laPos_] : 0.0f;
      outL[i] = vol * laL_[laPos_] + bd;
      outR[i] = vol * laR_[laPos_] + bd;
      laL_[laPos_] = inL;
      laR_[laPos_] = inR;
      if (bassLa) bassLa[laPos_] = bIn;
      if (++laPos_ == kLatency) laPos_ = 0;
    }
    return;
  }
  // Parallel ("New York") compression: the natural bus and the compressed
  // one are the same delayed signal, so their blend (1-k) dry + k comp is a
  // single gain, (1-k) + k * compressor gain. k glides to the setting.
  const float k0 = compMix_, k1 = compMixTarget_, kStep = (k1 - k0) / (float)n;
  for (int i = 0; i < n; i++) {
    const float inL = kBusGain * outL[i], inR = kBusGain * outR[i];
    const float x = fmaxf(fabsf(inL), fabsf(inR));
    const float bIn = bassIn ? bassIn[i] : 0.0f;
    const float k = k0 + kStep * (float)(i + 1);
    const float vol = v0 + vStep * (float)(i + 1), bv = b0 + bStep * (float)(i + 1);
    laQuiet_ = x == 0.0f && bIn == 0.0f ? (laQuiet_ < kLatency ? laQuiet_ + 1 : kLatency) : 0;
    compEnv_ += (x > compEnv_ ? compAtt_ : compRel_) * (x - compEnv_);
    if (--compCount_ <= 0) {
      compCount_ = kCompEvery;
      compStep_ = (compGain(compEnv_) - compGainNow_) * (1.0f / kCompEvery);
    }
    compGainNow_ += compStep_;
    const float g = (1.0f - k + k * compGainNow_) * vol;
    // Limiter, also looking ahead: a sample that will be too loud when it
    // plays (at the gains now) ramps the gain down to what it needs by the
    // time it arrives, then holds.
    const float pred = x * g + fabsf(bIn) * bv;
    if (pred > kLimitCeiling) {
      const float need = kLimitCeiling / pred;
      if (need < limTarget_) {
        limTarget_ = need;
        limStep_ = (need - limGain_) * (1.0f / kLatency);
      }
      limHold_ = kLatency;
    }
    if (limHold_ > 0) {
      limHold_--;
      if (limGain_ > limTarget_) limGain_ = fmaxf(limGain_ + limStep_, limTarget_);
    } else {
      limTarget_ = 1.0f;
      limGain_ += (1.0f - limGain_) * limRel_;
    }
    const float bd = bassLa ? bv * bassLa[laPos_] : 0.0f;
    const float l = g * laL_[laPos_] + bd, r = g * laR_[laPos_] + bd;
    laL_[laPos_] = inL;
    laR_[laPos_] = inR;
    if (bassLa) bassLa[laPos_] = bIn;
    if (++laPos_ == kLatency) laPos_ = 0;
    // Safety net: never above the ceiling, even if the prediction was low.
    const float peak = fmaxf(fabsf(l), fabsf(r)) * limGain_;
    if (peak > kLimitCeiling) limGain_ *= kLimitCeiling / peak;
    outL[i] = limGain_ * l;
    outR[i] = limGain_ * r;
  }
  compMix_ = k1;
}

IRDSP_COLD void DrumMachine::resetComp() {
  compEnv_ = 0.0f;
  compGainNow_ = compGain(0.0f);
  compStep_ = 0.0f;
  compCount_ = 0;
  limGain_ = limTarget_ = 1.0f;
  limStep_ = 0.0f;
  limHold_ = 0;
  for (int i = 0; i < kLatency; i++) laL_[i] = laR_[i] = 0.0f;
  laPos_ = 0;
  laQuiet_ = kLatency;
}

// ---- Sample voices ---------------------------------------------------------------

IRDSP_COLD void DrumMachine::fadeVoice(int i, float ms) {
  SampleVoice& v = sv_[i];
  const float step = v.gain / (ms * 0.001f * fs_);
  if (v.fade == 0.0f || step > v.fade) v.fade = step;
}

// Fades out the lane's sounding voices, except the newest `keep` that aren't
// already fading.
IRDSP_COLD void DrumMachine::fadeLane(int voice, float ms, int keep) {
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

IRDSP_COLD void DrumMachine::startSample(int voice, float vel) {
  const DrumKit& k = *kit_;
  // The layer recorded nearest this velocity (the softer one on a tie).
  const float v9 = vel * 9.0f;
  const DrumSample* s = &k.samples[k.first[voice]];
  for (int j = 1; j < k.count[voice]; j++) {
    const DrumSample& c = k.samples[k.first[voice] + j];
    if (fabsf((float)c.vel - v9) < fabsf((float)s->vel - v9)) s = &c;
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
  const float g = v9 / (float)s->vel;
  v.gain = kSampleGain * (g > kMaxLayerGain ? kMaxLayerGain : g);
  v.fade = 0.0f;
  v.panL = panL(voice);
  v.panR = panR(voice);
  v.lane = (uint8_t)voice;
}

void DrumMachine::renderSamples(float* outL, float* outR, int n) {
  for (SampleVoice& v : sv_) {
    if (!v.p) continue;
    const int16_t* p = v.p + v.pos;
    const uint32_t left = v.len - v.pos;
    int m = left < (uint32_t)n ? (int)left : n;
    float g = v.gain;
    const float pl = v.panL, pr = v.panR;
    bool done = (uint32_t)m == left;
    if (v.fade == 0.0f) {
      const float gl = g * pl, gr = g * pr;
      for (int i = 0; i < m; i++) {
        const float x = (float)p[i];
        outL[i] += gl * x;
        outR[i] += gr * x;
      }
    } else {
      for (int i = 0; i < m; i++) {
        g -= v.fade;
        if (g <= 0.0f) {
          done = true;
          break;
        }
        const float x = g * (float)p[i];
        outL[i] += pl * x;
        outR[i] += pr * x;
      }
      v.gain = g;
    }
    v.pos += (uint32_t)m;
    if (done) v.p = nullptr;
  }
}

bool DrumMachine::process(float* outL, float* outR, int n) {
  bool any = false;
  while (n > kMaxBlock) {
    any |= render(outL, outR, kMaxBlock);
    outL += kMaxBlock;
    outR += kMaxBlock;
    n -= kMaxBlock;
  }
  return render(outL, outR, n) || any;
}

bool DrumMachine::render(float* outL, float* outR, int n) {
  if (startReq_) {
    startReq_ = false;
    if (bassLa_) bass_.mute();
    if (swapReq_) swapPattern();
    step_ = 0;
    bar_ = 0;
    phrase_ = 0;
    // Song mode from the song's (or the loop's) first bar.
    song_ = songReq_ && songReq_->barCount ? songReq_ : nullptr;
    if (song_) {
      const int a = loopA_ - 1;
      songSeek(loopB_ > 0 && a >= 0 && a < (int)song_->barCount ? a : 0);
      ciTick_ = 0;
    }
    trBars_ = 0;
    countLeft_ = countInBars_;
    countIn_ = countLeft_ > 0;
    bpmNow_ = baseBpm_;
    tempoReq_ = false;
    toNext_ = 0.0;
    if (!active_) volCur_ = volTarget_;  // from silence: no ramp needed
    playing_ = true;
  }
  if (stopReq_) {
    stopReq_ = false;
    playing_ = false;
    countIn_ = false;
    if (bassLa_) bass_.mute();
  }
  if (bassLa_ && bassToneReq_ != bassTone_) bass_.setTone(bassTone_ = bassToneReq_);
  if (!bassOn_ && bassLa_) bass_.mute();
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
  if (!playing_ && !voicesActive() && !(bassLa_ && bass_.active()) && laQuiet_ >= kLatency) {
    active_ = false;
    volCur_ = volTarget_;
    bassVolCur_ = bassVolTarget_;
    resetComp();
    time_ += n;
    return false;
  }
  active_ = true;

  if (bassLa_)
    for (int j = 0; j < n; j++) bassBlk_[j] = 0.0f;
  int i = 0;
  while (i < n) {
    if (playing_ && toNext_ <= 0.0) {
      if (song_) {
        fireTick(time_ + i);
        toNext_ += (double)fs_ * 60.0 / ((double)bpmNow_ * kSongPpq);
      } else {
        fireStep(time_ + i);
        toNext_ += (double)fs_ * 60.0 / ((double)bpmNow_ * pat_[cur_].stepsPerBeat);
      }
    }
    int run = n - i;
    if (playing_) {
      const int k = (int)ceil(toNext_);
      if (k < run) run = k;
    }
    renderVoices(outL + i, outR + i, run);
    if (bassLa_) bass_.process(bassBlk_ + i, run);
    toNext_ -= run;
    i += run;
  }

  time_ += n;
  bus(outL, outR, n);
  return true;
}

}  // namespace irdsp
