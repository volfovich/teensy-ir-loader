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
void setDelayDuck(int percent);       // 0 .. 100, ducking depth
void setDelayDrive(int percent);      // 0 .. 100, input grit
void setDelayEra(int percent);        // 0 .. 100, 0 = 16-bit, 100 = 10-bit
// Drum machine. Playing is not saved; the rest is.
void setDrumsPlaying(bool on);
void setDrumStyle(int idx);           // 0-based, see irdsp::drumStyle()
void setDrumTempo(int bpm);           // 40 .. 240
void setDrumVolume(int percent);      // 0 .. 100
void setDrumCountIn(int bars);        // 0 (off) .. 4
void setDrumTrainer(bool on, int step, int every, int target);  // BPM, bars, BPM
void setDrumKit(int idx);             // 0-based, see irdsp::drumKit()
void setDrumComp(int percent);        // drum-bus compression, 0 = natural .. 100
void setDrumCrash(int bars);          // crash every 2/4/8/16 bars, 0 = off
// Bass: plays the songs' and styles' bass lines with the drums.
void setBassOn(bool on);
void setBassVolume(int percent);      // 0 .. 100
void setBassKey(int key);             // the styles' key, 0 = C .. 11 = B
void setBassTone(int percent);        // 0 = dark .. 100 = bright
// "C", "C#", ... "B"
const char* bassKeyName(int key);
void setReverbOn(bool on);
void setReverbMix(int percent);       // 0 .. 100, 0 = dry only, 100 = wet only
void setReverbSize(int percent);      // 0 .. 100, reverb time
void setReverbTone(int percent);      // 0 .. 100, 100 = brightest tail
void setReverbMode(int mode);         // 0 = hall, 1 = gated
void setReverbPreDelay(int ms);       // 0 .. 150
void setReverbMod(int percent);       // hall: chorus in the tail
void setReverbEra(int percent);       // 0 = 16-bit/16 kHz .. 100 = 10-bit/6 kHz
void setReverbGate(int ms);           // gated: 100 .. 600
// "hall" or "gated", for the current settings.
const char* reverbModeName();
