#include "SerialUI.h"

#include <Arduino.h>
#include <Audio.h>

#include <initializer_list>

#include "AudioGraph.h"
#include "DrumKit.h"
#include "DrumMachine.h"
#include "DrumSongs.h"
#include "IRLibrary.h"
#include "Params.h"
#include "config.h"

using namespace ui;

static bool g_meter = false;

static float toDb(float lin) { return lin > 1e-6f ? 20.0f * log10f(lin) : -120.0f; }

// ---- Help ------------------------------------------------------------------------

// A setting's argument in the help: "[on|off]", "<hall|gated>", "<0-100>".
static void argHint(int id, char* buf, int n) {
  const ParamDef& d = paramDef(id);
  if (d.unit == Unit::kOnOff) {
    snprintf(buf, n, "[on|off]");
  } else if (id == kDrumStyle || id == kDrumKit || id == kDrumSong) {
    snprintf(buf, n, "[n|name]");
  } else if (id == kBassKey) {
    snprintf(buf, n, "<C..B>");
  } else if (d.choices) {
    // The choices' first words: "<off|2|4|8|16>".
    int len = snprintf(buf, n, "<");
    for (int i = d.min; i <= d.max && len < n; i++) {
      const char* c = d.choices[i];
      len += snprintf(buf + len, n - len, "%s%.*s", i > d.min ? "|" : "", (int)strcspn(c, " "), c);
    }
    if (len < n) snprintf(buf + len, n - len, ">");
  } else if (d.unit == Unit::kHalfDb) {
    snprintf(buf, n, "<dB>");
  } else {
    int lo, hi;
    paramLimits(id, &lo, &hi);
    snprintf(buf, n, "<%d-%d>", lo, hi);
  }
}

void printHelp() {
  Serial.print("\nCommands:\n"
               "  l                     list IR files\n"
               "  <number>              load IR by number\n"
               "  n / p                 next / previous IR\n");
  for (int id = 0; id < kValueCount; id++) {
    const ParamDef& d = paramDef(id);
    if (!d.key) continue;
    char hint[48], cmd[64], now[32];
    argHint(id, hint, sizeof(hint));
    snprintf(cmd, sizeof(cmd), "%s %s", d.key, hint);
    paramText(id, now, sizeof(now));
    Serial.printf("  %-21s %s (now %s)\n", cmd, d.name, now);
  }
  Serial.print("  drum loop <a> <b>|off  loop bars a..b of the song\n"
               "  drum train <step> <every> <target>  set the speed trainer and switch it on\n"
               "  m                     toggle the live level meter\n"
               "  s                     status (CPU, levels, latency)\n"
               "  r                     rescan the SD card\n"
               "A toggle alone flips; a choice alone steps to the next; style, kit and song alone list them.\n");
}

// ---- Lists and status --------------------------------------------------------------

static void printList() {
  if (!irSdOk()) { Serial.println("No SD card."); return; }
  for (int i = 0; i < irCount(); i++)
    Serial.printf("%c %3d  %s\n", i == irCurrent() ? '>' : ' ', i + 1, irPath(i));
  if (irCount() == 0) Serial.println("No .wav files in / or /IR");
}

static void printStyles() {
  for (int i = 0; i < irdsp::drumStyleCount(); i++) {
    const irdsp::DrumStyle& s = irdsp::drumStyle(i);
    Serial.printf("%c %2d  %-10s %s\n", i == getParam(kDrumStyle) ? '>' : ' ', i + 1, s.name, s.title);
  }
}

static void printKits() {
  for (int i = 0; i < irdsp::drumKitCount(); i++) {
    const irdsp::DrumKit& k = irdsp::drumKit(i);
    Serial.printf("%c %2d  %-10s %s\n", i == getParam(kDrumKit) ? '>' : ' ', i + 1, k.name, k.title);
  }
}

