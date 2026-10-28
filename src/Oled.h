// Oled.h - minimal SH1106 / SSD1306 128x64 driver (OLED_SH1106 in config.h)
// on the codec's I2C bus (Wire, pins 18/19, 400 kHz). It holds no frame
// buffer: the UI draws into a ui::Canvas and sends it a page (8 pixel rows x
// 128 columns) at a time.
#pragma once

#include <stdint.h>

// Call after audioBegin() (the codec starts Wire). Finds the display at
// OLED_I2C_ADDR or 0x3D and initializes it, blank. False: no display.
bool oledBegin();
bool oledPresent();
// Sends page p (0..7): 128 column bytes, bit 0 = top pixel. About 3.3 ms.
void oledSendPage(int p, const uint8_t* data);
