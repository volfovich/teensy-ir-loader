// DrumMachine.h - a small drum machine for practicing along.
//
//   styles (DrumPatterns.cpp) -> sample-accurate step sequencer -> voices
//   (count-in, speed trainer, crash accents)    humanized velocity   |
//          panned stereo drum bus -> parallel compressor -> x volume
//                                                       + bass x bass volume -> limiter
//                                                         (1.5 ms lookahead)
//
// Voices come from the kit (DrumKit.h): either recorded samples, played
// straight from flash, or these synthesized ones. A lane a kit has no
// samples for, and the click, always use the synthesized voice.
//   kick        sine, pitch falling 160 -> 50 Hz, plus a short noise click
//   snare       185 Hz body plus band-limited noise
//   rim         cross-stick: two short sines plus a noise tick
//   hats        TR-808 style metal (6 square waves at inharmonic
//               frequencies) plus noise, high-passed; closed chokes open
//   ride        the same metal, band-passed, long decay
//   crash       the same metal with more noise, high-passed, long decay
//   toms        rack and floor: sines falling in pitch, plus a noise attack
//   click       metronome / count-in: 2 kHz on accents, 1.2 kHz otherwise
//
// Steps fire at their exact sample (a fractional sample counter, so the
// tempo never drifts). A style change takes effect at the next bar, a tempo
// change at the next step. Stopping lets the voices ring out; once they are
// silent the machine is idle and process() costs almost nothing.
//
// Count-in: clicks on the beats for 0-4 bars before the groove, and again
// after every speed-trainer tempo step, at the new tempo.
//
// Crash accents: a crash on the first beat of every N bars (counted from the
// first bar after a count-in), in place of that step's hat or ride hit.
//
// Song mode (setSong): instead of the style's one bar, a multi-bar song
// (DrumSong.h) plays from its start, or the loop's first bar, after the
// count-in, then wraps at its end or at the loop's last bar. Crash accents
// and the speed trainer apply to styles only.
//
// Bass (setBassBuffer, setBassOn): a song's bass line, or the style's in
// the bass key (setBassKey), plays on a plucked-string bass (PluckBass.h),
// on the same clock as the drums, not during count-ins, muted when the
// drums stop. It joins the bus after
// the compressor (the kick doesn't pump it) with its own volume, and the
// limiter keeps the sum below -0.5 dBFS.
//
// Dynamics: every hit has a velocity 1..9 from its lane, varied at random
// per lane (hats and cymbals the most, the click not at all). A sampled
// lane plays the layer recorded nearest that velocity. Each lane has a fixed
// pan (drummer's view: hats and crash left, ride right).
//
// Threading model: process() runs in the audio interrupt; everything else is
// called from the background and only writes single values or the spare
// pattern buffer, which the interrupt swaps in at a bar boundary.
#pragma once

#include <stdint.h>

#include "DrumKit.h"
#include "DrumSong.h"
#include "PluckBass.h"
#include "irdsp_blocks.h"

namespace irdsp {

// A built-in style. Each lane has one character per step:
//   '.' rest, '1'..'9' velocity (9 = hardest); 'g' 'x' 'X' mean 3, 6, 9.
// An empty lane is unused. Lanes are beats x stepsPerBeat long.
// The bass line, one character per step, in degrees of the bass key
// (setBassKey): 'R' root, '3' minor third, 'M' major third, '4' fourth,
// '5' fifth, '6' sixth, '7' minor seventh, 'O' octave; '-' hold, '.' mute,
// 'x' dead note. Empty: no bass.
struct DrumStyle {
  char name[10];    // command name ("shuffle")
  char title[14];   // display name ("blues shuffle")
  uint8_t beats;    // beats per bar
  uint8_t stepsPerBeat;
  char lanes[kDrumVoices][25];
  char bass[25];
};

int drumStyleCount();
const DrumStyle& drumStyle(int i);
// The style named (case-insensitive) or numbered 1..count; -1 if none.
int findDrumStyle(const char* nameOrNumber);

class DrumMachine {
 public:
  static constexpr int kMaxSteps = 24;
  static constexpr float kMinBpm = 40.0f, kMaxBpm = 240.0f;
  // The drum bus is delayed by this many samples (1.5 ms at 44.1 kHz), the
  // drum-bus compressor's lookahead.
  static constexpr int kLatency = 64;
  // process() blocks longer than this are split (the bass's block buffer).
  static constexpr int kMaxBlock = 256;
  // setBassBuffer's size in floats: the bass's strings, its lookahead
  // delay and a block.
  static constexpr int kBassBufLen = 2 * PluckBass::kLineLen + kLatency + kMaxBlock;
  // The hit hook's voice for bass events: vel is the note (0 = mute), +128
  // for a dead note.
  static constexpr int kBassHookVoice = 32;

