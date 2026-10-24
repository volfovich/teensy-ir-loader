// AudioGraph.h - audio objects (I2S in -> convolver + delay + reverb -> I2S
// out L/R), the codec, and the level/bypass/effect controls shared by the
// serial UI and the buttons.
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
void setDelayOn(bool on);
void setDelayMix(int percent);        // 0 .. 100, 0 = dry only, 100 = wet only
void setDelayTimeMs(int ms);          // 20 .. DELAY_MAX_MS
void setDelayFeedback(int percent);   // 0 .. 100, repeats
void setDelayTone(int percent);       // 0 .. 100, 100 = brightest repeats
void setDelayMod(int percent);        // 0 .. 100, modulation depth
void setDelayType(int type);          // 0 = ping-pong, 1 = studio 80s
void setDelayDuck(int percent);       // 0 .. 100, studio 80s: ducking depth
void setDelayDrive(int percent);      // 0 .. 100, studio 80s: input grit
void setDelayEra(int percent);        // 0 .. 100, studio 80s: 0 = 16-bit, 100 = 10-bit
// "ping-pong" / "studio 80s"
const char* delayTypeName(int type);
// Drum machine. Playing is not saved; the rest is.
void setDrumsPlaying(bool on);
void setDrumStyle(int idx);           // 0-based, see irdsp::drumStyle()
void setDrumTempo(int bpm);           // 40 .. 240
void setDrumVolume(int percent);      // 0 .. 100
void setDrumCountIn(int bars);        // 0 (off) .. 4
void setDrumTrainer(bool on, int step, int every, int target);  // BPM, bars, BPM
void setDrumKit(int idx);             // 0-based, see irdsp::drumKit()
void setDrumCrash(int bars);          // crash every 2/4/8/16 bars, 0 = off
void setReverbOn(bool on);
void setReverbMix(int percent);       // 0 .. 100, 0 = dry only, 100 = wet only
void setReverbSize(int percent);      // 0 .. 100, reverb time
void setReverbTone(int percent);      // 0 .. 100, 100 = brightest tail
void setReverbType(int type);         // 0 = plate, 1 = 80s
void setReverbMode(int mode);         // 80s: 0 = hall, 1 = gated
void setReverbPreDelay(int ms);       // 80s: 0 .. 150
void setReverbMod(int percent);       // 80s hall: chorus in the tail
void setReverbEra(int percent);       // 80s: 0 = 16-bit/16 kHz .. 100 = 10-bit/6 kHz
void setReverbGate(int ms);           // 80s gated: 100 .. 600
// "plate", "80s hall" or "80s gated", for the current settings.
const char* reverbTypeName();
