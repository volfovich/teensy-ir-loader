// PresetLibrary.h - presets: whole sounds as text files in /PRESETS on the SD
// card (PRESETS.md; the file format in lib/oledui/src/PresetFile.h). Named
// "NN Name.txt", NN being the preset's number (01-99).
//
// Background only (loop()): a recall loads the amp model and the IR the
// usual way and sets the values through setParamQuiet().
#pragma once

// Rebuilds the list of /PRESETS/*.txt and reads the preset in the settings
// (the one recalled last), for the edited mark. Called by scanCard().
void scanPresets();

int presetCount();
int presetCurrent();             // -1 = none
const char* presetName(int idx);  // the file name without ".txt" ("03 Lead")
int presetNumber(int idx);       // NN, or 0 if the name has none
int findPresetNumber(int n);     // the index of preset NN, or -1
int findPresetName(const char* text);  // the first whose name contains text, or -1

// The current sound differs from the preset recalled (its settings, model or
// IR); false with no preset.
bool presetEdited();

// Recalls preset idx: model, IR, then the settings. False if it can't be read
// (nothing changes then). Keeps a preset being tried (previewPreset).
bool recallPreset(int idx);

// Trying presets from the panel's list: previewPreset() plays preset idx,
// keeping the sound from before the first try (edits and all);
// cancelPreview() goes back to it, keepPreview() keeps what plays. Every
// other change to presets keeps.
bool previewPreset(int idx);
void cancelPreview();
void keepPreview();
// The preset recalled before the tries (the list's mark), else presetCurrent().
int presetSaved();
// Writes the current sound over the recalled preset.
bool savePreset();
// Saves the current sound as a new preset at the lowest free number; it
// becomes the recalled one. name: up to 24 characters ("" = "Preset NN").
bool newPreset(const char* name);
// Renames the recalled preset (its number stays).
bool renamePreset(const char* name);
// Deletes preset idx (the others keep their numbers).
bool deletePreset(int idx);

// Serial: the list ("> 03 Lead*") and the settings that differ from the
// recalled preset.
void printPresets();
void printPresetDiff();
