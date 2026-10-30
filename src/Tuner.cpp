#include "Tuner.h"

#include <Arduino.h>

#include "AudioGraph.h"

static constexpr int kWindow = 2048;  // at 24 kHz: 85 ms, two periods of 24 Hz
static constexpr uint32_t kEveryMs = 100;
static constexpr uint32_t kHoldMs = 400;  // a reading stays this long after the note fades

static TunerReading g_reading;
static uint32_t g_lastAt = 0, g_validAt = 0;

void tunerOpen(bool on) {
  convolver.setTuner(on);
  g_reading = TunerReading();
}

bool serviceTuner(uint32_t nowMs) {
  if (!convolver.tunerOn() || nowMs - g_lastAt < kEveryMs) return false;
  g_lastAt = nowMs;
  const bool was = g_reading.valid;
  float window[kWindow];  // 8 KB of stack, only meanwhile (RAM2 is short)
  if (!convolver.tunerSnapshot(window, kWindow)) return false;
  const irdsp::Pitch p = irdsp::PitchDetect::detect(window, kWindow, AudioConvolverIR::kTunerRate);
  if (p.hz > 0.0f) {
    const irdsp::Note n = irdsp::PitchDetect::note(p.hz);
    // The same note again: smooth the cents a little, so the needle settles.
    const bool same = was && n.index == g_reading.note.index && n.octave == g_reading.note.octave;
    const float before = g_reading.note.cents;
    g_reading.note = n;
    if (same) g_reading.note.cents = 0.5f * (before + n.cents);
    g_reading.hz = p.hz;
    g_reading.valid = true;
    g_validAt = nowMs;
    return true;
  }
  if (was && nowMs - g_validAt > kHoldMs) {
    g_reading.valid = false;
    return true;
  }
  return false;
}

const TunerReading& tunerReading() { return g_reading; }