static void printSongs() {
  const int cur = getParam(kDrumSong);
  for (int n = 1; n <= drumSongTotal(); n++)
    Serial.printf("%c %2d  %-14s %s%s\n", n == cur ? '>' : ' ', n, drumSongName(n),
                  drumSongFromSd(n) ? "/DRUMS file" : drumSongTitle(n), drumSongFromSd(n) ? "" : " (built-in)");
  if (!drumSongTotal()) Serial.println("No songs.");
  else Serial.println("  drum song off  plays the style again");
}

// ", mix 30%, time 380ms": each setting's label (lower case) and value,
// after a comma unless first.
static void printValues(std::initializer_list<int> ids, bool first = false) {
  for (int id : ids) {
    char label[24], text[32];
    snprintf(label, sizeof(label), "%s", paramDef(id).label);
    label[0] = (char)tolower((unsigned char)label[0]);
    paramText(id, text, sizeof(text));
    Serial.printf("%s%s %s", first ? "" : ", ", label, text);
    first = false;
  }
}

// "on", "off", or "off (ringing out)" for an effect.
static const char* effectState(int onId, bool active) {
  return getParam(onId) ? "on" : (active ? "off (ringing out)" : "off");
}

static void printDrumStatus() {
  const irdsp::DrumMachine& dm = convolver.drums();
  const irdsp::DrumSong* song = currentDrumSong();
  const char* what = song ? song->title : irdsp::drumStyle(getParam(kDrumStyle)).title;
  if (dm.playing()) {
    Serial.printf("Drums:     playing %s, %.0f BPM, ", what, dm.tempoNow());
    if (dm.countingIn()) Serial.print("counting in");
    else if (song) Serial.printf("bar %d/%u", dm.songBar() + 1, song->barCount);
    else Serial.printf("bar %d", dm.bar() + 1);
  } else {
    Serial.printf("Drums:     stopped, %s, %d BPM", what, getParam(kDrumTempo));
  }
  if (song && drumLoopFirst()) Serial.printf(", loop %d-%d", drumLoopFirst(), drumLoopLast());
  if (song) printValues({kDrumKit, kDrumVolume, kDrumComp, kDrumCountIn});
  else printValues({kDrumKit, kDrumCrash, kDrumVolume, kDrumComp, kDrumCountIn});
  if (!song && getParam(kTrainerOn))
    Serial.printf(", trainer +%d BPM every %d bars to %d", getParam(kTrainerStep), getParam(kTrainerEvery),
                  getParam(kTrainerTarget));
  if (!getParam(kBassOn)) Serial.print(", bass off");
  else if (song && !song->bassCount) Serial.print(", bass on (the song has none)");
  else if (song) Serial.printf(", bass volume %d%%", getParam(kBassVolume));
  else Serial.printf(", bass in %s, volume %d%%", paramDef(kBassKey).choices[getParam(kBassKey)], getParam(kBassVolume));
  Serial.println();
}

static void printStatus() {
  auto& eng = convolver.engine();
  const float blockMs = AUDIO_BLOCK_SAMPLES * 1000.0f / AUDIO_SAMPLE_RATE_EXACT;
  Serial.printf("\nIR:        %s\n", irCurrent() >= 0 ? irPath(irCurrent()) : "(none - passthrough)");
  Serial.printf("Partitions %d x %d samples, %s\n", eng.activePartitions(), AUDIO_BLOCK_SAMPLES,
                convolver.bypassed() ? "BYPASSED" : "active");
  Serial.printf("Latency:   ~%.2f ms buffering (2 x %d-sample blocks) + codec\n", 2 * blockMs, AUDIO_BLOCK_SAMPLES);
  Serial.printf("Delay:     %s", effectState(kDelayOn, convolver.delayActive()));
  printValues({kDelayMix, kDelayTime, kDelayRepeats, kDelayTone, kDelayMod, kDelayDuck, kDelayDrive, kDelayEra});
  Serial.println();
  printDrumStatus();
  Serial.printf("Reverb:    %s", effectState(kReverbOn, convolver.reverbActive()));
  printValues({kReverbMode, kReverbMix, kReverbSize, kReverbTone, kReverbPreDelay, kReverbEra});
  printValues({getParam(kReverbMode) ? kReverbGate : kReverbMod});
  Serial.println();
  Serial.printf("CPU:       IR + effects %.1f%% avg / %.1f%% max (delay %.1f%%, reverb %.1f%%, drums %.1f%%), audio total %.1f%% max\n",
                convolver.cpuPercent(), convolver.cpuPercentMax(), convolver.delayCpuPercent(),
                convolver.reverbCpuPercent(), convolver.drumCpuPercent(), AudioProcessorUsageMax());
  Serial.printf("Memory:    audio blocks %d / %d used (max)\n", AudioMemoryUsageMax(), AUDIO_MEMORY_BLOCKS);
  Serial.printf("Levels:    in %.1f dBFS, out %.1f dBFS peak, clipped samples %lu\n",
                toDb(convolver.takeInputPeak()), toDb(convolver.takeOutputPeak()),
                (unsigned long)convolver.clipCount());
  Serial.print("Settings:  ");
  printValues({kOutGain, kLineIn, kLineOut, kHeadphones, kBypass}, true);
  Serial.println();
  convolver.resetCpuMax();
  AudioProcessorUsageMaxReset();
}

