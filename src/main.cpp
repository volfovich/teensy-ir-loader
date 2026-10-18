// Teensy 4.0 + Audio Shield guitar IR loader
//
//   tube amp line out -> shield LINE IN (L) -> convolution -> LINE OUT + headphones
//
// Copy .wav impulse responses to the SD card (root or /IR folder). Any sample
// rate, mono or stereo, 8/16/24/32-bit PCM or 32/64-bit float; files are
// resampled to 44.1 kHz, trimmed and loudness-normalized on load.
//
// Control: USB serial (type "?" for help) and optional buttons (config.h).
//
//   AudioGraph  audio objects, codec, level/bypass setters
//   IRLibrary   SD card file list and IR loading
//   Settings    EEPROM-backed settings
//   SerialUI    serial commands and level meter
//   Buttons     next / previous / bypass buttons
#include <Arduino.h>

#include "AudioGraph.h"
#include "Buttons.h"
#include "IRLibrary.h"
#include "SerialUI.h"
#include "Settings.h"
#include "config.h"

void setup() {
  buttonsBegin();
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
  printHelp();
}

void loop() {
  serviceSerial();
  serviceButtons();
  serviceMeter();
  serviceSave();
}
