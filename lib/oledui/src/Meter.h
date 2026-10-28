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

// A bar from x to the right edge on the 16 px text row at page p, with
// ticks at -24, -12, -6 and -3 dBFS and the peak marker; zone marks the
// -6..-3 dBFS target (for setting the input level).
void drawMeterBar(Canvas& c, int x, int page, const MeterState& m, bool zone);

}  // namespace ui
