#include "DrumSongs.h"

#include <Arduino.h>
#include <SD.h>

#include "AudioGraph.h"
#include "IRLibrary.h"
#include "MidiDrums.h"
#include "Settings.h"
#include "config.h"

static const int kMaxFiles = 64, kNameMax = 48;
static const uint32_t kMaxEvents = 4096;
static const uint16_t kMaxBars = 512;

// Only used from loop(); the song buffers are read by the audio interrupt
// while a loaded song plays (they are only rewritten while it doesn't).
DMAMEM static char g_files[kMaxFiles][kNameMax];
DMAMEM static irdsp::SongEvent g_events[kMaxEvents];
DMAMEM static irdsp::SongBar g_bars[kMaxBars];
DMAMEM static irdsp::BassEvent g_bass[kMaxEvents];
static irdsp::DrumSong g_sdSong;
static int g_numFiles = 0;
static int g_selected = 0;  // 1-based, 0 = none
static int g_loopA = 0, g_loopB = 0;
static char g_name[24];

FLASHMEM static bool isMidiName(const char* n) {
  if (n[0] == '.') return false;
  const size_t len = strlen(n);
  return (len > 4 && !strcasecmp(n + len - 4, ".mid")) || (len > 5 && !strcasecmp(n + len - 5, ".midi"));
}

FLASHMEM static int cmpName(const void* a, const void* b) { return strcasecmp((const char*)a, (const char*)b); }

FLASHMEM void scanDrumSongs() {
  g_numFiles = 0;
  if (!irSdOk()) return;
  File d = SD.open("/DRUMS");
  if (!d || !d.isDirectory()) return;
  while (g_numFiles < kMaxFiles) {
    File e = d.openNextFile();
    if (!e) break;
    const char* name = e.name();
    if (!e.isDirectory() && isMidiName(name) && strlen(name) < (size_t)kNameMax) strcpy(g_files[g_numFiles++], name);
    e.close();
  }
  d.close();
  qsort(g_files, g_numFiles, kNameMax, cmpName);
  if (g_numFiles) Serial.printf("Found %d drum song(s) in /DRUMS.\n", g_numFiles);
}

FLASHMEM int drumSongTotal() { return irdsp::drumSongCount() + g_numFiles; }
FLASHMEM bool drumSongFromSd(int n) { return n > irdsp::drumSongCount(); }

// A file's name without the extension (into a static buffer).
FLASHMEM static const char* baseName(int file) {
  strncpy(g_name, g_files[file], sizeof(g_name) - 1);
  g_name[sizeof(g_name) - 1] = 0;
  char* dot = strrchr(g_name, '.');
  if (dot) *dot = 0;
  return g_name;
}

FLASHMEM const char* drumSongTitle(int n) {
  if (n < 1 || n > drumSongTotal()) return "";
  if (!drumSongFromSd(n)) return irdsp::drumSong(n - 1).title;
  return baseName(n - 1 - irdsp::drumSongCount());
}

FLASHMEM const char* drumSongName(int n) {
  if (n >= 1 && !drumSongFromSd(n)) return irdsp::drumSong(n - 1).name;
  return drumSongTitle(n);
}

FLASHMEM int findDrumSongIndex(const char* s) {
  if (!strcasecmp(s, "off")) return 0;
  if (s[0] >= '0' && s[0] <= '9') {
    const int n = atoi(s);
    return n >= 0 && n <= drumSongTotal() ? n : -1;
  }
  for (int n = 1; n <= drumSongTotal(); n++)
    if (!strcasecmp(s, drumSongName(n))) return n;
  return -1;
}

// Streams a file on the SD card to the MIDI reader.
struct FileSource : irdsp::MidiSource {
  File& f;
  explicit FileSource(File& file) : f(file) {}
  int read(uint8_t* dst, int n) override;
  bool seek(uint32_t pos) override;
};
FLASHMEM int FileSource::read(uint8_t* dst, int n) { return f.read(dst, n); }
FLASHMEM bool FileSource::seek(uint32_t pos) { return f.seek(pos); }

