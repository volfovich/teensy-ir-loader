#include "SerialUI.h"

#include <Arduino.h>
#include <Audio.h>

#include "AudioGraph.h"
#include "IRLibrary.h"
#include "Settings.h"
#include "config.h"

static bool g_meter = false;

static float toDb(float lin) { return lin > 1e-6f ? 20.0f * log10f(lin) : -120.0f; }

void printHelp() {
  Serial.printf(
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
      "  rv           toggle reverb (now %s)\n"
      "  mix <0-100>  reverb dry/wet, 0 = dry only (now %u)\n"
      "  size <0-100> reverb time (now %u)\n"
      "  tone <0-100> reverb brightness (now %u)\n"
      "  in <0-15>    line-in sensitivity, 0 = 3.12 Vpp (least sensitive)\n"
      "  out <13-31>  line-out level, 13 = loudest\n"
      "  hp <0-100>   headphone volume %%\n"
      "  m            toggle live level meter\n"
      "  s            status (CPU, levels, latency)\n"
      "  r            rescan SD card\n",
      g_settings.outGainDb, g_settings.delayOn ? "on" : "off", g_settings.delayMix, DELAY_MAX_MS,
      g_settings.delayTimeMs, g_settings.delayFeedback, g_settings.delayTone, g_settings.delayMod,
      delayTypeName(g_settings.delayType), g_settings.delayDuck, g_settings.delayDrive,
      g_settings.delayEra, g_settings.reverbOn ? "on" : "off", g_settings.reverbMix,
      g_settings.reverbSize, g_settings.reverbTone);
}

static void printList() {
  if (!irSdOk()) { Serial.println("No SD card."); return; }
  for (int i = 0; i < irCount(); i++)
    Serial.printf("%c %3d  %s\n", i == irCurrent() ? '>' : ' ', i + 1, irPath(i));
  if (irCount() == 0) Serial.println("No .wav files in / or /IR");
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
  Serial.printf("Reverb:    %s, mix %u%%, size %u%%, tone %u%%\n",
                g_settings.reverbOn ? "on" : (convolver.reverbActive() ? "off (tail ringing)" : "off"),
                g_settings.reverbMix, g_settings.reverbSize, g_settings.reverbTone);
  Serial.printf("CPU:       IR + effects %.1f%% avg / %.1f%% max (delay %.1f%%, reverb %.1f%%), audio total %.1f%% max\n",
                convolver.cpuPercent(), convolver.cpuPercentMax(), convolver.delayCpuPercent(),
                convolver.reverbCpuPercent(), AudioProcessorUsageMax());
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
  } else if (!strcmp(line, "rv")) {
    setReverbOn(!g_settings.reverbOn);
  } else if (!strcmp(line, "mix") && arg) {
    setReverbMix(atoi(arg));
  } else if (!strcmp(line, "size") && arg) {
    setReverbSize(atoi(arg));
  } else if (!strcmp(line, "tone") && arg) {
    setReverbTone(atoi(arg));
  } else if (!strcmp(line, "g") && arg) {
    setOutputGainDb((float)atof(arg));
  } else if (!strcmp(line, "in") && arg) {
    setLineInLevel(atoi(arg));
  } else if (!strcmp(line, "out") && arg) {
    setLineOutLevel(atoi(arg));
  } else if (!strcmp(line, "hp") && arg) {
    setHeadphoneVolume(atoi(arg));
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
