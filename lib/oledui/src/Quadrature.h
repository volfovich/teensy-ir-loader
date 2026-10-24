// Quadrature.h - rotary encoder state table. ab = (A << 1) | B, the pin
// levels after a change. Invalid transitions (both pins changed, i.e. a
// missed or bouncing edge) count 0, so bounce can't run the count away.
//
// Header-only and not UI_COLD: it runs in the encoder interrupt.
#pragma once

#include <stdint.h>

namespace ui {

// +1 / -1 per valid quarter step, 0 for no change or an invalid transition.
inline int quadStep(uint8_t prevAB, uint8_t ab) {
  // Index prev * 4 + now. 00 -> 01 -> 11 -> 10 -> 00 counts up.
  static const int8_t kTable[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
  return kTable[((prevAB & 3) << 2) | (ab & 3)];
}

}  // namespace ui
