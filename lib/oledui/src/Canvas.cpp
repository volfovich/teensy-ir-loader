#include "Canvas.h"

#include <string.h>

#include "Fonts.h"
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

UI_COLD void Canvas::dots(int x0, int x1, int y) {
  for (int x = x0; x <= x1; x += 2) pixel(x, y, true);
}

UI_COLD int Canvas::text(int x, int page, const char* s, Font f) {
  const bool big = f == Font::kBig;
  const int cw = big ? kBigCharW : kCharW, pages = big ? 4 : 2;
  if (page < 0 || page + pages > kPages) return x;
  for (; *s; s++, x += cw) {
    if (x < 0) continue;
    // The glyph's last column is blank spacing, so it may fall off the edge.
    if (x + cw - 1 > kW) break;
    const unsigned ch = (unsigned char)*s;
    const int idx = ch >= 32 && ch <= 126 ? (int)ch - 32 : '?' - 32;
    const uint8_t* g = big ? kFont16x32[idx] : kFont8x16[idx];
    const int n = x + cw > kW ? kW - x : cw;
    for (int p = 0; p < pages; p++) memcpy(buf_ + (page + p) * kW + x, g + p * cw, n);
  }
  dirty_ |= ((1 << pages) - 1) << page;
  return x;
}

UI_COLD int Canvas::width(const char* s, Font f) {
  return (int)strlen(s) * (f == Font::kBig ? kBigCharW : kCharW);
}

UI_COLD int Canvas::textRight(int xEnd, int page, const char* s, Font f) {
  return text(xEnd - width(s, f), page, s, f);
}

UI_COLD int Canvas::textCenter(int page, const char* s, Font f) {
  return text((kW - width(s, f)) / 2, page, s, f);
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
