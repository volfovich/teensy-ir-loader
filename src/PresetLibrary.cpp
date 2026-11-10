#include "PresetLibrary.h"

#include <Arduino.h>
#include <SD.h>

#include "AmpLibrary.h"
#include "IRLibrary.h"
#include "Params.h"
#include "PresetFile.h"
#include "Settings.h"
#include "config.h"

using namespace ui;

static_assert(kPresetPathMax == IR_PATH_MAX, "a preset's paths are the IR and amp lists' paths");

static const char kDir[] = "/PRESETS";
static const int kMaxPresets = 99;
static const int kNameMax = 48;      // the file name, with ".txt"
static const int kUserNameMax = 24;  // what a new or renamed preset may be called

// The list and the recalled preset's values, in OCRAM (background only).
DMAMEM static char g_files[kMaxPresets][kNameMax];
DMAMEM static PresetData g_ref;      // the recalled preset, as in its file
DMAMEM static PresetData g_staging;  // a preset being read or written
static int g_count = 0;
static int g_current = -1;

int presetCount() { return g_count; }
int presetCurrent() { return g_current; }

// The name without ".txt", in a small rotating buffer (two at a time).
const char* presetName(int idx) {
  static char buf[2][kNameMax];
  static int which = 0;
  char* b = buf[which ^= 1];
  strlcpy(b, g_files[idx], kNameMax);
  const size_t n = strlen(b);
  if (n > 4) b[n - 4] = 0;
  return b;
}

static int numberOf(const char* name) {
  int n = 0, digits = 0;
  while (isdigit((unsigned char)name[digits]) && digits < 3) n = n * 10 + (name[digits++] - '0');
  return digits && name[digits] == ' ' ? n : 0;
}

int presetNumber(int idx) { return numberOf(g_files[idx]); }

int findPresetNumber(int n) {
  for (int i = 0; i < g_count; i++)
    if (n > 0 && presetNumber(i) == n) return i;
  return -1;
}

int findPresetName(const char* text) {
  const size_t n = strlen(text);
  for (int i = 0; i < g_count; i++)
    for (const char* s = g_files[i]; *s; s++)
      if (!strncasecmp(s, text, n)) return i;
  return -1;
}

static void pathOf(const char* file, char* path) { snprintf(path, IR_PATH_MAX, "%s/%.*s", kDir, kNameMax, file); }

// ---- Reading and writing -----------------------------------------------------------

// Reads a preset file into p (defaults for what it leaves out). False if it
// can't be opened.
static bool readFile(const char* path, PresetData* p, bool report) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  presetDefaults(p);
  char line[kPresetPathMax + 40];
  int len = 0, unknown = 0, bad = 0, lineNo = 0;
  bool longLine = false;
  auto finish = [&]() {
    line[len] = 0;
    lineNo++;
    if (longLine) {
      bad++;
    } else {
      const PresetLine r = parsePresetLine(line, p);
      if (r == PresetLine::kUnknown) unknown++;
      if (r == PresetLine::kBad) bad++;
      if (report && (r == PresetLine::kUnknown || r == PresetLine::kBad))
        Serial.printf("  line %d %s: %s\n", lineNo, r == PresetLine::kBad ? "has a bad value" : "skipped", line);
    }
    len = 0;
    longLine = false;
  };
  uint8_t buf[256];
  int n;
  while ((n = f.read(buf, sizeof(buf))) > 0) {
    for (int i = 0; i < n; i++) {
      if (buf[i] == '\n') finish();
      else if (len < (int)sizeof(line) - 1) line[len++] = (char)buf[i];
      else longLine = true;
    }
  }
  if (len) finish();
  f.close();
  (void)unknown;
  return true;
}

static void sinkToFile(void* ctx, const char* line) {
  File* f = static_cast<File*>(ctx);
  f->print(line);
  f->print("\r\n");  // CRLF: readable in any editor
}

