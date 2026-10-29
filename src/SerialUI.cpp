#include "SerialUI.h"

#include <Arduino.h>
#include <Audio.h>

#include "AudioGraph.h"
#include "DrumKit.h"
#include "DrumMachine.h"
#include "DrumSongs.h"
#include "IRLibrary.h"
#include "Settings.h"
#include "config.h"

static bool g_meter = false;

static float toDb(float lin) { return lin > 1e-6f ? 20.0f * log10f(lin) : -120.0f; }

// "every 4 bars" or "off".
static const char* crashText() {
  static char buf[16];
  if (!g_settings.drumCrash) return "off";
  snprintf(buf, sizeof(buf), "every %u bars", g_settings.drumCrash);
  return buf;
}

// "2 bars", "1 bar" or "off".
static const char* countText() {
  static char buf[12];
  if (!g_settings.drumCountIn) return "off";
  snprintf(buf, sizeof(buf), "%u bar%s", g_settings.drumCountIn, g_settings.drumCountIn == 1 ? "" : "s");
  return buf;
}

// The selected song's name, or "off".
static const char* songText() {
  const irdsp::DrumSong* s = currentDrumSong();
  return s ? s->name : "off";
}

// "9-16" or "off".
static const char* loopText() {
  static char buf[16];
  if (!drumLoopFirst()) return "off";
  snprintf(buf, sizeof(buf), "%d-%d", drumLoopFirst(), drumLoopLast());
  return buf;
}

static void printSongs() {
  const irdsp::DrumSong* cur = currentDrumSong();
  for (int n = 1; n <= drumSongTotal(); n++) {
    const bool sel = cur && !strcmp(drumSongName(n), cur->name);
    Serial.printf("%c %2d  %-14s %s%s\n", sel ? '>' : ' ', n, drumSongName(n),
                  drumSongFromSd(n) ? "/DRUMS file" : drumSongTitle(n), drumSongFromSd(n) ? "" : " (built-in)");
  }
  if (!drumSongTotal()) Serial.println("No songs.");
  else Serial.println("  drum song off  plays the style again");
}

