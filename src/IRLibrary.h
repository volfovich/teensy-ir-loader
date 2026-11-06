// IRLibrary.h - the list of .wav IRs on the SD card and loading them into the
// convolver (resampled, trimmed and normalized by irdsp::prepareIR).
#pragma once

// (Re)mounts the SD card and rebuilds the sorted file list.
void scanCard();

// Loads file idx (0-based) and, with save, remembers it in the settings (the
// IR loaded at startup). False on failure.
bool loadIRIndex(int idx, bool save = true);
// Remembers the IR playing now in the settings.
void saveCurrentIR();
// The index of the IR in the settings, or -1 (none, or not on the card).
int irSaved();
// Loads the next (+1) or previous (-1) IR, skipping files that fail to load.
void loadRelative(int step);
// Loads the IR saved in the settings, or the first one that loads.
void loadStartupIR();

bool irSdOk();
int irCount();
int irCurrent();  // -1 = none loaded (passthrough)
const char* irPath(int idx);