FLASHMEM static bool loadSdSong(int file) {
  char path[8 + kNameMax];
  snprintf(path, sizeof(path), "/DRUMS/%s", g_files[file]);
  File f = SD.open(path);
  if (!f) {
    Serial.printf("Cannot open %s\n", path);
    return false;
  }
  FileSource src(f);
  const uint32_t t0 = millis();
  const irdsp::MidiResult r =
      irdsp::loadMidiDrums(src, g_events, kMaxEvents, g_bars, kMaxBars, g_sdSong, g_bass, kMaxEvents);
  f.close();
  if (r.error) {
    Serial.printf("%s: %s\n", g_files[file], r.error);
    return false;
  }
  const char* base = baseName(file);
  strncpy(g_sdSong.name, base, sizeof(g_sdSong.name) - 1);
  g_sdSong.name[sizeof(g_sdSong.name) - 1] = 0;
  strncpy(g_sdSong.title, base, sizeof(g_sdSong.title) - 1);
  g_sdSong.title[sizeof(g_sdSong.title) - 1] = 0;
  Serial.printf("Loaded %s: %u bars, %lu hits, %u BPM%s (%lu ms)\n", g_files[file], g_sdSong.barCount,
                (unsigned long)g_sdSong.eventCount, g_sdSong.bpm, r.ignored ? ", some notes skipped" : "",
                (unsigned long)(millis() - t0));
  if (r.ignored) Serial.printf("  %lu drum notes have no lane (e.g. cowbell, tambourine)\n", (unsigned long)r.ignored);
  if (r.bassChannel > 0)
    Serial.printf("  bass from channel %d, %lu events\n", r.bassChannel, (unsigned long)g_sdSong.bassCount);
  else
    Serial.println("  no bass part");
  return true;
}

FLASHMEM bool selectDrumSong(int n, bool keepTempo) {
  if (n < 0 || n > drumSongTotal()) return false;
  const bool wasPlaying = convolver.drumsPlaying();
  if (wasPlaying) {
    setDrumsPlaying(false);
    // Wait for the interrupt to let go of the song before replacing it.
    const uint32_t t0 = millis();
    while (convolver.drumsRunning() && millis() - t0 < 100) delay(1);
  }
  const irdsp::DrumSong* song = nullptr;
  if (n >= 1) {
    if (drumSongFromSd(n)) {
      convolver.setDrumSong(nullptr);
      if (!loadSdSong(n - 1 - irdsp::drumSongCount())) {
        g_selected = 0;
        return false;
      }
      song = &g_sdSong;
    } else {
      song = &irdsp::drumSong(n - 1);
    }
  }
  g_selected = n;
  g_loopA = g_loopB = 0;
  convolver.setDrumLoop(0, 0);
  convolver.setDrumSong(song);
  if (g_settings.drumSong != n) {
    g_settings.drumSong = n;
    scheduleSave();
  }
  if (song) {
    Serial.printf("Drum song %d: %s, %u bars\n", n, song->title, song->barCount);
    if (!keepTempo) setDrumTempo(song->bpm);
  } else {
    Serial.println("Drum song off (the style plays)");
  }
  if (wasPlaying) setDrumsPlaying(true);
  return true;
}

FLASHMEM void setDrumLoop(int first, int last) {
  const irdsp::DrumSong* s = currentDrumSong();
  if (!s || first < 1) {
    g_loopA = g_loopB = 0;
  } else {
    g_loopA = constrain(first, 1, (int)s->barCount);
    g_loopB = constrain(last, g_loopA, (int)s->barCount);
  }
  convolver.setDrumLoop(g_loopA, g_loopB);
  if (g_loopA) Serial.printf("Loop bars %d-%d\n", g_loopA, g_loopB);
  else Serial.println("Loop off (the whole song)");
}

FLASHMEM int drumLoopFirst() { return g_loopA; }
FLASHMEM int drumLoopLast() { return g_loopB; }

FLASHMEM const irdsp::DrumSong* currentDrumSong() {
  if (g_selected < 1) return nullptr;
  return drumSongFromSd(g_selected) ? &g_sdSong : &irdsp::drumSong(g_selected - 1);
}