  explicit DrumMachine(float sampleRate = 44100.0f);

  // ---- Control (background) -------------------------------------------------
  void setStyle(int idx);      // at the next bar (at once when stopped)
  void setTempo(float bpm);    // at the next step; restarts the trainer
  void setVolume(float n);     // 0..1 (0 = silent, 1 = 0 dB)
  // Count-in bars (0 = off): before the groove, and after each trainer step.
  void setCountIn(int bars) { countInBars_ = bars < 0 ? 0 : bars; }
  // The kit to play (kept, not copied); its sounding samples fade out.
  void setKit(const DrumKit* kit) { kitReq_ = kit; }
  // A crash on the first beat of every `bars` bars; 0 = off.
  void setCrashEvery(int bars) { crashEvery_ = bars < 0 ? 0 : bars; }
  // Scales the random velocity spread (1 = as designed, 0 = every hit exact).
  void setHumanize(float amount) { humanize_ = amount < 0.0f ? 0.0f : amount; }
  // The drum-bus compressor and limiter (on by default; tests turn them off
  // to measure exact levels).
  void setBusDynamics(bool on) { busDynamics_ = on; }
  // How much of the compressed drum bus is blended with the natural one,
  // 0..1 (parallel compression): 0 = natural drums (only the limiter),
  // 1 = fully compressed (loudest).
  void setBusComp(float k) { compMixTarget_ = k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k); }
  // Speed trainer: every `everyBars` bars the tempo rises by `stepBpm`, up
  // to `targetBpm`, starting from the set tempo.
  void setTrainer(bool on, int stepBpm, int everyBars, int targetBpm);
  // Song mode with this song (kept, not copied), or nullptr for the style.
  // Takes effect at the next start(); the song must stay valid while the
  // machine is running() it.
  void setSong(const DrumSong* song) { songReq_ = song; }
  // Loop bars first..last (1-based) of the song; 0, 0 = the whole song.
  // Applies from the next bar line.
  void setLoop(int first, int last) {
    loopA_ = first < 1 ? 0 : first;
    loopB_ = first < 1 || last < first ? 0 : last;
  }
  // Bass: buf is kBassBufLen floats (DMAMEM on the Teensy); without one
  // there is no bass. Call before the audio runs.
  void setBassBuffer(float* buf);
  void setBassOn(bool on) { bassOn_ = on; }
  void setBassVolume(float n);  // 0..1, the same curve as setVolume
  void setBassTone(float t) { bassToneReq_ = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }
  // The styles' key, 0 = C .. 11 = B; the root sounds from E1 to D#2.
  void setBassKey(int key) { bassKey_ = ((key % 12) + 12) % 12; }
  // The root note (MIDI) of a key.
  static int bassRoot(int key) { return 28 + (((key - 4) % 12) + 12) % 12; }
  void start() { stopReq_ = false; startReq_ = true; }
  void stop() { startReq_ = false; stopReq_ = true; }

