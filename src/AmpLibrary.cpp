#include "AmpLibrary.h"

#include <Arduino.h>
#include <SD.h>

#include "AudioGraph.h"
#include "IRLibrary.h"
#include "NamA2.h"
#include "Params.h"
#include "SdSource.h"
#include "Settings.h"
#include "config.h"

using namespace ui;

static const int kMaxAmps = 64;
// The file list and the weights being handed to the audio side, in OCRAM:
// only the background touches them (the weights once, when swapped in).
DMAMEM static char g_files[kMaxAmps][IR_PATH_MAX];
DMAMEM static float g_staging[irdsp::nama2::kWeightCount];
static int g_numFiles = 0;
static int g_current = -1;
static irdsp::NamInfo g_info;

int ampCount() { return g_numFiles; }
int ampCurrent() { return g_current; }
const char* ampPath(int idx) { return g_files[idx]; }
const irdsp::NamInfo& ampInfo() { return g_info; }

static bool isNamName(const char* n) {
  if (n[0] == '.') return false;  // hidden files, macOS "._" resource forks
  const size_t len = strlen(n);
  return len > 4 && strcasecmp(n + len - 4, ".nam") == 0;
}

static int cmpPath(const void* a, const void* b) { return strcasecmp((const char*)a, (const char*)b); }

void scanAmps() {
  g_numFiles = 0;
  g_current = -1;
  if (!irSdOk()) return;
  File d = SD.open("/AMP");
  if (!d || !d.isDirectory()) return;
  while (g_numFiles < kMaxAmps) {
    File e = d.openNextFile();
    if (!e) break;
    if (!e.isDirectory() && isNamName(e.name())) {
      const int n = snprintf(g_files[g_numFiles], IR_PATH_MAX, "/AMP/%s", e.name());
      if (n > 0 && n < IR_PATH_MAX) g_numFiles++;
      else Serial.printf("  skipped (path too long): %s\n", e.name());
    }
    e.close();
  }
  d.close();
  qsort(g_files, g_numFiles, IR_PATH_MAX, cmpPath);
  if (g_numFiles) Serial.printf("Found %d amp model(s) in /AMP.\n", g_numFiles);
}

// s contains text, ignoring case.
static bool containsNoCase(const char* s, const char* text) {
  const size_t n = strlen(text);
  for (; *s; s++)
    if (!strncasecmp(s, text, n)) return true;
  return !n;
}

int findAmp(const char* text) {
  for (int i = 0; i < g_numFiles; i++)
    if (containsNoCase(strrchr(g_files[i], '/') + 1, text)) return i;
  return -1;
}

// The shield's line-in full scale in dBu: a sine at 0 dBFS is 3.12 V p-p
// (+3.1 dBu) at sensitivity 0, and each step is 1.5 dB more sensitive.
static float lineInFullScaleDbu() { return 3.07f - 1.5f * (float)getParam(kLineIn); }

float ampCalibrationDb() {
  if (g_current < 0 || !getParam(kAmpCal) || isnan(g_info.inputLevelDbu)) return NAN;
  return lineInFullScaleDbu() - g_info.inputLevelDbu;
}

float ampLevelDb() { return g_current < 0 || isnan(g_info.loudness) ? 0.0f : -18.0f - g_info.loudness; }

void ampApplyGains() {
  const float cal = ampCalibrationDb();
  const float in = 0.5f * (float)getParam(kAmpIn) + (isnan(cal) ? 0.0f : cal);
  const float out = ampLevelDb() + 0.5f * (float)getParam(kAmpOut);
  convolver.amp().setGains(powf(10.0f, in / 20.0f), powf(10.0f, out / 20.0f));
}

bool loadAmpIndex(int idx) {
  if (idx < 0 || idx >= g_numFiles) return false;
  irdsp::AmpStage<AUDIO_BLOCK_SAMPLES>& amp = convolver.amp();
  // The previous model is swapped in within a fade (20 ms).
  const uint32_t t = millis();
  while (!amp.weightsTaken()) {
    if (millis() - t > 500) {
      Serial.println("Busy, try again.");
      return false;
    }
    yield();
  }

  const char* path = g_files[idx];
  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("Cannot open %s\n", path);
    return false;
  }
  SdSource src(f);
  irdsp::NamInfo info;
  const char* err = "";
  const uint32_t t0 = millis();
  const bool ok = irdsp::readNamA2Lite(src, g_staging, &info, &err);
  f.close();
  if (!ok) {
    Serial.printf("Failed to load %s: %s\n", path, err);
    return false;
  }
  const uint32_t ms = millis() - t0;
  g_info = info;
  g_current = idx;
  amp.loadWeights(g_staging);
  ampApplyGains();
  strlcpy(g_settings.ampPath, path, IR_PATH_MAX);
  scheduleSave();

  Serial.printf("[amp %d/%d] %s\n      A2-Lite", idx + 1, g_numFiles, path);
  if (info.gearMake[0] || info.gearModel[0]) Serial.printf(", %s %s", info.gearMake, info.gearModel);
  if (info.gearType[0] || info.toneType[0]) Serial.printf(" (%s%s%s)", info.gearType, info.gearType[0] && info.toneType[0] ? ", " : "", info.toneType);
  Serial.printf(", level %+.1f dB", ampLevelDb());
  const float cal = ampCalibrationDb();
  if (!isnan(cal)) Serial.printf(", input %.1f dBu -> %+.1f dB", info.inputLevelDbu, cal);
  Serial.printf(", %lu ms\n", (unsigned long)ms);
  if (fabsf(info.sampleRate - AUDIO_SAMPLE_RATE_EXACT) > 1.0f)
    Serial.printf("      model is %.0f Hz, the firmware %.0f Hz: its tone is shifted\n", info.sampleRate,
                  AUDIO_SAMPLE_RATE_EXACT);
  return true;
}

void loadStartupAmp() {
  for (int i = 0; i < g_numFiles; i++) {
    if (!strcasecmp(g_files[i], g_settings.ampPath)) {
      if (loadAmpIndex(i)) return;
      break;
    }
  }
  for (int i = 0; i < g_numFiles; i++)
    if (loadAmpIndex(i)) return;
}
