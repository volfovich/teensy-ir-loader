// Meter.h - level meter ballistics and drawing.
//
//   - Scale -48 .. 0 dBFS, linear in dB.
//   - The bar rises instantly and falls at 20 dB/s.
//   - A peak marker holds for 1.5 s, then falls like the bar.
//   - CLIP shows for at least 2 s after a clip.
#pragma once

#include <stdint.h>

#include "Canvas.h"

namespace ui {

struct MeterState {
  static constexpr float kFloorDb = -48.0f, kFallDbPerS = 20.0f;
  static constexpr uint32_t kHoldMs = 1500, kClipMs = 2000;

  float levelDb = -120.0f, peakDb = -120.0f;
  uint32_t peakAt = 0, clipUntil = 0, lastMs = 0;
  bool started = false;

  // peak: linear peak (1.0 = full scale) since the last update. clipped:
  // a clip happened since the last update.
  void update(float peak, bool clipped, uint32_t nowMs);
  bool clipping(uint32_t nowMs) const { return (int32_t)(clipUntil - nowMs) > 0; }
  // Forgets the peak hold and the clip latch.
  void reset() { peakDb = levelDb; clipUntil = 0; }
};

float toDb(float linear);

// One 8 px row at page p: label (3 characters) then a 100 px bar with
// ticks at -24, -12, -6, -3 and 0 dBFS, the peak marker and CLIP.
void drawRowMeter(Canvas& c, int page, const char* label, const MeterState& m, uint32_t nowMs);
// A full-width 8 px bar at page p, with the -6..-3 dBFS target zone marked
// (for setting the input level).
void drawWideMeter(Canvas& c, int page, const MeterState& m, bool zone);

}  // namespace ui