// ---- Commands --------------------------------------------------------------------

// Splits off the first word of s: returns the rest (after the spaces), or
// nullptr if there is none.
static char* splitWord(char* s) {
  char* rest = strchr(s, ' ');
  if (!rest) return nullptr;
  *rest++ = 0;
  while (*rest == ' ') rest++;
  return *rest ? rest : nullptr;
}

// The commands that aren't one setting: "drum style|kit|song" take names,
// "drum loop" and "drum train" several numbers, "bass key" a note name.
// True if the line was one of them.
static bool specialCommand(const char* cmd, char* arg) {
  char* val = arg ? splitWord(arg) : nullptr;
  if (!strcmp(cmd, "drum") && arg) {
    if (!strcmp(arg, "style")) {
      if (!val) printStyles();
      else if (irdsp::findDrumStyle(val) < 0) Serial.println("No such style (type 'drum style').");
      else setParam(kDrumStyle, irdsp::findDrumStyle(val));
      return true;
    }
    if (!strcmp(arg, "kit")) {
      if (!val) printKits();
      else if (irdsp::findDrumKit(val) < 0) Serial.println("No such kit (type 'drum kit').");
      else setParam(kDrumKit, irdsp::findDrumKit(val));
      return true;
    }
    if (!strcmp(arg, "song")) {
      if (!val) printSongs();
      else if (findDrumSongIndex(val) < 0) Serial.println("No such song (type 'drum song').");
      else setParam(kDrumSong, findDrumSongIndex(val));
      return true;
    }
    if (!strcmp(arg, "loop")) {
      if (!val || !strcmp(val, "off")) {
        setDrumLoop(0, 0);
      } else {
        char* p = val;
        const int a = (int)strtol(p, &p, 10);
        const int b = (int)strtol(p, &p, 10);
        setDrumLoop(a, b > 0 ? b : a);
      }
      return true;
    }
    if (!strcmp(arg, "train") && val) {
      // Three numbers. (No sscanf: it pulls ~30 KB of locale tables into RAM1.)
      int n[3], count = 0;
      for (char* p = val; *p && count < 3;) {
        n[count++] = (int)strtol(p, &p, 10);
        while (*p == ' ') p++;
      }
      if (count != 3) return false;
      setParam(kTrainerStep, n[0]);
      setParam(kTrainerEvery, n[1]);
      setParam(kTrainerTarget, n[2]);
      setParam(kTrainerOn, 1);
      return true;
    }
  }
  if (!strcmp(cmd, "bass") && arg && !strcmp(arg, "key") && val) {
    // A note name: C D E F G A B, optionally # or b.
    static const int8_t kPitch[] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
    const char c = (char)toupper((unsigned char)val[0]);
    if (c < 'A' || c > 'G') {
      Serial.println("Key: a note name, e.g. E, F#, Bb");
      return true;
    }
    const int key = kPitch[c - 'A'] + (val[1] == '#' ? 1 : (val[1] == 'b' ? -1 : 0));
    setParam(kBassKey, (key + 12) % 12);
    return true;
  }
  return false;
}

