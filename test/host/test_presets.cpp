// test_presets.cpp - the preset file (PresetFile.h, PRESETS.md): writing and
// reading back every preset setting, the forgiving rules (comments, CRLF, any
// order, unknown keys, missing keys, values out of range) and damaged input.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "PresetFile.h"

using namespace ui;

static int g_fail = 0;
#define CHECK(cond, ...)                              \
  do {                                                \
    if (!(cond)) {                                    \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
      printf(__VA_ARGS__);                            \
      printf("\n");                                   \
      g_fail++;                                       \
    }                                                 \
  } while (0)

static void collect(void* ctx, const char* line) { static_cast<std::vector<std::string>*>(ctx)->push_back(line); }

static std::vector<std::string> write(const PresetData& p) {
  std::vector<std::string> lines;
  writePreset(p, collect, &lines);
  return lines;
}

static PresetData read(const std::vector<std::string>& lines, int* unknown = nullptr, int* bad = nullptr) {
  PresetData p;
  presetDefaults(&p);
  for (const std::string& l : lines) {
    const PresetLine r = parsePresetLine(l.c_str(), &p);
    if (unknown && r == PresetLine::kUnknown) ++*unknown;
    if (bad && r == PresetLine::kBad) ++*bad;
  }
  return p;
}

static bool same(const PresetData& a, const PresetData& b) {
  return presetDiff(a, b, 0) < 0 && !strcmp(a.ampPath, b.ampPath) && !strcmp(a.irPath, b.irPath);
}

int main() {
  printf("test_presets\n");
  int count = 0;
  for (int id = 0; id < kParamCount; id++) count += inPreset(id);
  CHECK(count == 35, "35 settings in a preset (%d)", count);
  CHECK(inPreset(kAmpIn) && inPreset(kGateType) && inPreset(kPresetLevel) && inPreset(kBypass), "amp, gate, pvol, bypass in");
  CHECK(!inPreset(kOutGain) && !inPreset(kLineIn) && !inPreset(kDrumTempo) && !inPreset(kBassOn), "levels, drums, bass out");

  // Round trip at each end of every range, and the defaults.
  for (int pass = 0; pass < 3; pass++) {
    PresetData p;
    presetDefaults(&p);
    for (int id = 0; id < kParamCount; id++)
      if (inPreset(id)) p.v[id] = pass == 0 ? paramDef(id).min : (pass == 1 ? paramDef(id).max : paramDef(id).def);
    strcpy(p.ampPath, "/AMP/JCM 800 2203 (boosted) #5.nam");
    strcpy(p.irPath, pass == 2 ? "" : "/IR/V30 SM57 Mesa 4x12.wav");
    const std::vector<std::string> lines = write(p);
    CHECK(same(read(lines), p), "round trip, pass %d", pass);
    CHECK((int)lines.size() == count + 1 + 1 + (pass == 2 ? 0 : 1), "lines: a comment, the paths, every setting (%zu)",
          lines.size());
  }
  // A few lines as they are written.
  {
    PresetData p;
    presetDefaults(&p);
    p.v[kAmpIn] = 7;
    p.v[kGateType] = 0;
    p.v[kAmpOn] = 1;
    p.v[kPresetLevel] = -3;
    const std::vector<std::string> l = write(p);
    auto has = [&](const char* s) {
      for (const std::string& x : l)
        if (x == s) return true;
      return false;
    };
    CHECK(has("amp in +3.5") && has("gate type nam") && has("amp on") && has("pvol -1.5") && has("dtime 380") &&
              has("rmode hall"),
          "written as typed over serial");
  }

  // Forgiving: comments, CRLF, tabs, any order, a key that's a prefix of
  // another, unknown keys, bad values, out of range.
  {
    int unknown = 0, bad = 0;
    const PresetData p = read({"# a comment", "", "   ", "dtime 500\r", "\tamp on  ", "amp in -2.0\r\n", "drum vol 80",
                               "bpm 120", "future thing 3", "dmix lots", "dfb 250", "gate thr -200", "ir   /IR/a b.wav  \r",
                               "amp model /AMP/x.nam"},
                              &unknown, &bad);
    PresetData def;
    presetDefaults(&def);
    CHECK(p.v[kDelayTime] == 500 && p.v[kAmpOn] == 1 && p.v[kAmpIn] == -4, "values read (dtime %d, amp %d, amp in %d)",
          p.v[kDelayTime], p.v[kAmpOn], p.v[kAmpIn]);
    CHECK(p.v[kDelayRepeats] == 100 && p.v[kGateThr] == -90, "clamped to the range (dfb %d, gate thr %d)",
          p.v[kDelayRepeats], p.v[kGateThr]);
    CHECK(p.v[kDelayMix] == def.v[kDelayMix] && p.v[kDrumVolume] == def.v[kDrumVolume] && p.v[kDrumTempo] == def.v[kDrumTempo],
          "bad values and global settings leave the defaults");
    CHECK(unknown == 3 && bad == 1, "drum vol, bpm, future thing unknown (%d); dmix lots bad (%d)", unknown, bad);
    CHECK(!strcmp(p.irPath, "/IR/a b.wav") && !strcmp(p.ampPath, "/AMP/x.nam"), "paths trimmed (%s|%s)", p.irPath,
          p.ampPath);
    CHECK(p.v[kReverbMix] == def.v[kReverbMix], "a missing key keeps the default");
  }

  // The diff walks the preset settings only.
  {
    PresetData a, b;
    presetDefaults(&a);
    presetDefaults(&b);
    b.v[kDrumTempo] = 200;  // not a preset setting
    CHECK(presetDiff(a, b, 0) < 0, "a global setting isn't a difference");
    b.v[kDelayMix] = 99;
    b.v[kGateRel] = 300;
    const int d0 = presetDiff(a, b, 0), d1 = presetDiff(a, b, d0 + 1);
    CHECK(d0 == kDelayMix && d1 == kGateRel && presetDiff(a, b, d1 + 1) < 0, "diff finds dmix then gate rel");
  }

  // Damaged input: random bytes and very long lines never crash, and every
  // value stays within its range.
  {
    uint32_t rng = 1;
    auto rnd = [&]() { return rng = rng * 1664525u + 1013904223u, rng >> 8; };
    PresetData p;
    presetDefaults(&p);
    std::vector<std::string> good = write(p);
    for (int t = 0; t < 2000; t++) {
      std::string s = good[1 + rnd() % (good.size() - 1)];
      const int edits = 1 + rnd() % 4;
      for (int e = 0; e < edits; e++) {
        if (rnd() % 5 == 0) s += std::string(rnd() % 300, (char)(32 + rnd() % 90));
        else if (!s.empty()) s[rnd() % s.size()] = (char)(1 + rnd() % 254);
      }
      parsePresetLine(s.c_str(), &p);
    }
    bool inRange = true;
    for (int id = 0; id < kParamCount; id++)
      if (inPreset(id)) inRange = inRange && p.v[id] >= paramDef(id).min && p.v[id] <= paramDef(id).max;
    CHECK(inRange && strlen(p.ampPath) < (size_t)kPresetPathMax && strlen(p.irPath) < (size_t)kPresetPathMax,
          "2000 damaged lines: values in range, paths bounded");
  }

  printf(g_fail ? "test_presets: %d FAILED\n" : "test_presets: all passed\n", g_fail);
  return g_fail ? 1 : 0;
}