  bool playing() const { return startReq_ || (playing_ && !stopReq_); }
  bool active() const { return active_; }  // playing or still ringing
  bool countingIn() const { return countIn_; }
  int bar() const { return bar_; }          // groove bars played (not count-ins)
  bool running() const { return playing_; }  // the sequencer, as the interrupt sees it
  const DrumSong* song() const { return song_; }  // the song playing (nullptr: style)
  int songBar() const { return songBar_; }   // 0-based bar of the song
  float tempoNow() const { return bpmNow_; }
  // The beat sounding now in the current bar (0-based, count-in bars too;
  // -1 when stopped) and the bar's beats, for beat displays.
  void beatPos(int* beat, int* beats) const;
  int style() const { return styleReq_; }
  int samplesPlaying() const;  // sample voices sounding (for tests)

  // ---- Audio ------------------------------------------------------------------
  // Writes n samples of the stereo drum bus. Returns false, writing nothing,
  // while idle (stopped and silent).
  bool process(float* outL, float* outR, int n);

  // Called (in the audio interrupt) for every hit, with its velocity code
  // (1..9, before humanizing) and the sample it lands on, counted across
  // process() calls. For tests and beat displays.
  using HitHook = void (*)(void* ctx, int voice, int vel, uint64_t sample);
  void setHitHook(HitHook hook, void* ctx) { hookCtx_ = ctx; hook_ = hook; }

 private:
  static constexpr int kSampleVoices = 16;

  struct Pattern {
    uint8_t steps = 16, beats = 4, stepsPerBeat = 4;
    bool drums = false;  // has lanes other than the click (crash accents)
    bool bass = false;   // has a bass line
    uint8_t vel[kDrumVoices][kMaxSteps] = {};  // 0 = rest, 1..9
    int8_t bassStep[kMaxSteps] = {};  // semitones above the root, or kBassHold / kBassMute / kBassDead
  };