// Writes p as path: to a temporary file first, then over the old one, so a
// failed write never leaves half a preset.
static bool writeFile(const char* path, const PresetData& p) {
  if (!SD.exists(kDir) && !SD.mkdir(kDir)) {
    Serial.println("Cannot create /PRESETS on the SD card.");
    return false;
  }
  char tmp[IR_PATH_MAX];
  pathOf(".saving.tmp", tmp);
  SD.remove(tmp);
  File f = SD.open(tmp, FILE_WRITE);
  if (!f) {
    Serial.println("Cannot write to the SD card.");
    return false;
  }
  writePreset(p, sinkToFile, &f);
  const bool ok = f.getWriteError() == 0;
  f.close();
  if (!ok) {
    SD.remove(tmp);
    Serial.println("Writing the preset failed (card full?).");
    return false;
  }
  if (SD.exists(path)) SD.remove(path);
  if (!SD.rename(tmp, path)) {
    Serial.println("Cannot rename the saved preset.");
    return false;
  }
  return true;
}

// The current sound as a preset.
static void currentSound(PresetData* p) {
  presetDefaults(p);
  for (int id = 0; id < kParamCount; id++)
    if (inPreset(id)) p->v[id] = g_settings.v[id];
  if (ampCurrent() >= 0) strlcpy(p->ampPath, ampPath(ampCurrent()), kPresetPathMax);
  if (irCurrent() >= 0) strlcpy(p->irPath, irPath(irCurrent()), kPresetPathMax);
}

// ---- The list ----------------------------------------------------------------------

static bool isPresetName(const char* n) {
  if (n[0] == '.') return false;  // hidden files, macOS "._" forks, our temporary file
  const size_t len = strlen(n);
  return len > 4 && strcasecmp(n + len - 4, ".txt") == 0;
}

// By number, then by name (unnumbered ones last).
static int cmpPreset(const void* a, const void* b) {
  const int na = numberOf((const char*)a), nb = numberOf((const char*)b);
  if (na != nb) return (na ? na : 1000) - (nb ? nb : 1000);
  return strcasecmp((const char*)a, (const char*)b);
}

static void setCurrentFromSettings() {
  g_current = -1;
  for (int i = 0; i < g_count && g_settings.presetPath[0]; i++) {
    char path[IR_PATH_MAX];
    pathOf(g_files[i], path);
    if (!strcasecmp(path, g_settings.presetPath)) {
      if (readFile(path, &g_ref, false)) g_current = i;
      break;
    }
  }
}

void scanPresets() {
  keepPreview();  // the list's numbers change
  g_count = 0;
  g_current = -1;
  if (!irSdOk()) return;
  File d = SD.open(kDir);
  if (!d || !d.isDirectory()) return;
  while (g_count < kMaxPresets) {
    File e = d.openNextFile();
    if (!e) break;
    if (!e.isDirectory() && isPresetName(e.name())) {
      if (strlen(e.name()) < (size_t)kNameMax) strlcpy(g_files[g_count++], e.name(), kNameMax);
      else Serial.printf("  skipped (name too long): %s\n", e.name());
    }
    e.close();
  }
  d.close();
  qsort(g_files, g_count, kNameMax, cmpPreset);
  setCurrentFromSettings();
  if (g_count) Serial.printf("Found %d preset(s) in /PRESETS.\n", g_count);
}

// ---- The edited mark ---------------------------------------------------------------

bool presetEdited() {
  if (g_current < 0) return false;
  currentSound(&g_staging);
  if (presetDiff(g_staging, g_ref, 0) >= 0) return true;
  if (g_ref.ampPath[0] && strcasecmp(g_ref.ampPath, g_staging.ampPath)) return true;
  if (g_ref.irPath[0] && strcasecmp(g_ref.irPath, g_staging.irPath)) return true;
  return false;
}

// ---- Recall, save, rename, delete --------------------------------------------------

// Sets the sound: the model and the IR (their own messages; a path not on
// the card keeps the current one), then the values. Returns how many values
// changed.
static int applySound(const PresetData& p) {
  if (p.ampPath[0]) {
    int i = -1;
    for (int k = 0; k < ampCount() && i < 0; k++)
      if (!strcasecmp(ampPath(k), p.ampPath)) i = k;
    if (i < 0) Serial.printf("  amp model not on the card, kept the current one: %s\n", p.ampPath);
    else if (i != ampCurrent()) loadAmpIndex(i);
  }
  if (p.irPath[0]) {
    int i = -1;
    for (int k = 0; k < irCount() && i < 0; k++)
      if (!strcasecmp(irPath(k), p.irPath)) i = k;
    if (i < 0) Serial.printf("  IR not on the card, kept the current one: %s\n", p.irPath);
    else if (i != irCurrent()) loadIRIndex(i);
  }
  int changed = 0;
  for (int id = 0; id < kParamCount; id++) {
    if (!inPreset(id) || g_settings.v[id] == p.v[id]) continue;
    setParamQuiet(id, p.v[id]);
    changed++;
  }
  return changed;
}

