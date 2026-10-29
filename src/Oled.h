// Oled.h - minimal SH1106 / SSD1306 128x64 driver (OLED_SH1106 in config.h)
// on its own I2C bus (OLED_WIRE, Wire1 on pins 16/17), apart from the
// codec's. It holds no frame buffer: the UI draws into a ui::Canvas and sends
// it a page (8 pixel rows x 128 columns) at a time.
#pragma once

#include <stdint.h>

// Starts the bus, finds the display at OLED_I2C_ADDR or 0x3D and initializes
// it, blank. Needs nothing else running, so it can come first in setup().
// False: no display.
bool oledBegin();
bool oledPresent();
// Sends page p (0..7): 128 column bytes, bit 0 = top pixel. About 1.3 ms at
// 1 MHz.
void oledSendPage(int p, const uint8_t* data);
