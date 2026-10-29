// ui_fake.h - a stand-in for the firmware's settings, for the UI tests and
// the screen renderer.
#pragma once

#include <cstdio>

#include "Menu.h"
#include "Screens.h"

struct FakeHost : ui::MenuHost {
  int v[64] = {};
  static_assert(ui::kAmpLoad < 64, "v[] holds every ItemId");
  int timeMax = 750;
  int sets = 0;
  int loaded = -1, ampLoaded = -1, rescans = 0, resets = 0;  // the panel's actions
  FakeHost() {
    v[ui::kDelayOn] = 1; v[ui::kDelayMix] = 30; v[ui::kDelayTime] = 380;
    v[ui::kDelayRepeats] = 35; v[ui::kDelayTone] = 60; v[ui::kDelayMod] = 15;
    v[ui::kDelayDuck] = 40; v[ui::kDelayDrive] = 20; v[ui::kDelayEra] = 40;
    v[ui::kReverbOn] = 1; v[ui::kReverbMix] = 25; v[ui::kReverbSize] = 50; v[ui::kReverbTone] = 50;
    v[ui::kReverbMode] = 0; v[ui::kReverbPreDelay] = 40; v[ui::kReverbMod] = 40;
    v[ui::kReverbEra] = 40; v[ui::kReverbGate] = 300;
    v[ui::kDrumSong] = 0; v[ui::kDrumLoopFrom] = 0; v[ui::kDrumLoopTo] = 32; v[ui::kDrumComp] = 40;
    v[ui::kOutGain] = -12; v[ui::kLineIn] = 0; v[ui::kLineOut] = 29; v[ui::kHeadphones] = 50;
    v[ui::kDrumsPlay] = 1; v[ui::kDrumStyle] = 1; v[ui::kDrumTempo] = 100; v[ui::kDrumVolume] = 60;
    v[ui::kDrumCountIn] = 2; v[ui::kTrainerOn] = 0; v[ui::kTrainerStep] = 5; v[ui::kTrainerEvery] = 4;
    v[ui::kTrainerTarget] = 140; v[ui::kDrumKit] = 1; v[ui::kDrumCrash] = 2;
    v[ui::kBassOn] = 1; v[ui::kBassVolume] = 70; v[ui::kBassKey] = 4; v[ui::kBassTone] = 50;
    v[ui::kAmpOn] = 1; v[ui::kAmpIn] = 6; v[ui::kAmpOut] = -3; v[ui::kAmpCal] = 1;
  }
  static const char* styleName(int i) {
    static const char* names[] = {"metronome", "rock",          "offbeat 8ths", "offbeat 16ths",
                                  "triplets",  "pop",           "blues shuffle", "6/8 ballad"};
    return names[i < 0 || i > 7 ? 0 : i];
  }
  static const char* songName(int i) {
    static const char* names[] = {"off", "Back in Black-ish", "Sweet Child-ish", "Livin on a Prayer-ish",
                                  "Enter Sandman-ish", "Smoke on Water-ish"};
    return names[i < 0 || i > 5 ? 0 : i];
  }
  static const char* keyName(int i) {
    static const char* names[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    return names[i < 0 || i > 11 ? 0 : i];
  }
  static const char* kitName(int i) {
    static const char* names[] = {"synth", "rock"};
    return names[i < 0 || i > 1 ? 0 : i];
  }
  int get(uint8_t id) override {
    if (id == ui::kIsReverbHall) return v[ui::kReverbMode] == 0;
    if (id == ui::kIsReverbGated) return v[ui::kReverbMode] == 1;
    if (id == ui::kIsDrumSong) return v[ui::kDrumSong] != 0;
    return v[id];
  }
  void set(uint8_t id, int value) override {
    sets++;
    if (id == ui::kIrLoad) loaded = value;
    else if (id == ui::kAmpLoad) ampLoaded = value;
    else if (id == ui::kIrRescan) rescans++;
    else if (id == ui::kMeterReset) resets++;
    else v[id] = value;
  }
  void limits(uint8_t id, int* lo, int* hi) override {
    (void)lo;
    if (id == ui::kDelayTime) *hi = timeMax;
    if (id == ui::kDrumStyle) *hi = 7;
    if (id == ui::kDrumKit) *hi = 1;
    if (id == ui::kDrumSong) *hi = 5;
    if (id == ui::kDrumLoopFrom || id == ui::kDrumLoopTo) *hi = 32;
  }
  bool valueText(uint8_t id, char* buf, int n) override {
    if (id == ui::kScreenIR) std::snprintf(buf, n, "3/12");
    else if (id == ui::kScreenAmp) std::snprintf(buf, n, "7/8");
    else if (id == ui::kDrumStyle) std::snprintf(buf, n, "%s", styleName(v[id]));
    else if (id == ui::kDrumKit) std::snprintf(buf, n, "%s", kitName(v[id]));
    else if (id == ui::kDrumSong) std::snprintf(buf, n, "%s", songName(v[id]));
    else if (id == ui::kDrumLoopFrom && v[id] == 0) std::snprintf(buf, n, "off");
    else if (id == ui::kBassKey) std::snprintf(buf, n, "%s", keyName(v[id]));
    else return false;
    return true;
  }
};

inline const char* fakeIrPath(int i) {
  static const char* names[] = {
      "/IR/Greenback.wav",       "/IR/V30 Room.wav",     "/IR/V30 SM57 Mesa 4x12 close.wav",
      "/IR/V30 R121.wav",        "/IR/Alnico Blue 2x12 R121.wav", "/IR/G12H 2x12.wav",
      "/IR/Creamback 1x12.wav",  "/IR/OH 412 RECT.wav",  "/IR/OH Hair-Metal.wav",
      "/IR/OH Mdrn-Prog.wav",    "/IR/Jensen P12Q.wav",  "/IR/Celestion G12M.wav",
  };
  return names[i % 12];
}

inline const char* fakeAmpPath(int i) {
  static const char* names[] = {
      "/AMP/JCM_800_2203_Amp_Gain_3.nam",
      "/AMP/JCM_800_2203_Amp_Gain_5.nam",
      "/AMP/JCM_800_2203_Amp_Gain_7.nam",
      "/AMP/JCM_800_2203_Amp_Gain_9.nam",
      "/AMP/JCM_800_2203_OD8008_Boosted_Amp_Gain_3.nam",
      "/AMP/JCM_800_2203_OD8008_Boosted_Amp_Gain_39.nam",
      "/AMP/JCM_800_2203_OD8008_Boosted_Amp_Gain_5.nam",
      "/AMP/JCM_800_2203_OD8008_Boosted_Amp_Gain_7.nam",
  };
  return names[i % 8];
}

// The model of the DISPLAY.md mockups.
inline ui::UiModel fakeModel() {
  ui::UiModel m;
  m.nowMs = 10000;
  m.sdOk = true;
  m.irCount = 12;
  m.irCurrent = 2;
  m.irPath = fakeIrPath;
  m.ampCount = 8;
  m.ampCurrent = 6;
  m.ampPath = fakeAmpPath;
  m.ampOn = true;
  m.delayOn = true;
  m.reverbOn = true;
  m.drumsPlaying = true;
  m.drumStyle = "rock";
  m.drumTempo = 100;
  m.drumBeat = 1;
  m.drumBeats = 4;
  m.cpu = 33.2f; m.cpuMax = 35.1f;
  m.blocksUsed = 4; m.blocksMax = 20; m.lineIn = 0;
  m.in.update(0.42f, false, 9000);    // -7.5 dBFS
  m.in.update(0.30f, false, 10000);   // falling
  m.out.update(0.32f, false, 9500);   // -9.9 dBFS
  m.out.update(0.25f, false, 10000);
  return m;
}