// Reads preset idx's file and sets its sound; it becomes the recalled one.
static bool recallFile(int idx) {
  if (idx < 0 || idx >= g_count) return false;
  char path[IR_PATH_MAX];
  pathOf(g_files[idx], path);
  Serial.printf("[preset] %s\n", presetName(idx));
  if (!readFile(path, &g_staging, true)) {
    Serial.printf("Cannot open %s\n", path);
    return false;
  }
  const int changed = applySound(g_staging);
  g_ref = g_staging;
  g_current = idx;
  strlcpy(g_settings.presetPath, path, IR_PATH_MAX);
  scheduleSave();
  Serial.printf("  %d setting(s) changed%s\n", changed, presetEdited() ? "; differs from the file (see 'pre show')" : "");
  return true;
}

// ---- Trying presets from the panel's list --------------------------------------------
// The first preview keeps the sound as it was (edits and all) and which
// preset was recalled; cancelPreview() puts them back, keepPreview() drops
// them. Anything else that changes presets keeps.

DMAMEM static PresetData g_snapSound, g_snapRef;
DMAMEM static char g_snapPath[IR_PATH_MAX];
static int g_snapIdx = -1;
static bool g_snap = false;

int presetSaved() { return g_snap ? g_snapIdx : g_current; }

static void restoreSnapshot() {
  applySound(g_snapSound);
  g_ref = g_snapRef;
  g_current = g_snapIdx;
  strlcpy(g_settings.presetPath, g_snapPath, IR_PATH_MAX);
  scheduleSave();
}

bool previewPreset(int idx) {
  if (idx < 0 || idx >= g_count) return false;
  if (!g_snap) {
    currentSound(&g_snapSound);
    g_snapRef = g_ref;
    g_snapIdx = g_current;
    strlcpy(g_snapPath, g_settings.presetPath, IR_PATH_MAX);
    g_snap = true;
  }
  if (idx == g_snapIdx) {  // back on the one recalled before: its sound as it was, edits too
    Serial.printf("[preset] %s (as before)\n", presetName(idx));
    restoreSnapshot();
    return true;
  }
  return recallFile(idx);
}

void keepPreview() { g_snap = false; }

void cancelPreview() {
  if (!g_snap) return;
  Serial.println("[preset] back to the sound before the list");
  restoreSnapshot();
  g_snap = false;
}

bool recallPreset(int idx) {
  // Keeping the preset being tried: it is already playing.
  if (g_snap && idx == g_current) {
    g_snap = false;
    return true;
  }
  g_snap = false;
  return recallFile(idx);
}

bool savePreset() {
  keepPreview();
  if (g_current < 0) {
    Serial.println("No preset recalled: 'pre new <name>' saves a new one.");
    return false;
  }
  char path[IR_PATH_MAX];
  pathOf(g_files[g_current], path);
  currentSound(&g_staging);
  if (!writeFile(path, g_staging)) return false;
  g_ref = g_staging;
  Serial.printf("Saved %s\n", presetName(g_current));
  return true;
}

// A name for a file: printable, none of / \ : * ? " < > |, trimmed, at most
// kUserNameMax characters.
static void cleanName(const char* in, char* out) {
  while (*in == ' ') in++;
  int n = 0;
  for (; *in && n < kUserNameMax; in++) {
    const char c = *in;
    if (c < 32 || c > 126 || strchr("/\\:*?\"<>|", c)) continue;
    out[n++] = c;
  }
  while (n && out[n - 1] == ' ') n--;
  out[n] = 0;
}

