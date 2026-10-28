// Teensy 4.0 + Audio Shield guitar IR loader
//
//   tube amp line out -> shield LINE IN (L) -> convolution -> ping-pong delay
//                     -> plate reverb -> LINE OUT L/R + headphones
//                     (32-bit float throughout)
//
// Copy .wav impulse responses to the SD card (root or /IR folder). Any sample
// rate, mono or stereo, 8/16/24/32-bit PCM or 32/64-bit float; files are
// resampled to 44.1 kHz, trimmed and loudness-normalized on load.
//
// Control: USB serial (type "?" for help) and the OLED + encoder panel
// (DISPLAY.md, pins in config.h).
//
//   AudioGraph  audio objects, codec, level/bypass/effect setters
//   IRLibrary   SD card file list and IR loading
//   Settings    EEPROM-backed settings
//   SerialUI    serial commands and level meter
//   Controls    the panel's encoder and keys
//   Oled        SSD1306 driver on the codec's I2C bus
//   DisplayUI   the panel: lib/oledui's screens and menu on the OLED
#include <Arduino.h>

#include "AudioGraph.h"
#include "Controls.h"
#include "DisplayUI.h"
#include "DrumSongs.h"
#include "IRLibrary.h"
#include "SerialUI.h"
#include "Settings.h"
#include "config.h"

FLASHMEM void setup() {
  controlsBegin();
  loadSettings();

  // Audio first: the passthrough starts immediately, even before the IR loads.
  audioBegin();

  Serial.begin(115200);
  uint32_t t = millis();
  while (!Serial && millis() - t < 1500) {}
  Serial.printf("\nTeensy IR loader - block %d samples, max IR %d samples\n",
                AUDIO_BLOCK_SAMPLES, IR_MAX_LENGTH);

  scanCard();
  loadStartupIR();
  // The drum song saved last time (an SD file may have gone: then none).
  if (g_settings.drumSong && !selectDrumSong(g_settings.drumSong, true)) selectDrumSong(0);
  printHelp();
  displayBegin();
}

void loop() {
  serviceSerial();
  serviceDisplay();
  serviceMeter();
  serviceSave();
}