void printHelp() {
  // The help text in flash (PSTR): as a plain literal it would be copied to
  // RAM1, taking stack space.
  Serial.printf(PSTR(
      "\nCommands:\n"
      "  l            list IR files\n"
      "  <number>     load IR by number\n"
      "  n / p        next / previous IR\n"
      "  b            toggle bypass\n"
      "  g <dB>       output gain after the IR (now %.1f dB)\n"
      "  dl           toggle delay (now %s)\n"
      "  dmix <0-100> delay dry/wet, 0 = dry only (now %u)\n"
      "  dtime <ms>   delay time, 20-%d ms (now %u)\n"
      "  dfb <0-100>  delay repeats (now %u)\n"
      "  dtone <0-100> delay brightness (now %u)\n"
      "  dmod <0-100> delay modulation depth (now %u)\n"
      "  dtype [0|1]  delay type, 0 = ping-pong, 1 = studio 80s (now %s)\n"
      "  dduck <0-100> studio 80s: repeats duck while you play (now %u)\n"
      "  ddrive <0-100> studio 80s: input grit (now %u)\n"
      "  dera <0-100> studio 80s: 0 = 16-bit/16 kHz, 100 = 10-bit/6 kHz (now %u)\n"
      "  drum         start / stop the drums (now %s)\n"
      "  bpm <40-240> drum tempo (now %u)\n"
      "  drum style [n|name]  list the styles / choose one (now %s)\n"
      "  drum kit [n|name]    list the kits / choose one (now %s)\n"
      "  drum song [n|name|off]  list the songs / play one instead of the style (now %s)\n"
      "  drum loop <a> <b> | off  loop bars a..b of the song (now %s)\n"
      "  drum crash <0|2|4|8|16>  crash every N bars, 0 = off (now %s)\n"
      "  drum vol <0-100>     drum volume (now %u)\n"
      "  drum comp <0-100>    drum-bus compression, 0 = natural (now %u)\n"
      "  drum count [0-4]     count-in bars, 0 = off; alone toggles (now %s)\n"
      "  drum train [step every target]  toggle / set the speed trainer (now %s)\n"
      "  bass [on|off]  the bass with the drums; alone toggles (now %s)\n"
      "  bass vol|tone <0-100>  bass volume, brightness (now %u, %u)\n"
      "  bass key <C..B>  the styles' key, e.g. E, F#, Bb; songs keep their own (now %s)\n"
      "  rv           toggle reverb (now %s)\n"
      "  mix <0-100>  reverb dry/wet, 0 = dry only (now %u)\n"
      "  size <0-100> reverb time; 80s gated: decaying .. flat (50) .. rising (now %u)\n"
      "  tone <0-100> reverb brightness (now %u)\n"
      "  rtype [0|1]  reverb type, 0 = plate, 1 = 80s (now %s)\n"
      "  rmode [hall|gated]  80s: hall or gated reverb (now %s)\n"
      "  rpre <0-150> 80s: pre-delay, ms (now %u)\n"
      "  rmod <0-100> 80s hall: chorus in the tail (now %u)\n"
      "  rera <0-100> 80s: 0 = 16-bit/16 kHz, 100 = 10-bit/6 kHz (now %u)\n"
      "  rgate <100-600>  80s gated: burst length, ms (now %u)\n"
      "  in <0-15>    line-in sensitivity, 0 = 3.12 Vpp, least sensitive (now %u)\n"
      "  out <13-31>  line-out level, 13 = loudest (now %u)\n"
      "  hp <0-100>   headphone volume (now %d%%)\n"
      "  m            toggle live level meter\n"
      "  s            status (CPU, levels, latency)\n"
      "  r            rescan SD card\n"),
      g_settings.outGainDb, g_settings.delayOn ? "on" : "off", g_settings.delayMix, DELAY_MAX_MS,
      g_settings.delayTimeMs, g_settings.delayFeedback, g_settings.delayTone, g_settings.delayMod,
      delayTypeName(g_settings.delayType), g_settings.delayDuck, g_settings.delayDrive,
      g_settings.delayEra, convolver.drumsPlaying() ? "playing" : "stopped", g_settings.drumTempo,
      irdsp::drumStyle(g_settings.drumStyle).name, irdsp::drumKit(g_settings.drumKit).name, songText(), loopText(),
      crashText(),
      g_settings.drumVolume, g_settings.drumComp,
      countText(), g_settings.trainerOn ? "on" : "off", g_settings.bassOn ? "on" : "off", g_settings.bassVolume,
      g_settings.bassTone, bassKeyName(g_settings.bassKey), g_settings.reverbOn ? "on" : "off", g_settings.reverbMix,
      g_settings.reverbSize, g_settings.reverbTone, g_settings.reverbType ? "80s" : "plate",
      g_settings.reverbMode ? "gated" : "hall", g_settings.reverbPreDelay, g_settings.reverbMod, g_settings.reverbEra,
      g_settings.reverbGateMs, g_settings.lineIn, g_settings.lineOut,
      (int)lroundf(g_settings.hpVolume * 100));
}

static void printList() {
  if (!irSdOk()) { Serial.println("No SD card."); return; }
  for (int i = 0; i < irCount(); i++)
    Serial.printf("%c %3d  %s\n", i == irCurrent() ? '>' : ' ', i + 1, irPath(i));
  if (irCount() == 0) Serial.println("No .wav files in / or /IR");
}

static void printDrums() {
  const irdsp::DrumSong* song = currentDrumSong();
  if (convolver.drumsPlaying()) {
    if (song) Serial.printf("Drums:     playing song \"%s\", %.0f BPM, ", song->title, convolver.drumTempoNow());
    else Serial.printf("Drums:     playing %s, %.0f BPM, ", irdsp::drumStyle(g_settings.drumStyle).title, convolver.drumTempoNow());
    if (convolver.drumsCountingIn()) Serial.print("counting in");
    else if (song) Serial.printf("bar %d/%u", convolver.drumSongBar() + 1, song->barCount);
    else Serial.printf("bar %d", convolver.drumBar() + 1);
  } else if (song) {
    Serial.printf("Drums:     stopped, song \"%s\" (%u bars), %u BPM", song->title, song->barCount, g_settings.drumTempo);
  } else {
    Serial.printf("Drums:     stopped, %s, %u BPM", irdsp::drumStyle(g_settings.drumStyle).title,
                  g_settings.drumTempo);
  }
  if (song && drumLoopFirst()) Serial.printf(", loop %d-%d", drumLoopFirst(), drumLoopLast());
  Serial.printf(", kit %s", irdsp::drumKit(g_settings.drumKit).name);
  if (!song) Serial.printf(", crash %s", crashText());
  Serial.printf(", vol %u%%, comp %u%%, count-in %s", g_settings.drumVolume, g_settings.drumComp, countText());
  if (g_settings.trainerOn && !song)
    Serial.printf(", trainer +%u every %u bar%s to %u", g_settings.trainerStep, g_settings.trainerEvery,
                  g_settings.trainerEvery == 1 ? "" : "s", g_settings.trainerTarget);
  if (!g_settings.bassOn) Serial.print(", bass off");
  else if (song) Serial.printf(", bass %s, vol %u%%", song->bassCount ? "on" : "on (the song has none)", g_settings.bassVolume);
  else Serial.printf(", bass in %s, vol %u%%", bassKeyName(g_settings.bassKey), g_settings.bassVolume);
  Serial.println();
}

