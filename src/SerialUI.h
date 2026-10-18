// SerialUI.h - USB serial command line and live level meter.
#pragma once

void printHelp();
// Call from loop(): reads serial input and runs complete command lines.
void serviceSerial();
// Call from loop(): prints the level meter every 200 ms while it is enabled.
void serviceMeter();
