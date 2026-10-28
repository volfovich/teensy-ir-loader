#include "Meter.h"

#include <math.h>

#include "ui_port.h"

namespace ui {

UI_COLD float toDb(float lin) { return lin > 1e-6f ? 20.0f * log10f(lin) : -120.0f; }

UI_COLD void MeterState::update(float peak, bool clipped, uint32_t now) {
  const float db = toDb(peak);
  const float fall = started ? kFallDbPerS * (float)(now - lastMs) * 0.001f : 1000.0f;
  started = true;
  lastMs = now;
  levelDb = db > levelDb - fall ? db : levelDb - fall;
  if (db >= peakDb) {
    peakDb = db;
    peakAt = now;
  } else if (now - peakAt > kHoldMs) {
    peakDb -= fall;
    if (peakDb < levelDb) peakDb = levelDb;
  }
  if (clipped) clipUntil = now + kClipMs;
}

// x offset (0 .. w) of a level on a bar w pixels wide.
static UI_COLD int barX(float db, int w) {
  const float t = (db - MeterState::kFloorDb) / -MeterState::kFloorDb;
  const int x = (int)lroundf(t * (float)w);
  return x < 0 ? 0 : (x > w ? w : x);
}

// Bar at (x, y), w x h: filled up to the level, the empty part outlined top
// and bottom, ticks on the empty part, a full-height peak marker.
static UI_COLD void drawBar(Canvas& c, int x, int y, int w, int h, const MeterState& m) {
  static const float UI_PROGMEM kTicks[] = {-24.0f, -12.0f, -6.0f, -3.0f};
  const int lv = barX(m.levelDb, w);
  c.fill(x, y, w, h, false);
  c.fill(x, y, lv, h, true);
  c.hline(x + lv, x + w - 1, y);
  c.hline(x + lv, x + w - 1, y + h - 1);
  c.vline(x + w - 1, y, y + h - 1);
  for (float t : kTicks) {
    const int tx = x + barX(t, w);
    if (tx >= x + lv) c.vline(tx, y + h - 3, y + h - 1);
  }
  const int px = barX(m.peakDb, w);  // never below the level
  if (m.peakDb > MeterState::kFloorDb) c.vline(x + (px >= w ? w - 1 : px), y, y + h - 1);
}

UI_COLD void drawMeterBar(Canvas& c, int x, int page, const MeterState& m, bool zone) {
  const int y = page * 8 + 2, w = Canvas::kW - x, h = 12;
  drawBar(c, x, y, w, h, m);
  if (zone) {
    // Two dotted lines across -6 .. -3 dBFS, inverted so they show on both
    // the filled and the empty part of the bar.
    const int z0 = x + barX(-6.0f, w), z1 = x + barX(-3.0f, w);
    for (int i = z0; i <= z1; i += 2) {
      c.pixel(i, y + 3, !c.get(i, y + 3));
      c.pixel(i, y + h - 4, !c.get(i, y + h - 4));
    }
  }
}

}  // namespace ui