static void printStyles() {
  for (int i = 0; i < irdsp::drumStyleCount(); i++) {
    const irdsp::DrumStyle& s = irdsp::drumStyle(i);
    Serial.printf("%c %2d  %-10s %s\n", i == g_settings.drumStyle ? '>' : ' ', i + 1, s.name, s.title);
  }
}

static void printKits() {
  for (int i = 0; i < irdsp::drumKitCount(); i++) {
    const irdsp::DrumKit& k = irdsp::drumKit(i);
    Serial.printf("%c %2d  %-10s %s\n", i == g_settings.drumKit ? '>' : ' ', i + 1, k.name, k.title);
  }
}

// "drum ..." subcommands; arg is the rest of the line (may be null).
static void drumCommand(char* arg) {
  if (!arg || !*arg) {
    setDrumsPlaying(!convolver.drumsPlaying());
    return;
  }
  char* val = strchr(arg, ' ');
  if (val) { *val++ = 0; while (*val == ' ') val++; }
  if (!strcmp(arg, "style")) {
    if (!val || !*val) { printStyles(); return; }
    const int idx = irdsp::findDrumStyle(val);
    if (idx < 0) Serial.println("No such style (type 'drum style').");
    else setDrumStyle(idx);
  } else if (!strcmp(arg, "kit")) {
    if (!val || !*val) { printKits(); return; }
    const int idx = irdsp::findDrumKit(val);
    if (idx < 0) Serial.println("No such kit (type 'drum kit').");
    else setDrumKit(idx);
  } else if (!strcmp(arg, "song")) {
    if (!val || !*val) { printSongs(); return; }
    const int n = findDrumSongIndex(val);
    if (n < 0) Serial.println("No such song (type 'drum song').");
    else selectDrumSong(n);
  } else if (!strcmp(arg, "loop")) {
    if (!val || !*val || !strcmp(val, "off")) {
      setDrumLoop(0, 0);
    } else {
      char* p = val;
      const int a = (int)strtol(p, &p, 10);
      const int b = (int)strtol(p, &p, 10);
      setDrumLoop(a, b > 0 ? b : a);
    }
  } else if (!strcmp(arg, "crash")) {
    if (!val || !*val) Serial.printf("Crash %s\n", crashText());
    else setDrumCrash(atoi(val));
  } else if (!strcmp(arg, "comp")) {
    if (!val || !*val) Serial.printf("Drum compression %u%%\n", g_settings.drumComp);
    else setDrumComp(atoi(val));
  } else if (!strcmp(arg, "vol") && val) {
    setDrumVolume(atoi(val));
  } else if (!strcmp(arg, "count")) {
    if (val && *val) setDrumCountIn(atoi(val));
    else setDrumCountIn(g_settings.drumCountIn ? 0 : DEFAULT_DRUM_COUNT_IN);
  } else if (!strcmp(arg, "train")) {
    // Three numbers, or none to toggle. (No sscanf: it would pull about
    // 30 KB of locale tables into RAM1.)
    int n[3], count = 0;
    for (char* p = val; p && *p && count < 3;) {
      n[count++] = (int)strtol(p, &p, 10);
      while (*p == ' ') p++;
    }
    if (count == 3) setDrumTrainer(true, n[0], n[1], n[2]);
    else setDrumTrainer(!g_settings.trainerOn, g_settings.trainerStep, g_settings.trainerEvery, g_settings.trainerTarget);
  } else {
    printHelp();
  }
}

