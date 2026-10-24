// ui_fake.h - a stand-in for the firmware's settings, for the UI tests and
// the screen renderer.
#pragma once

#include <cstdio>

#include "Menu.h"
#include "Screens.h"

struct FakeHost : ui::MenuHost {
  int v[64] = {};
  int timeMax = 750;
  int sets = 0;
  FakeHost() {
    v[ui::kDelayOn] = 1; v[ui::kDelayType] = 1; v[ui::kDelayMix] = 30; v[ui::kDelayTime] = 380;
    v[ui::kDelayRepeats] = 35; v[ui::kDelayTone] = 60; v[ui::kDelayMod] = 15;
    v[ui::kDelayDuck] = 40; v[ui::kDelayDrive] = 20; v[ui::kDelayEra] = 40;
    v[ui::kReverbOn] = 1; v[ui::kReverbMix] = 25; v[ui::kReverbSize] = 50; v[ui::kReverbTone] = 50;
    v[ui::kOutGain] = -12; v[ui::kLineIn] = 0; v[ui::kLineOut] = 29; v[ui::kHeadphones] = 50;
  }
  int get(uint8_t id) override {
    if (id == ui::kIsStudioDelay) return v[ui::kDelayType] == 1;
    return v[id];
  }
  void set(uint8_t id, int value) override { v[id] = value; sets++; }
  void limits(uint8_t id, int* lo, int* hi) override {
    (void)lo;
    if (id == ui::kDelayTime) *hi = timeMax;
  }
  bool valueText(uint8_t id, char* buf, int n) override {
    if (id != ui::kScreenIR) return false;
    std::snprintf(buf, n, "3/12");
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

// The home-screen model of the DISPLAY.md mockups.
inline ui::UiModel fakeModel() {
  ui::UiModel m;
  m.nowMs = 10000;
  m.sdOk = true;
  m.irCount = 12;
  m.irCurrent = m.irShown = 2;
  m.irPath = fakeIrPath;
  m.delayOn = true;
  m.reverbOn = true;
  m.studioDelay = true;
  m.delayTimeMs = 380;
  m.reverbMix = 25;
  m.cpu = 33.2f; m.cpuMax = 35.1f; m.cpuDelay = 4.4f; m.cpuReverb = 6.2f;
  m.blocksUsed = 4; m.blocksMax = 20; m.latencyMs = 1.45f; m.partitions = 250; m.blockSize = 32;
  m.gainDb = -6.0f; m.hpPercent = 50; m.lineIn = 0; m.lineOut = 29; m.irMaxLength = 8192;
  m.in.update(0.42f, false, 9000);    // -7.5 dBFS
  m.in.update(0.30f, false, 10000);   // falling
  m.out.update(0.32f, false, 9500);   // -9.9 dBFS
  m.out.update(0.25f, false, 10000);
  return m;
}