  void init();  // the constructor's work, kept in flash (IRDSP_COLD)
  void fireStep(uint64_t sample);
  void fireTick(uint64_t sample);
  void songSeek(int bar);
  void endBar();
  void swapPattern();
  void trigger(int voice, int code, uint64_t sample);
  bool voicesActive() const;
  void renderVoices(float* outL, float* outR, int n);
  void bus(float* outL, float* outR, int n);
  void playBass(const BassEvent& e, uint64_t sample);
  void styleBass(int code, uint64_t sample);
  static constexpr int8_t kBassHold = -1, kBassMute = -2, kBassDead = -3;
  bool render(float* outL, float* outR, int n);
  void startSample(int voice, float vel);
  void fadeLane(int voice, float ms, int keep);
  void fadeVoice(int i, float ms);
  void renderSamples(float* outL, float* outR, int n);
  void resetComp();
  inline float noise() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (float)(int32_t)rng_ * (1.0f / 2147483648.0f);
  }
  inline float sine(uint32_t phase) const {
    const uint32_t i = phase >> 24;
    const float f = (float)(phase & 0x00FFFFFF) * (1.0f / 16777216.0f);
    return sin_[i] + (sin_[i + 1] - sin_[i]) * f - 1.0f;
  }
  uint32_t inc(float hz) const { return (uint32_t)(hz * hzToInc_); }

  const float fs_, hzToInc_;
  const float* sin_;
  uint32_t rng_ = 0x9E3779B9u;

  // Sequencer.
  Pattern pat_[2];
  volatile int cur_ = 0;
  volatile bool swapReq_ = false, startReq_ = false, stopReq_ = false, tempoReq_ = false;
  volatile bool playing_ = false, active_ = false, countIn_ = false;
  volatile int styleReq_ = 0, bar_ = 0, countInBars_ = 1;
  int countLeft_ = 0, phrase_ = 0;  // count-in bars to go; bars since one
  volatile float baseBpm_ = 100.0f, bpmNow_ = 100.0f;
  volatile bool trOn_ = false;
  volatile int trStep_ = 5, trEvery_ = 4, trTarget_ = 140;
  int step_ = 0, trBars_ = 0;
  double toNext_ = 0.0;  // samples until the next step
  uint64_t time_ = 0;    // samples processed
  HitHook hook_ = nullptr;
  void* hookCtx_ = nullptr;
  volatile int crashEvery_ = 0;
  // Song mode.
  const DrumSong* volatile songReq_ = nullptr;
  const DrumSong* song_ = nullptr;
  volatile int loopA_ = 0, loopB_ = 0, songBar_ = 0;
  uint32_t tick_ = 0, barEnd_ = 0, evIdx_ = 0, ciTick_ = 0;
  volatile float humanize_ = 1.0f;
  volatile bool busDynamics_ = true;
  volatile float volTarget_ = 0.0f;
  float volCur_ = 0.0f;
  // Bass.
  PluckBass bass_;
  float* bassLa_ = nullptr;   // its lookahead delay (kLatency), beside the drums'
  float* bassBlk_ = nullptr;  // this block's bass (kMaxBlock)
  volatile bool bassOn_ = true;
  volatile float bassVolTarget_ = 0.7f, bassToneReq_ = 0.5f;
  float bassVolCur_ = 0.7f, bassTone_ = -1.0f;
  uint32_t bassIdx_ = 0;
  volatile int bassKey_ = 4;  // E
  int bassLast_ = 28;         // the style's last pitch (for dead notes)

  // Drum-bus compressor: envelope, gain (with makeup) and its ramp.
  float compAtt_, compRel_, compEnv_ = 0.0f, compGainNow_ = 1.0f, compStep_ = 0.0f;
  float limRel_, limGain_ = 1.0f, limTarget_ = 1.0f, limStep_ = 0.0f;  // peak limiter
  volatile float compMixTarget_ = 0.4f;
  float compMix_ = 0.4f;  // the parallel blend, gliding to the target
  int limHold_ = 0;
  float laL_[kLatency] = {}, laR_[kLatency] = {};  // lookahead delay
  int laPos_ = 0, laQuiet_ = kLatency;  // silent input samples, up to kLatency
  int compCount_ = 0;

  // Voices: amplitude envelopes (0 = silent) and their per-sample decay.
  float kickAmp_ = 0, kickPitch_ = 0, kickClick_ = 0, kickAmpK_, kickPitchK_, kickClickK_;
  uint32_t kickPhase_ = 0;
  float snBody_ = 0, snNoise_ = 0, snBodyK_, snNoiseK_;
  uint32_t snPhase_ = 0, snInc_;
  Biquad snHp_, snLp_;
  float rimAmp_ = 0, rimTick_ = 0, rimAmpK_, rimTickK_;
  uint32_t rimPhase1_ = 0, rimPhase2_ = 0, rimInc1_, rimInc2_;
  Biquad rimHp_;
  float hatC_ = 0, hatO_ = 0, ride_ = 0, hatCK_, hatOK_, rideK_;
  uint32_t metalPhase_[6] = {}, metalInc_[6];
  Biquad hatHp1_, hatHp2_, rideBp_;
  float crash_ = 0, crashK_;
  Biquad crashHp1_, crashHp2_;
  float tomAmp_[2] = {}, tomPitch_[2] = {}, tomClick_[2] = {}, tomAmpK_[2], tomPitchK_;
  uint32_t tomPhase_[2] = {};
  float clickAmp_ = 0, clickAmpK_;
  uint32_t clickPhase_ = 0, clickInc_ = 0, clickHi_, clickLo_;

  // Sample voices: a pointer into the kit's sample, and a fade (gain step
  // per sample) once it is cut off.
  struct SampleVoice {
    const int16_t* p = nullptr;
    uint32_t pos = 0, len = 0, age = 0;
    float gain = 0.0f, fade = 0.0f;
    float panL = 1.0f, panR = 1.0f;
    uint8_t lane = 0;
  };
  const DrumKit* volatile kitReq_ = nullptr;
  const DrumKit* kit_ = nullptr;
  SampleVoice sv_[kSampleVoices];
  uint32_t svAge_ = 0;
};

}  // namespace irdsp
