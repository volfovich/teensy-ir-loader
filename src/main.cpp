// Teensy 4.0 + Audio Shield guitar IR loader
//
//   tube amp line out -> shield LINE IN (L) -> [NAM amp, optional] -> convolution
//                     -> studio 80s delay -> 80s reverb -> LINE OUT L/R + headphones
//                     (32-bit float throughout; drums and bass added after)
//
// Copy .wav impulse responses to the SD card (root or /IR folder), and NAM A2
// amp models (.nam) to /AMP. Any sample
// rate, mono or stereo, 8/16/24/32-bit PCM or 32/64-bit float; files are
// resampled to 48 kHz, trimmed and loudness-normalized on load.
//
// Control: USB serial (type "?" for help) and the OLED + encoder panel
// (DISPLAY.md, pins in config.h).
//
//   AudioGraph  audio objects and the codec
//   Params      reading and changing any setting (the table: ParamDefs.cpp)
//   Settings    the settings saved in EEPROM
//   IRLibrary   SD card file list and IR loading
//   AmpLibrary  the NAM amp models in /AMP and loading them (NAM.md)
//   DrumSongs   the drum songs, built-in and from the SD card
//   SerialUI    serial commands and level meter
//   Controls    the panel's encoder and keys
//   Oled        SH1106 / SSD1306 driver on its own I2C bus
//   DisplayUI   the panel: lib/oledui's screens and menu on the OLED
#include <Arduino.h>

#include "AmpLibrary.h"
#include "AudioGraph.h"
#include "Controls.h"
#include "DisplayUI.h"
#include "DrumSongs.h"
#include "IRLibrary.h"
#include "Params.h"
#include "SerialUI.h"
#include "Settings.h"
#include "config.h"


void setup() {
  controlsBegin();
  loadSettings();
  // The splash first (the display has its own I2C bus), then the audio: the
  // passthrough starts there, even before the IR loads.
  displayBegin();
  audioBegin();

  // No Serial.begin(): USB serial works without it on the Teensy, and it
  // waits up to 2 s for a serial monitor to open the port (0.75 s on a USB
  // charger). So this line only shows if one is already open; every state
  // can be printed later (?, s).
  Serial.printf("\nTeensy IR loader - block %d samples, max IR %d samples. Type ? for the commands.\n",
                AUDIO_BLOCK_SAMPLES, IR_MAX_LENGTH);

  scanCard();
  loadStartupIR();
  loadStartupAmp();
  // The drum song saved last time (an SD file may have gone: then none).
  if (getParam(ui::kDrumSong) && !selectDrumSong(getParam(ui::kDrumSong), true)) setParam(ui::kDrumSong, 0);
  displayBootDone();
}

void loop() {
  serviceSerial();
  serviceDisplay();
  serviceMeter();
  serviceSave();
}