// "bass ..." subcommands; arg is the rest of the line (may be null).
static void bassCommand(char* arg) {
  if (!arg || !*arg) {
    setBassOn(!g_settings.bassOn);
    return;
  }
  char* val = strchr(arg, ' ');
  if (val) { *val++ = 0; while (*val == ' ') val++; }
  if (!strcmp(arg, "on") || !strcmp(arg, "off")) {
    setBassOn(!strcmp(arg, "on"));
  } else if (!strcmp(arg, "vol") && val && *val) {
    setBassVolume(atoi(val));
  } else if (!strcmp(arg, "tone") && val && *val) {
    setBassTone(atoi(val));
  } else if (!strcmp(arg, "key") && val && *val) {
    // A note name: C D E F G A B, optionally # or b.
    static const int8_t kPitch[] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
    const char c = (char)toupper((unsigned char)val[0]);
    if (c < 'A' || c > 'G') {
      Serial.println("Key: a note name, e.g. E, F#, Bb");
      return;
    }
    int key = kPitch[c - 'A'];
    if (val[1] == '#') key++;
    else if (val[1] == 'b') key--;
    setBassKey(key);
  } else {
    printHelp();
  }
}

static void printStatus() {
  auto& eng = convolver.engine();
  const float blockMs = AUDIO_BLOCK_SAMPLES * 1000.0f / AUDIO_SAMPLE_RATE_EXACT;
  Serial.printf("\nIR:        %s\n", irCurrent() >= 0 ? irPath(irCurrent()) : "(none - passthrough)");
  Serial.printf("Partitions %d x %d samples, %s\n", eng.activePartitions(), AUDIO_BLOCK_SAMPLES,
                convolver.bypassed() ? "BYPASSED" : "active");
  Serial.printf("Latency:   ~%.2f ms buffering (2 x %d-sample blocks) + codec\n",
                2 * blockMs, AUDIO_BLOCK_SAMPLES);
  Serial.printf("Delay:     %s, %s, mix %u%%, %u ms, repeats %u%%, tone %u%%, mod %u%%",
                g_settings.delayOn ? "on" : (convolver.delayActive() ? "off (repeats ringing)" : "off"),
                delayTypeName(g_settings.delayType), g_settings.delayMix, g_settings.delayTimeMs,
                g_settings.delayFeedback, g_settings.delayTone, g_settings.delayMod);
  if (g_settings.delayType)
    Serial.printf(", duck %u%%, drive %u%%, era %u%%", g_settings.delayDuck, g_settings.delayDrive,
                  g_settings.delayEra);
  Serial.println();
  printDrums();
  Serial.printf("Reverb:    %s, %s, mix %u%%, size %u%%, tone %u%%",
                g_settings.reverbOn ? "on" : (convolver.reverbActive() ? "off (tail ringing)" : "off"),
                reverbTypeName(), g_settings.reverbMix, g_settings.reverbSize, g_settings.reverbTone);
  if (g_settings.reverbType) {
    Serial.printf(", pre %u ms, era %u%%", g_settings.reverbPreDelay, g_settings.reverbEra);
    if (g_settings.reverbMode) Serial.printf(", gate %u ms", g_settings.reverbGateMs);
    else Serial.printf(", mod %u%%", g_settings.reverbMod);
  }
  Serial.println();
  Serial.printf("CPU:       IR + effects %.1f%% avg / %.1f%% max (delay %.1f%%, reverb %.1f%%, drums %.1f%%), audio total %.1f%% max\n",
                convolver.cpuPercent(), convolver.cpuPercentMax(), convolver.delayCpuPercent(),
                convolver.reverbCpuPercent(), convolver.drumCpuPercent(), AudioProcessorUsageMax());
  Serial.printf("Memory:    audio blocks %d / %d used (max)\n", AudioMemoryUsageMax(),
                AUDIO_MEMORY_BLOCKS);
  Serial.printf("Levels:    in %.1f dBFS, out %.1f dBFS peak, clipped samples %lu\n",
                toDb(convolver.takeInputPeak()), toDb(convolver.takeOutputPeak()),
                (unsigned long)convolver.clipCount());
  Serial.printf("Settings:  gain %.1f dB, line-in %u, line-out %u, headphones %d%%\n",
                g_settings.outGainDb, g_settings.lineIn, g_settings.lineOut,
                (int)lroundf(g_settings.hpVolume * 100));
  convolver.resetCpuMax();
  AudioProcessorUsageMaxReset();
}

