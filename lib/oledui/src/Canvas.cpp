#include "Canvas.h"

#include <string.h>

#include "Font5x7.h"
#include "ui_port.h"

namespace ui {

UI_COLD void Canvas::clear() {
  memset(buf_, 0, kW * kPages);
  dirty_ = 0xFF;
}

UI_COLD void Canvas::clearPages(int p0, int p1) {
  for (int p = p0; p <= p1; p++) {
    if (p < 0 || p >= kPages) continue;
    memset(buf_ + p * kW, 0, kW);
    dirty_ |= 1 << p;
  }
}

UI_COLD void Canvas::pixel(int x, int y, bool on) {
  if (x < 0 || x >= kW || y < 0 || y >= kH) return;
  uint8_t& b = buf_[(y >> 3) * kW + x];
  const uint8_t m = 1 << (y & 7);
  b = on ? (b | m) : (b & ~m);
  dirty_ |= 1 << (y >> 3);
}

UI_COLD bool Canvas::get(int x, int y) const {
  if (x < 0 || x >= kW || y < 0 || y >= kH) return false;
  return buf_[(y >> 3) * kW + x] & (1 << (y & 7));
}

UI_COLD void Canvas::hline(int x0, int x1, int y, bool on) {
  for (int x = x0; x <= x1; x++) pixel(x, y, on);
}

UI_COLD void Canvas::vline(int x, int y0, int y1, bool on) {
  for (int y = y0; y <= y1; y++) pixel(x, y, on);
}

UI_COLD void Canvas::fill(int x, int y, int w, int h, bool on) {
  for (int j = y; j < y + h; j++) hline(x, x + w - 1, j, on);
}

UI_COLD void Canvas::frame(int x, int y, int w, int h) {
  hline(x, x + w - 1, y);
  hline(x, x + w - 1, y + h - 1);
  vline(x, y, y + h - 1);
  vline(x + w - 1, y, y + h - 1);
}

UI_COLD void Canvas::invert(int x, int y, int w, int h) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++) pixel(i, j, !get(i, j));
}

UI_COLD void Canvas::invertText(int x, int page, int w) {
  if (page < 0 || page >= kPages) return;
  uint8_t* row = buf_ + page * kW;
  for (int i = x < 0 ? 0 : x; i < x + w && i < kW; i++) row[i] = (uint8_t)~(row[i] << 1);
  dirty_ |= 1 << page;
}

UI_COLD void Canvas::dots(int x0, int x1, int y) {
  for (int x = x0; x <= x1; x += 2) pixel(x, y, true);
}

// Spreads the 8 bits of b over 16 (each bit doubled), for the 2x font.
static UI_COLD uint16_t stretch(uint8_t b) {
  uint16_t r = 0;
  for (int i = 0; i < 8; i++)
    if (b & (1 << i)) r |= 3u << (2 * i);
  return r;
}

UI_COLD int Canvas::text(int x, int page, const char* s, bool big) {
  if (page < 0 || page >= kPages || (big && page + 1 >= kPages)) return x;
  const int cw = big ? kBigCharW : kCharW;
  for (; *s; s++, x += cw) {
    if (x < 0) continue;
    // The glyph must fit; its spacing column(s) may fall off the edge.
    if (x + cw - (big ? 2 : 1) > kW) break;
    const uint8_t* g = glyph(*s);
    uint8_t* row = buf_ + page * kW;
    if (!big) {
      for (int c = 0; c < 5; c++) row[x + c] = g[c];
      if (x + 5 < kW) row[x + 5] = 0;
    } else {
      uint8_t* row2 = row + kW;
      for (int c = 0; c < 6; c++) {
        const uint16_t v = c < 5 ? stretch(g[c]) : 0;
        for (int d = 0; d < 2; d++) {
          const int cx = x + 2 * c + d;
          if (cx >= kW) break;
          row[cx] = (uint8_t)v;
          row2[cx] = (uint8_t)(v >> 8);
        }
      }
    }
  }
  dirty_ |= (big ? 3 : 1) << page;
  return x;
}

UI_COLD int Canvas::width(const char* s, bool big) {
  return (int)strlen(s) * (big ? kBigCharW : kCharW);
}

UI_COLD int Canvas::textRight(int xEnd, int page, const char* s, bool big) {
  // The last character's spacing column is dropped, so the ink ends at xEnd.
  return text(xEnd - width(s, big) + (big ? 2 : 1), page, s, big);
}

UI_COLD int Canvas::textCenter(int page, const char* s, bool big) {
  return text((kW - width(s, big) + (big ? 2 : 1)) / 2, page, s, big);
}

UI_COLD int Canvas::takeDirtyPage() {
  for (int p = 0; p < kPages; p++) {
    if (dirty_ & (1 << p)) {
      dirty_ &= ~(1 << p);
      return p;
    }
  }
  return -1;
}

}  // namespace ui
