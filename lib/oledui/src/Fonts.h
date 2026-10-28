// Fonts.h - the panel's fonts (Spleen, BSD-2-Clause), ASCII 32..126 in the
// SSD1306 page layout: per 8-pixel page from the top, one byte per column,
// bit 0 = top pixel. Generated into Fonts.cpp by tools/make_font.py.
#pragma once

#include <stdint.h>

namespace ui {

extern const uint8_t kFont8x16[95][16];   // 2 pages x 8 columns
extern const uint8_t kFont16x32[95][64];  // 4 pages x 16 columns

}  // namespace ui