// Rescans and makes file the recalled preset.
static void rescanAndSelect(const char* file) {
  pathOf(file, g_settings.presetPath);
  scheduleSave();
  scanPresets();
}

bool newPreset(const char* name) {
  keepPreview();
  bool used[kMaxPresets + 1] = {};
  for (int i = 0; i < g_count; i++)
    if (presetNumber(i) <= kMaxPresets) used[presetNumber(i)] = true;
  int num = 1;
  while (num <= kMaxPresets && used[num]) num++;
  if (num > kMaxPresets) {
    Serial.println("All 99 preset numbers are taken.");
    return false;
  }
  char clean[kUserNameMax + 1], file[kNameMax], path[IR_PATH_MAX];
  cleanName(name ? name : "", clean);
  if (!clean[0]) snprintf(clean, sizeof(clean), "Preset %02d", num);
  snprintf(file, sizeof(file), "%02d %s.txt", num, clean);
  pathOf(file, path);
  currentSound(&g_staging);
  if (!writeFile(path, g_staging)) return false;
  rescanAndSelect(file);
  Serial.printf("Saved as %02d %s\n", num, clean);
  return true;
}

bool renamePreset(const char* name) {
  keepPreview();
  if (g_current < 0) {
    Serial.println("No preset recalled.");
    return false;
  }
  char clean[kUserNameMax + 1], file[kNameMax], from[IR_PATH_MAX], to[IR_PATH_MAX];
  cleanName(name ? name : "", clean);
  if (!clean[0]) {
    Serial.println("pre name <text>");
    return false;
  }
  const int num = presetNumber(g_current);
  if (num) snprintf(file, sizeof(file), "%02d %s.txt", num, clean);
  else snprintf(file, sizeof(file), "%s.txt", clean);
  pathOf(g_files[g_current], from);
  pathOf(file, to);
  if (strcasecmp(from, to) && SD.exists(to)) {
    Serial.println("A preset with that name exists.");
    return false;
  }
  if (!SD.rename(from, to)) {
    Serial.println("Cannot rename the preset.");
    return false;
  }
  rescanAndSelect(file);
  Serial.printf("Renamed to %s\n", file);
  return true;
}

bool deletePreset(int idx) {
  keepPreview();
  if (idx < 0 || idx >= g_count) return false;
  char path[IR_PATH_MAX];
  pathOf(g_files[idx], path);
  if (!SD.remove(path)) {
    Serial.printf("Cannot delete %s\n", path);
    return false;
  }
  Serial.printf("Deleted %s\n", presetName(idx));
  if (idx == g_current) {
    g_settings.presetPath[0] = 0;
    scheduleSave();
  }
  scanPresets();
  return true;
}

// ---- Serial ------------------------------------------------------------------------

void printPresets() {
  if (!g_count) {
    Serial.println("No presets in /PRESETS ('pre new <name>' saves the current sound as one).");
    return;
  }
  const bool edited = presetEdited();
  for (int i = 0; i < g_count; i++)
    Serial.printf("%c %s%s\n", i == g_current ? '>' : ' ', presetName(i), i == g_current && edited ? "*" : "");
}

void printPresetDiff() {
  if (g_current < 0) {
    Serial.println("No preset recalled.");
    return;
  }
  currentSound(&g_staging);
  int n = 0;
  for (int id = presetDiff(g_staging, g_ref, 0); id >= 0; id = presetDiff(g_staging, g_ref, id + 1), n++) {
    char now[24], was[24];
    formatTyped(paramDef(id), g_staging.v[id], now, sizeof(now));
    formatTyped(paramDef(id), g_ref.v[id], was, sizeof(was));
    Serial.printf("  %s %s (preset %s)\n", paramDef(id).key, now, was);
  }
  if (g_ref.ampPath[0] && strcasecmp(g_ref.ampPath, g_staging.ampPath))
    Serial.printf("  amp model %s (preset %s)\n", g_staging.ampPath, g_ref.ampPath), n++;
  if (g_ref.irPath[0] && strcasecmp(g_ref.irPath, g_staging.irPath))
    Serial.printf("  ir %s (preset %s)\n", g_staging.irPath, g_ref.irPath), n++;
  if (!n) Serial.printf("%s: as saved.\n", presetName(g_current));
}
