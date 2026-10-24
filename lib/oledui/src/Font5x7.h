// Font5x7.h - classic 5x7 ASCII font (32..126), 5 column bytes per glyph,
// bit 0 = top row (the SSD1306 page format).
#pragma once

#include <stdint.h>

namespace ui {

// The 5 columns of character c; '?' for anything outside 32..126.
const uint8_t* glyph(char c);

}  // namespace ui
