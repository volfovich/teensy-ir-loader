// Canvas.h - 128x64 monochrome frame buffer in the SSD1306 page layout
// (8 pages of 128 column bytes, bit 0 = top pixel of the page), with text in
// two fonts, lines, boxes and a dirty mask per page.
//
// Text positions are in pixels across and pages (8-pixel rows) down. The
// normal font is 8x16 (16 characters x 4 rows, 2 pages per row), the big one
// 16x32 (8 characters, 4 pages tall). Spleen fonts, see Fonts.h.
#pragma once

#include <stdint.h>

namespace ui {

enum class Font : uint8_t { kNormal, kBig };

class Canvas {
 public:
  static constexpr int kW = 128, kH = 64, kPages = 8;
  static constexpr int kCharW = 8, kBigCharW = 16, kCols = kW / kCharW;
  static constexpr int kRowPages = 2;  // a row of normal text
  static constexpr int kLeft = 1;      // x of left-aligned rows

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
  // Inverts a row of normal text (pages p and p+1), columns x .. x+w-1.
  void invertText(int x, int page, int w) { invert(x, page * 8, w, kRowPages * 8); }
  void invertRow(int page) { invertText(0, page, kW); }
  // Every other pixel of row y from x0 to x1 (a dotted underline).
  void dots(int x0, int x1, int y);

  // Draws s at x (pixels), its top at page p; returns the x after the last
  // character. Glyphs have their blank column on the right, so rows start
  // at x = 1 (kLeft) to keep inverted text off the edge. Characters past the right edge are dropped. Only the cells
  // drawn are overwritten (their background turns off).
  int text(int x, int page, const char* s, Font f = Font::kNormal);
  // Right-aligned so the text ends at x (exclusive); centered on the screen.
  int textRight(int xEnd, int page, const char* s, Font f = Font::kNormal);
  int textCenter(int page, const char* s, Font f = Font::kNormal);
  static int width(const char* s, Font f = Font::kNormal);

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
