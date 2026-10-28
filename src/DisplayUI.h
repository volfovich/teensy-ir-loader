// DisplayUI.h - the OLED front panel: feeds the encoder and key events to
// ui::Panel (lib/oledui), keeps its model up to date from the settings and
// the audio objects, and sends the screen to the OLED a page at a time.
// Settings change through the AudioGraph setters, like over serial.
#pragma once

// Call at the end of setup() (after audioBegin() and the SD scan): shows
// the splash. Without a display the panel stays off.
void displayBegin();
// Call every loop() pass: controls, redraws, and at most one page sent
// (about 3.3 ms of I2C).
void serviceDisplay();
