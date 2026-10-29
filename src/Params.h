// Params.h - reading and changing the settings (ui::Param, see ParamDefs.h)
// by number, for the serial commands and the panel alike. applyParam() in
// Params.cpp is the one place that knows what each setting does.
#pragma once

#include "ParamDefs.h"

// The current value (saved settings from g_settings; the others, such as
// the drums playing or the song's loop, from where they live).
int getParam(int id);
// Clamps v to the setting's range, applies it, saves it (a saved setting)
// and confirms it on Serial.
void setParam(int id, int v);
// The setting's range: the table's, narrowed to the delay's longest time and
// to how many styles, kits, songs and bars there are.
void paramLimits(int id, int* lo, int* hi);
// The value as text: a style, kit or song by name, otherwise as the table
// formats it ("30%", "380ms", "ON").
void paramText(int id, char* buf, int n);
// Applies every saved setting, except the drum song (which needs the SD
// card): call once at startup after audioBegin().
void applyAllParams();
