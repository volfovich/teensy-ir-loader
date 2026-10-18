// AudioGraph.h - audio objects (I2S in -> convolver -> I2S out), the codec,
// and the level/bypass controls shared by the serial UI and the buttons.
#pragma once

#include "AudioConvolverIR.h"

extern AudioConvolverIR convolver;

// Starts the audio library and codec with the levels from g_settings, so call
// loadSettings() first. The passthrough runs from here on, even with no IR.
void audioBegin();

// Each setter clamps its value, applies it, schedules a settings save and
// prints a confirmation on Serial.
void setBypass(bool on);
void setOutputGainDb(float db);       // -60 .. 24 dB
void setLineInLevel(int level);       // 0 .. 15, 0 = 3.12 Vpp (least sensitive)
void setLineOutLevel(int level);      // 13 .. 31, 13 = loudest
void setHeadphoneVolume(int percent); // 0 .. 100
