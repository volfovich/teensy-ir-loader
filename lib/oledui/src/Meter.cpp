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

UI_COLD void drawRowMeter(Canvas& c, int page, const char* label, const MeterState& m, uint32_t now) {
  c.clearPages(page, page);
  c.text(0, page, label);
  const int x = 26, w = Canvas::kW - x;
  drawBar(c, x, page * 8 + 1, w, 6, m);
  if (m.clipping(now)) {
    const char* clip = UI_STR("CLIP");
    const int cx = Canvas::kW - Canvas::width(clip) - 1;
    c.fill(cx - 1, page * 8, Canvas::kW - cx + 1, 8, false);
    c.text(cx, page, clip);
    c.invertText(cx - 1, page, Canvas::kW - cx + 1);
  }
}

UI_COLD void drawWideMeter(Canvas& c, int page, const MeterState& m, bool zone) {
  c.clearPages(page, page);
  drawBar(c, 0, page * 8, Canvas::kW, 8, m);
  if (zone) {
    // Two dotted lines across -6 .. -3 dBFS, inverted so they show on both
    // the filled and the empty part of the bar.
    const int z0 = barX(-6.0f, Canvas::kW), z1 = barX(-3.0f, Canvas::kW);
    for (int x = z0; x <= z1; x += 2) {
      c.pixel(x, page * 8 + 2, !c.get(x, page * 8 + 2));
      c.pixel(x, page * 8 + 5, !c.get(x, page * 8 + 5));
    }
  }
}

}  // namespace ui
