// AmpLibrary.h - the NAM amp models (.nam files in /AMP on the SD card) and
// loading them into the amp block (see NAM.md and irdsp::AmpStage).
#pragma once

#include "NamFile.h"

// Rebuilds the sorted list of /AMP/*.nam (called by scanCard()).
void scanAmps();

// Loads model idx (0-based); remembers it in the settings. False on failure.
bool loadAmpIndex(int idx);
// Loads the model saved in the settings, or the first one that loads.
void loadStartupAmp();
// The first model whose file name contains text (case-insensitive), or -1.
int findAmp(const char* text);

int ampCount();
int ampCurrent();  // -1 = none loaded
const char* ampPath(int idx);
// What the loaded model's file says about itself.
const irdsp::NamInfo& ampInfo();

// Sets the amp's input and output gains from the settings (amp in / out /
// cal, the line-in sensitivity) and the model: call when any of them change.
void ampApplyGains();
// The calibration gain into the model (dB), or NAN if it has none or it's off.
float ampCalibrationDb();
// The model's levelling gain (dB): -18 - its loudness, 0 if it has none.
float ampLevelDb();
