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

// Sets the amp's input and output gains and its tone controls from the
// settings (amp in / out / cal / bass / mid / treble / presence, the line-in
// sensitivity) and the model: call when any of them change.
void ampApplyGains();
// The input's full scale at the guitar, in dBu: a sine at 0 dBFS is 3.12 V
// p-p (+3.1 dBu) at the shield at line-in sensitivity 0, each step is 1.5 dB
// more sensitive, and the buffer in front adds INPUT_PAD_DB.
float ampInputFullScaleDbu();
// The calibration gain into the model (dB): the full scale minus the model's
// input_level_dbu, or minus AMP_UNCAL_REF_DBU (the NAM plugin's default
// interface) for an uncalibrated model. NAN with no model or amp cal off.
float ampCalibrationDb();
// The model's levelling gain (dB): AMP_LEVEL_DB - its loudness, 0 if it has
// none.
float ampLevelDb();
