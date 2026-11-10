#include "PresetFile.h"

#include <stdio.h>
#include <string.h>

namespace ui {

void presetDefaults(PresetData* p) {
  memset(p, 0, sizeof(*p));
  for (int id = 0; id < kParamCount; id++) p->v[id] = paramDef(id).def;
}

void writePreset(const PresetData& p, PresetLineSink sink, void* ctx) {
  char line[kPresetPathMax + 32];
  sink(ctx, "# Teensy IR Loader preset (PRESETS.md): serial commands, one per line");
  snprintf(line, sizeof(line), "amp model %s", p.ampPath);
  if (p.ampPath[0]) sink(ctx, line);
  snprintf(line, sizeof(line), "ir %s", p.irPath);
  if (p.irPath[0]) sink(ctx, line);
  for (int id = 0; id < kParamCount; id++) {
    if (!inPreset(id)) continue;
    const ParamDef& d = paramDef(id);
    char value[24];
    formatTyped(d, p.v[id], value, sizeof(value));
    snprintf(line, sizeof(line), "%s %s", d.key, value);
    sink(ctx, line);
  }
}

// Copies the path after a key, without trailing spaces or CR.
static void copyPath(const char* s, char* dst) {
  while (*s == ' ') s++;
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\n')) n--;
  if (n >= (size_t)kPresetPathMax) n = kPresetPathMax - 1;
  memcpy(dst, s, n);
  dst[n] = 0;
}

PresetLine parsePresetLine(const char* line, PresetData* p) {
  while (*line == ' ' || *line == '\t') line++;
  char buf[kPresetPathMax + 32];
  size_t n = strlen(line);
  while (n && (line[n - 1] == ' ' || line[n - 1] == '\r' || line[n - 1] == '\n' || line[n - 1] == '\t')) n--;
  if (!n || line[0] == '#') return PresetLine::kSkip;
  if (n >= sizeof(buf)) return PresetLine::kBad;
  memcpy(buf, line, n);
  buf[n] = 0;

  if (!strncmp(buf, "amp model ", 10)) {
    copyPath(buf + 10, p->ampPath);
    return PresetLine::kOk;
  }
  if (!strncmp(buf, "ir ", 3)) {
    copyPath(buf + 3, p->irPath);
    return PresetLine::kOk;
  }
  // The longest preset key the line starts with, followed by a space:
  // "amp in" wins over "amp".
  int best = -1;
  size_t bestLen = 0;
  for (int id = 0; id < kParamCount; id++) {
    if (!inPreset(id)) continue;
    const char* key = paramDef(id).key;
    const size_t kl = strlen(key);
    if (kl > bestLen && !strncmp(buf, key, kl) && buf[kl] == ' ') best = id, bestLen = kl;
  }
  if (best < 0) return PresetLine::kUnknown;
  const char* value = buf + bestLen;
  while (*value == ' ') value++;
  const ParamDef& d = paramDef(best);
  int v;
  if (!parseValue(d, value, &v)) return PresetLine::kBad;
  if (v < d.min) v = d.min;
  if (v > d.max) v = d.max;
  p->v[best] = (int16_t)v;
  return PresetLine::kOk;
}

int presetDiff(const PresetData& a, const PresetData& b, int from) {
  for (int id = from < 0 ? 0 : from; id < kParamCount; id++)
    if (inPreset(id) && a.v[id] != b.v[id]) return id;
  return -1;
}

}  // namespace ui
