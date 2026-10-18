// IRLibrary.h - the list of .wav IRs on the SD card and loading them into the
// convolver (resampled, trimmed and normalized by irdsp::prepareIR).
#pragma once

// (Re)mounts the SD card and rebuilds the sorted file list.
void scanCard();

// Loads file idx (0-based); remembers it in the settings. False on failure.
bool loadIRIndex(int idx);
// Loads the next (+1) or previous (-1) IR, skipping files that fail to load.
void loadRelative(int step);
// Loads the IR saved in the settings, or the first one that loads.
void loadStartupIR();

bool irSdOk();
int irCount();
int irCurrent();  // -1 = none loaded (passthrough)
const char* irPath(int idx);
