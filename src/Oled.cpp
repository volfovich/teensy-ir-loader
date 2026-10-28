#include "Oled.h"

#include <Arduino.h>
#include <Wire.h>

#include "config.h"

static int g_addr = -1;

// Commands go after a 0x00 control byte, display data after 0x40.
FLASHMEM static bool command(const uint8_t* c, int n) {
  Wire.beginTransmission(g_addr);
  Wire.write(0x00);
  Wire.write(c, n);
  return Wire.endTransmission() == 0;
}

FLASHMEM static bool probe(int addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

FLASHMEM bool oledBegin() {
  if (OLED_I2C_ADDR < 0) return false;
  // The SGTL5000 is rated for 400 kHz too; Wire starts at 100 kHz.
  Wire.setClock(400000);
  if (probe(OLED_I2C_ADDR)) g_addr = OLED_I2C_ADDR;
  else if (probe(0x3D)) g_addr = 0x3D;
  else return false;

#if OLED_SH1106
  static const uint8_t PROGMEM kInit[] = {
      0xAE,        // display off
      0xD5, 0x80,  // clock divide / oscillator
      0xA8, 0x3F,  // multiplex: 64 rows
      0xD3, 0x00,  // no display offset
      0x40,        // start line 0
      0xAD, 0x8B,  // DC-DC converter on (internal VCC)
      0xA1,        // columns mirrored: module mounted upright
      0xC8,        // rows scan from COM63 down
      0xDA, 0x12,  // COM pins: alternative (128x64)
      0x81, 0xCF,  // contrast
      0xD9, 0x22,  // pre-charge period
      0xDB, 0x35,  // VCOM deselect level
      0xA4,        // show the RAM contents
      0xA6,        // not inverted
  };
#else
  static const uint8_t PROGMEM kInit[] = {
      0xAE,        // display off
      0xD5, 0x80,  // clock divide / oscillator
      0xA8, 0x3F,  // multiplex: 64 rows
      0xD3, 0x00,  // no display offset
      0x40,        // start line 0
      0x8D, 0x14,  // charge pump on (internal VCC)
      0x20, 0x02,  // page addressing mode
      0xA1,        // columns mirrored (column 127 = SEG0): module mounted upright
      0xC8,        // rows scan from COM63 down
      0xDA, 0x12,  // COM pins: alternative, no remap (128x64)
      0x81, 0xCF,  // contrast
      0xD9, 0xF1,  // pre-charge period
      0xDB, 0x40,  // VCOMH deselect level
      0xA4,        // show the RAM contents
      0xA6,        // not inverted
  };
#endif
  if (!command(kInit, sizeof(kInit))) {
    g_addr = -1;
    return false;
  }
  // Blank the whole RAM (the SH1106 has 132 columns), so no power-on
  // garbage shows in columns the UI never writes.
  static const uint8_t PROGMEM zeros[132] = {};
  for (int p = 0; p < 8; p++) {
    const uint8_t addr[] = {(uint8_t)(0xB0 | p), 0x00, 0x10};
    command(addr, sizeof(addr));
    Wire.beginTransmission(g_addr);
    Wire.write(0x40);
    Wire.write(zeros, OLED_SH1106 ? 132 : 128);
    Wire.endTransmission();
  }
  static const uint8_t PROGMEM kOn[] = {0xAF};
  command(kOn, sizeof(kOn));
  return true;
}

FLASHMEM bool oledPresent() { return g_addr >= 0; }

FLASHMEM void oledSendPage(int p, const uint8_t* data) {
  if (g_addr < 0) return;
  // The SH1106 has 132 columns of RAM; the visible 128 start at OLED_COL_OFFSET.
  const uint8_t col = OLED_SH1106 ? OLED_COL_OFFSET : 0;
  const uint8_t addr[] = {(uint8_t)(0xB0 | p), (uint8_t)(col & 0x0F), (uint8_t)(0x10 | (col >> 4))};
  command(addr, sizeof(addr));
  // 1 control byte + 128 data bytes fit the Teensy 4 Wire buffer (136).
  Wire.beginTransmission(g_addr);
  Wire.write(0x40);
  Wire.write(data, 128);
  Wire.endTransmission();
}