static void handleCommand(char* line) {
  while (*line == ' ') line++;
  for (char* e = line + strlen(line); e > line && e[-1] == ' ';) *--e = 0;
  if (!*line) return;

  char* arg = strchr(line, ' ');
  if (arg) { *arg++ = 0; while (*arg == ' ') arg++; }

  if (isdigit((unsigned char)line[0])) {
    int idx = atoi(line) - 1;
    if (!loadIRIndex(idx)) Serial.println("No such IR number (type 'l').");
  } else if (!strcmp(line, "l")) {
    printList();
  } else if (!strcmp(line, "n")) {
    loadRelative(+1);
  } else if (!strcmp(line, "p")) {
    loadRelative(-1);
  } else if (!strcmp(line, "b")) {
    setBypass(!g_settings.bypass);
  } else if (!strcmp(line, "dl")) {
    setDelayOn(!g_settings.delayOn);
  } else if (!strcmp(line, "dmix") && arg) {
    setDelayMix(atoi(arg));
  } else if (!strcmp(line, "dtime") && arg) {
    setDelayTimeMs(atoi(arg));
  } else if (!strcmp(line, "dfb") && arg) {
    setDelayFeedback(atoi(arg));
  } else if (!strcmp(line, "dtone") && arg) {
    setDelayTone(atoi(arg));
  } else if (!strcmp(line, "dmod") && arg) {
    setDelayMod(atoi(arg));
  } else if (!strcmp(line, "dtype")) {
    setDelayType(arg ? atoi(arg) : !g_settings.delayType);
  } else if (!strcmp(line, "dduck") && arg) {
    setDelayDuck(atoi(arg));
  } else if (!strcmp(line, "ddrive") && arg) {
    setDelayDrive(atoi(arg));
  } else if (!strcmp(line, "dera") && arg) {
    setDelayEra(atoi(arg));
  } else if (!strcmp(line, "drum")) {
    drumCommand(arg);
  } else if (!strcmp(line, "bass")) {
    bassCommand(arg);
  } else if (!strcmp(line, "bpm") && arg) {
    setDrumTempo(atoi(arg));
  } else if (!strcmp(line, "rv")) {
    setReverbOn(!g_settings.reverbOn);
  } else if (!strcmp(line, "mix") && arg) {
    setReverbMix(atoi(arg));
  } else if (!strcmp(line, "size") && arg) {
    setReverbSize(atoi(arg));
  } else if (!strcmp(line, "tone") && arg) {
    setReverbTone(atoi(arg));
  } else if (!strcmp(line, "rtype")) {
    setReverbType(arg ? atoi(arg) : !g_settings.reverbType);
  } else if (!strcmp(line, "rmode")) {
    if (!arg) setReverbMode(!g_settings.reverbMode);
    else setReverbMode(!strcmp(arg, "gated") || !strcmp(arg, "1") ? 1 : 0);
  } else if (!strcmp(line, "rpre") && arg) {
    setReverbPreDelay(atoi(arg));
  } else if (!strcmp(line, "rmod") && arg) {
    setReverbMod(atoi(arg));
  } else if (!strcmp(line, "rera") && arg) {
    setReverbEra(atoi(arg));
  } else if (!strcmp(line, "rgate") && arg) {
    setReverbGate(atoi(arg));
  } else if (!strcmp(line, "g") && arg) {
    setOutputGainDb((float)atof(arg));
  } else if (!strcmp(line, "in")) {
    if (arg) setLineInLevel(atoi(arg));
    else Serial.printf("Line-in level %u\n", g_settings.lineIn);
  } else if (!strcmp(line, "out")) {
    if (arg) setLineOutLevel(atoi(arg));
    else Serial.printf("Line-out level %u\n", g_settings.lineOut);
  } else if (!strcmp(line, "hp")) {
    if (arg) setHeadphoneVolume(atoi(arg));
    else Serial.printf("Headphone volume %d%%\n", (int)lroundf(g_settings.hpVolume * 100));
  } else if (!strcmp(line, "m")) {
    g_meter = !g_meter;
  } else if (!strcmp(line, "s")) {
    printStatus();
  } else if (!strcmp(line, "r")) {
    scanCard();
    loadStartupIR();
  } else {
    printHelp();
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