// The setting whose key the line starts with ("drum vol 80" -> kDrumVolume),
// trying two-word keys first; *value is what follows the key (or nullptr).
static int findParam(const char* cmd, const char* arg, const char** value) {
  if (arg) {
    const char* rest = strchr(arg, ' ');
    const size_t len = rest ? (size_t)(rest - arg) : strlen(arg);
    for (int id = 0; id < kValueCount; id++) {
      const char* key = paramDef(id).key;
      const size_t cl = strlen(cmd);
      if (key && !strncmp(key, cmd, cl) && key[cl] == ' ' && strlen(key + cl + 1) == len &&
          !strncmp(key + cl + 1, arg, len)) {
        while (rest && *rest == ' ') rest++;
        *value = rest && *rest ? rest : nullptr;
        return id;
      }
    }
  }
  for (int id = 0; id < kValueCount; id++) {
    if (paramDef(id).key && !strcmp(paramDef(id).key, cmd)) {
      *value = arg;
      return id;
    }
  }
  return -1;
}

static void handleCommand(char* line) {
  while (*line == ' ') line++;
  for (char* e = line + strlen(line); e > line && e[-1] == ' ';) *--e = 0;
  if (!*line) return;
  char* arg = splitWord(line);

  if (isdigit((unsigned char)line[0])) {
    if (!loadIRIndex(atoi(line) - 1)) Serial.println("No such IR number (type 'l').");
  } else if (!strcmp(line, "l")) {
    printList();
  } else if (!strcmp(line, "n")) {
    loadRelative(+1);
  } else if (!strcmp(line, "p")) {
    loadRelative(-1);
  } else if (!strcmp(line, "m")) {
    g_meter = !g_meter;
  } else if (!strcmp(line, "s")) {
    printStatus();
  } else if (!strcmp(line, "r")) {
    scanCard();
    loadStartupIR();
  } else if (!strcmp(line, "?") || !strcmp(line, "help")) {
    printHelp();
  } else {
    char argCopy[64] = "";
    if (arg) snprintf(argCopy, sizeof(argCopy), "%s", arg);  // specialCommand splits it
    if (specialCommand(line, arg ? argCopy : nullptr)) return;
    const char* value = nullptr;
    const int id = findParam(line, arg, &value);
    if (id < 0) {
      Serial.println("Unknown command (type ? for the list).");
      return;
    }
    const ParamDef& d = paramDef(id);
    int v;
    if (value) {
      if (parseValue(d, value, &v)) {
        setParam(id, v);
      } else {
        char hint[48];
        argHint(id, hint, sizeof(hint));
        Serial.printf("%s %s\n", d.key, hint);
      }
    } else if (d.unit == Unit::kOnOff) {
      setParam(id, !getParam(id));
    } else if (d.choices && d.unit != Unit::kName) {
      int lo, hi;
      paramLimits(id, &lo, &hi);
      setParam(id, getParam(id) >= hi ? lo : getParam(id) + 1);
    } else {
      char text[32];
      paramText(id, text, sizeof(text));
      Serial.printf("%s %s\n", d.name, text);
    }
  }
}

void serviceSerial() {
  static char buf[64];
  static int len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      buf[len] = 0;
      if (len) handleCommand(buf);
      len = 0;
    } else if (len < (int)sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

void serviceMeter() {
  static uint32_t last = 0;
  if (!g_meter || millis() - last < 200) return;
  last = millis();
  float in = toDb(convolver.takeInputPeak()), out = toDb(convolver.takeOutputPeak());
  char bar[41];
  int n = constrain((int)((in + 60.0f) * 40.0f / 60.0f), 0, 40);
  memset(bar, '#', n);
  memset(bar + n, '.', 40 - n);
  bar[40] = 0;
  Serial.printf("in %6.1f dBFS [%s]  out %6.1f dBFS%s\n", in, bar, out,
                (in > -0.5f || out > -0.1f) ? "  CLIP!" : "");
}
