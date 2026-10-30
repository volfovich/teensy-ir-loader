// DisplayUI.h - the OLED front panel: feeds the encoder and key events to
// ui::Panel (lib/oledui), keeps its model up to date from the settings and
// the audio objects, and sends the screen to the OLED a page at a time.
// Settings change through the AudioGraph setters, like over serial.
#pragma once

// Call first in setup() (after loadSettings()): shows the splash at once.
// Without a display the panel stays off.
void displayBegin();
// Call at the end of setup(), after the SD scan: the splash shows the IR
// count. The home screen follows once the splash has been up 1.5 s.
void displayBootDone();
// Call every loop() pass: controls, redraws, and at most one page sent
// (about 3.3 ms of I2C).
void serviceDisplay();
// Shows a box ("Loading", the name) at once, before a slow job that keeps
// loop() busy; the panel redraws itself after it.
void displayBusy(const char* what, const char* name);
