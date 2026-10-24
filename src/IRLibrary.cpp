#include "IRLibrary.h"

#include <Arduino.h>
#include <SD.h>

#include "AudioGraph.h"
#include "DrumSongs.h"
#include "IRLoader.h"
#include "Settings.h"
#include "config.h"

// Load buffers live in the 512 KB OCRAM (DMAMEM) so the DTCM stays free for
// the convolver's spectra and delay line, which are accessed every block.
// The source is streamed through g_scratch, so any file length fits.
static const int kLoadScratch = 2048;
DMAMEM static float g_scratch[kLoadScratch];
DMAMEM static float g_ir[IR_MAX_LENGTH];
// Resampler kernel table: enough for 192 kHz sources (makes loading ~50x faster).
static const int kKernelTableSize = 14336;
DMAMEM static float g_kernelTable[kKernelTableSize];

// ---------------------------------------------------------------------------
// IR file list
// ---------------------------------------------------------------------------
// Only used from loop(), so it lives in OCRAM too (rows past g_numFiles are
// never read, so it needs no initialization).
DMAMEM static char g_files[IR_MAX_FILES][IR_PATH_MAX];
static int g_numFiles = 0;
static int g_current = -1;
static bool g_sdOk = false;

bool irSdOk() { return g_sdOk; }
int irCount() { return g_numFiles; }
int irCurrent() { return g_current; }
const char* irPath(int idx) { return g_files[idx]; }

FLASHMEM static bool isWavName(const char* n) {
  if (n[0] == '.') return false;  // hidden files, macOS "._" resource forks
  size_t len = strlen(n);
  return len > 4 && strcasecmp(n + len - 4, ".wav") == 0;
}

FLASHMEM static int cmpPath(const void* a, const void* b) {
  return strcasecmp((const char*)a, (const char*)b);
}

FLASHMEM static void scanDir(const char* dir) {
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) return;
  while (g_numFiles < IR_MAX_FILES) {
    File e = d.openNextFile();
    if (!e) break;
    const char* name = e.name();
    if (!e.isDirectory() && isWavName(name)) {
      const char* sep = (dir[strlen(dir) - 1] == '/') ? "" : "/";
      int n = snprintf(g_files[g_numFiles], IR_PATH_MAX, "%s%s%s", dir, sep, name);
      if (n > 0 && n < IR_PATH_MAX) g_numFiles++;
      else Serial.printf("  skipped (path too long): %s\n", name);
    }
    e.close();
  }
  d.close();
}

FLASHMEM void scanCard() {
  g_numFiles = 0;
  g_current = -1;
  g_sdOk = SD.begin(SDCARD_CS_PIN);
  if (!g_sdOk) {
    Serial.println("SD card not found - running as clean passthrough. Type 'r' to retry.");
    return;
  }
  const char* dirs[] = IR_SCAN_DIRS;
  for (const char* d : dirs) scanDir(d);
  qsort(g_files, g_numFiles, IR_PATH_MAX, cmpPath);
  Serial.printf("Found %d IR file(s).\n", g_numFiles);
  scanDrumSongs();
}

// ---------------------------------------------------------------------------
// IR loading
// ---------------------------------------------------------------------------
class SdSource : public irdsp::ByteSource {
 public:
  explicit SdSource(File& f) : f_(f) {}
  int read(void* dst, int n) override { return f_.read(dst, n); }
  bool seek(uint32_t pos) override { return f_.seek(pos); }

 private:
  File& f_;
};

FLASHMEM bool loadIRIndex(int idx) {
  if (idx < 0 || idx >= g_numFiles) return false;
  auto& eng = convolver.engine();
  // A previous crossfade may still be running (25 ms max).
  uint32_t t = millis();
  while (!eng.readyForLoad()) {
    if (millis() - t > 500) { Serial.println("Busy, try again."); return false; }
    yield();
  }

  const char* path = g_files[idx];
  File f = SD.open(path, FILE_READ);
  if (!f) { Serial.printf("Cannot open %s\n", path); return false; }

  irdsp::IRPrepOptions opt;
  opt.targetRate = AUDIO_SAMPLE_RATE_EXACT;
  opt.channels = IR_CHANNEL_MODE;
  opt.normalize = IR_NORMALIZE;
  opt.trimLeadDb = IR_TRIM_LEAD_DB;
  opt.trimTailDb = IR_TRIM_TAIL_DB;
  opt.kernelTable = g_kernelTable;
  opt.kernelTableCap = kKernelTableSize;

  irdsp::IRPrepResult res;
  const char* err = "";
  SdSource src(f);
  uint32_t t0 = millis();
  bool ok = irdsp::prepareIR(src, opt, g_scratch, kLoadScratch, g_ir, IR_MAX_LENGTH, res, &err);
  f.close();
  if (!ok) {
    Serial.printf("Failed to load %s: %s\n", path, err);
    return false;
  }
  eng.loadIR(g_ir, res.length);
  uint32_t ms = millis() - t0;

  g_current = idx;
  strlcpy(g_settings.irPath, path, IR_PATH_MAX);
  scheduleSave();

  Serial.printf("[%d/%d] %s\n", idx + 1, g_numFiles, path);
  Serial.printf("      %u Hz, %u ch, %u-bit %s, %d frames -> %d samples (%.1f ms, %d partitions)%s%s, %lu ms\n",
                (unsigned)res.wav.sampleRate, res.wav.channels, res.wav.bitsPerSample,
                res.wav.format == 3 ? "float" : "PCM", res.sourceFrames, res.length,
                res.length * 1000.0f / AUDIO_SAMPLE_RATE_EXACT,
                (res.length + AUDIO_BLOCK_SAMPLES - 1) / AUDIO_BLOCK_SAMPLES,
                res.resampled ? ", resampled" : "", res.truncated ? ", TRUNCATED" : "",
                (unsigned long)ms);
  if (res.leadTrimmed > 0)
    Serial.printf("      removed %d samples of leading silence\n", res.leadTrimmed);
  return true;
}

FLASHMEM void loadRelative(int step) {
  if (g_numFiles == 0) { Serial.println("No IR files."); return; }
  int start = g_current < 0 ? (step > 0 ? -1 : 0) : g_current;
  // Skip over files that fail to load.
  for (int tries = 0; tries < g_numFiles; tries++) {
    start = (start + step + g_numFiles) % g_numFiles;
    if (loadIRIndex(start)) return;
  }
}

FLASHMEM void loadStartupIR() {
  if (g_numFiles == 0) return;
  for (int i = 0; i < g_numFiles; i++) {
    if (strcasecmp(g_files[i], g_settings.irPath) == 0) {
      if (loadIRIndex(i)) return;
      break;
    }
  }
  loadRelative(+1);
}
