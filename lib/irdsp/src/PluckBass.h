// PluckBass.h - a monophonic electric bass from a plucked-string model
// (Karplus-Strong), for the drum machine's bass lines.
//
//   pluck: a triangle (the string pulled at 1/5 of its length) plus a trace
//          of noise, low-passed (zero phase, around the period) to as many
//          harmonics as the string keeps, more for hard notes -> the
//          string's delay line; a 1.5 ms attack ramp. A pluck brighter
//          than the string would be a click followed by a dull tone.
//   string: delay line -> allpass (fractional delay, exact tuning) ->
//          one-pole low-pass at a multiple of the note's frequency (Tone:
//          how many harmonics last; the higher ones die within a fraction
//          of a second on every note, as on a real bass) -> loop gain
//          (sustain: low notes ring longest) -> back in
//   output: x envelope -> DC blocker -> body low-pass -> gentle saturation
//          (1-2 % at normal levels)
//
// Muting is an envelope on the string's output, smooth per sample: the loop
// gain only acts once per period (24 ms for E1), so damping through it
// would drop the level in steps, which click.
// Two strings ("slots"): a new note plucks the idle one and fades the other
// out (~30 ms), so notes change without pitch glides or clicks. mute() fades
// the sounding note (~80 ms to -60 dB) like a lifted finger; a dead note is
// a pluck faded from the start, the percussive thump.
//
// noteOn(), mute() and setTone() may run in the audio interrupt between
// process() calls; they fill or retune a string and live in flash
// (IRDSP_COLD), only process() is hot. The delay lines are a buffer the
// caller provides (DMAMEM on the Teensy).
#pragma once

#include <stdint.h>

#include "irdsp_blocks.h"

namespace irdsp {

class PluckBass {
 public:
  static constexpr int kLineLen = 1536;            // per string: B0 at 48 kHz fits
  static constexpr int kMinNote = 23, kMaxNote = 55;  // B0 .. G3 (MIDI)

  // buf: 2 * kLineLen floats. Call before anything else (not in the audio
  // interrupt).
  void init(float sampleRate, float* buf);
  void setTone(float t);  // 0 = dark (flatwound) .. 1 = bright (roundwound, pick)
  // vel 1..9. A note outside kMinNote..kMaxNote is moved by octaves into it.
  void noteOn(int note, int vel, bool dead = false);
  void mute();
  bool active() const { return slot_[0].on || slot_[1].on; }
  // Adds the bass (mono) to out[0..n-1]; false (and nothing added) when
  // silent.
  bool process(float* out, int n);

  // The note's frequency (equal temperament, A4 = 440 Hz).
  static float noteHz(int note);

 private:
  struct Slot {
    float* line = nullptr;
    int len = 0;           // whole samples of delay
    int w = 0;             // write index
    float ap = 0.0f;       // allpass coefficient (the fractional delay)
    float apX = 0.0f, apY = 0.0f;
    float lpA = 1.0f, lpY = 0.0f;  // loop low-pass
    float gain = 0.0f;     // loop gain per pass
    float hz = 0.0f;
    float env = 0.0f, envMul = 1.0f;  // output envelope, its factor per sample
    float att = 1.0f;                 // attack ramp 0 -> 1
    float peak = 0.0f;     // for idle detection
    bool on = false;
  };
  void tune(Slot& s, float hz, float t60);
  float fadeMul(float t60) const;
  // Harmonics the string keeps (its loop low-pass, in multiples of the
  // note): 6 (Tone 0) .. 36 (Tone 1).
  float harmonics() const { return 6.0f + 30.0f * tone_ * tone_; }

  void pluck(Slot& s, int vel, bool dead);
  inline float noise() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (float)(int32_t)rng_ * (1.0f / 2147483648.0f);
  }

  float fs_ = 44100.0f, tone_ = 0.5f, attStep_ = 0.015f;
  Slot slot_[2];
  int cur_ = 0;
  float dcX_ = 0.0f, dcY_ = 0.0f, dcR_ = 0.997f;  // DC blocker
  Biquad body_;
  uint32_t rng_ = 0x2545F491u;
};

}  // namespace irdsp
