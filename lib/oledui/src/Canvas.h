// Canvas.h - 128x64 monochrome frame buffer in the SSD1306 page layout
// (8 pages of 128 column bytes, bit 0 = top pixel of the page), with text in
// the 5x7 font, a 2x font, lines, boxes and a dirty mask per page.
//
// Text positions are in pixels across and pages (8-pixel rows) down. The
// small font is 6 px per character (21 per row), the 2x font 12 px wide and
// 2 pages tall (10 per row).
#pragma once

#include <stdint.h>

namespace ui {

class Canvas {
 public:
  static constexpr int kW = 128, kH = 64, kPages = 8;
  static constexpr int kCharW = 6, kBigCharW = 12, kCols = kW / kCharW;

  // buf: kW * kPages bytes (DMAMEM on the Teensy). Call clear() before use.
  explicit Canvas(uint8_t* buf) : buf_(buf) {}

  void clear();                  // all pixels off, all pages dirty
  void clearPages(int p0, int p1);  // pages p0..p1 off

  void pixel(int x, int y, bool on);
  bool get(int x, int y) const;
  void hline(int x0, int x1, int y, bool on = true);
  void vline(int x, int y0, int y1, bool on = true);
  void fill(int x, int y, int w, int h, bool on);
  void frame(int x, int y, int w, int h);  // 1 px outline
  void invert(int x, int y, int w, int h);
  void invertPage(int page) { invert(0, page * 8, kW, 8); }
  // Every other pixel of row y from x0 to x1 (a dotted underline).
  void dots(int x0, int x1, int y);

  // Draws s at x (pixels), page p; returns the x after the last character.
  // Characters past the right edge are dropped. Only the cells drawn are
  // overwritten (their background turns off).
  int text(int x, int page, const char* s, bool big = false);
  // Right-aligned so the text ends at x (exclusive); centered on the screen.
  int textRight(int xEnd, int page, const char* s, bool big = false);
  int textCenter(int page, const char* s, bool big = false);
  static int width(const char* s, bool big = false);

  const uint8_t* page(int p) const { return buf_ + p * kW; }
  uint8_t dirtyMask() const { return dirty_; }
  // The lowest dirty page (and clears its flag), or -1 if none.
  int takeDirtyPage();
  void markAllDirty() { dirty_ = 0xFF; }

 private:
  uint8_t* buf_;
  uint8_t dirty_ = 0xFF;
};

}  // namespace ui
