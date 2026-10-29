#include "DisplayUI.h"

#include <Arduino.h>
#include <Audio.h>

#include "AudioGraph.h"
#include "Canvas.h"
#include "Controls.h"
#include "DrumKit.h"
#include "DrumMachine.h"
#include "DrumSongs.h"
#include "IRLibrary.h"
#include "Menu.h"
#include "Oled.h"
#include "Panel.h"
#include "Settings.h"
#include "config.h"

// 1: print the control events on Serial (to check the encoder's direction
// and counts per detent, see config.h).
#define CONTROLS_DEBUG 0

static constexpr uint32_t kFrameMs = 40;      // redraw rate (25 Hz) when nothing happens
static constexpr uint32_t kSplashMs = 1500;
static constexpr uint32_t kStatusMs = 1000;   // the Status screen's refresh

// What the canvas draws into, and a copy of what the OLED shows: only the
// pages that differ are sent. Both in RAM2.
static uint8_t DMAMEM g_fb[ui::Canvas::kW * ui::Canvas::kPages];
static uint8_t DMAMEM g_shown[ui::Canvas::kW * ui::Canvas::kPages];
static ui::Canvas g_canvas(g_fb);
static ui::UiModel g_model;
static uint32_t g_frameAt = 0, g_statusAt = 0, g_splashUntil = 0, g_clips = 0, g_clipBase = 0;
static int g_nextPage = 0;

// ---- Settings, for the menu --------------------------------------------------------

class FirmwareHost final : public ui::MenuHost {
 public:
  int get(uint8_t id) override;
  void set(uint8_t id, int value) override;
  void limits(uint8_t id, int* lo, int* hi) override;
  bool valueText(uint8_t id, char* buf, int n) override;
};

static int songBars() {
  const irdsp::DrumSong* s = currentDrumSong();
  return s ? s->barCount : 1;
}

int FirmwareHost::get(uint8_t id) {
  const Settings& s = g_settings;
  switch (id) {
    case ui::kDelayOn: return s.delayOn;
    case ui::kDelayMix: return s.delayMix;
    case ui::kDelayTime: return s.delayTimeMs;
    case ui::kDelayRepeats: return s.delayFeedback;
    case ui::kDelayTone: return s.delayTone;
    case ui::kDelayMod: return s.delayMod;
    case ui::kDelayDuck: return s.delayDuck;
    case ui::kDelayDrive: return s.delayDrive;
    case ui::kDelayEra: return s.delayEra;
    case ui::kReverbOn: return s.reverbOn;
    case ui::kReverbMix: return s.reverbMix;
    case ui::kReverbSize: return s.reverbSize;
    case ui::kReverbTone: return s.reverbTone;
    case ui::kReverbMode: return s.reverbMode;
    case ui::kReverbPreDelay: return s.reverbPreDelay;
    case ui::kReverbMod: return s.reverbMod;
    case ui::kReverbEra: return s.reverbEra;
    case ui::kReverbGate: return s.reverbGateMs;
    case ui::kOutGain: return (int)lroundf(s.outGainDb * 2);
    case ui::kLineIn: return s.lineIn;
    case ui::kLineOut: return s.lineOut;
    case ui::kHeadphones: return (int)lroundf(s.hpVolume * 100);
    case ui::kBypass: return s.bypass;
    case ui::kDrumsPlay: return convolver.drums().playing();
    case ui::kDrumStyle: return s.drumStyle;
    case ui::kDrumKit: return s.drumKit;
    case ui::kDrumSong: return s.drumSong;
    case ui::kDrumLoopFrom: return drumLoopFirst();
    case ui::kDrumLoopTo: return drumLoopFirst() ? drumLoopLast() : songBars();
    case ui::kDrumCrash: return s.drumCrash >= 16 ? 4 : (s.drumCrash >= 8 ? 3 : (s.drumCrash >= 4 ? 2 : (s.drumCrash ? 1 : 0)));
    case ui::kDrumTempo: return s.drumTempo;
    case ui::kDrumVolume: return s.drumVolume;
    case ui::kDrumComp: return s.drumComp;
    case ui::kDrumCountIn: return s.drumCountIn;
    case ui::kTrainerOn: return s.trainerOn;
    case ui::kTrainerStep: return s.trainerStep;
    case ui::kTrainerEvery: return s.trainerEvery;
    case ui::kTrainerTarget: return s.trainerTarget;
    case ui::kBassOn: return s.bassOn;
    case ui::kBassVolume: return s.bassVolume;
    case ui::kBassKey: return s.bassKey;
    case ui::kBassTone: return s.bassTone;
    case ui::kIsReverbHall: return s.reverbMode == 0;
    case ui::kIsReverbGated: return s.reverbMode == 1;
    case ui::kIsDrumSong: return s.drumSong != 0;
    default: return 0;
  }
}

void FirmwareHost::set(uint8_t id, int v) {
  const Settings& s = g_settings;
  switch (id) {
    case ui::kDelayOn: setDelayOn(v); break;
    case ui::kDelayMix: setDelayMix(v); break;
    case ui::kDelayTime: setDelayTimeMs(v); break;
    case ui::kDelayRepeats: setDelayFeedback(v); break;
    case ui::kDelayTone: setDelayTone(v); break;
    case ui::kDelayMod: setDelayMod(v); break;
    case ui::kDelayDuck: setDelayDuck(v); break;
    case ui::kDelayDrive: setDelayDrive(v); break;
    case ui::kDelayEra: setDelayEra(v); break;
    case ui::kReverbOn: setReverbOn(v); break;
    case ui::kReverbMix: setReverbMix(v); break;
    case ui::kReverbSize: setReverbSize(v); break;
    case ui::kReverbTone: setReverbTone(v); break;
    case ui::kReverbMode: setReverbMode(v); break;
    case ui::kReverbPreDelay: setReverbPreDelay(v); break;
    case ui::kReverbMod: setReverbMod(v); break;
    case ui::kReverbEra: setReverbEra(v); break;
    case ui::kReverbGate: setReverbGate(v); break;
    case ui::kOutGain: setOutputGainDb(v * 0.5f); break;
    case ui::kLineIn: setLineInLevel(v); break;
    case ui::kLineOut: setLineOutLevel(v); break;
    case ui::kHeadphones: setHeadphoneVolume(v); break;
    case ui::kBypass: setBypass(v); break;
    case ui::kDrumsPlay: setDrumsPlaying(v); break;
    case ui::kDrumStyle: setDrumStyle(v); break;
    case ui::kDrumKit: setDrumKit(v); break;
    case ui::kDrumSong: selectDrumSong(v); break;
    case ui::kDrumLoopFrom:
      if (!v) setDrumLoop(0, 0);
      else setDrumLoop(v, max(v, drumLoopFirst() ? drumLoopLast() : songBars()));
      break;
    case ui::kDrumLoopTo: setDrumLoop(drumLoopFirst() ? drumLoopFirst() : 1, v); break;
    case ui::kDrumCrash: setDrumCrash(v ? 1 << v : 0); break;  // 2, 4, 8, 16 bars
    case ui::kDrumTempo: setDrumTempo(v); break;
    case ui::kDrumVolume: setDrumVolume(v); break;
    case ui::kDrumComp: setDrumComp(v); break;
    case ui::kDrumCountIn: setDrumCountIn(v); break;
    case ui::kTrainerOn: setDrumTrainer(v, s.trainerStep, s.trainerEvery, s.trainerTarget); break;
    case ui::kTrainerStep: setDrumTrainer(s.trainerOn, v, s.trainerEvery, s.trainerTarget); break;
    case ui::kTrainerEvery: setDrumTrainer(s.trainerOn, s.trainerStep, v, s.trainerTarget); break;
    case ui::kTrainerTarget: setDrumTrainer(s.trainerOn, s.trainerStep, s.trainerEvery, v); break;
    case ui::kBassOn: setBassOn(v); break;
    case ui::kBassVolume: setBassVolume(v); break;
    case ui::kBassKey: setBassKey(v); break;
    case ui::kBassTone: setBassTone(v); break;
    case ui::kIrLoad: loadIRIndex(v); break;
    case ui::kIrRescan:
      scanCard();
      loadStartupIR();
      break;
    case ui::kMeterReset:
      g_model.in.reset();
      g_model.out.reset();
      g_clipBase = convolver.clipCount();
      break;
    default: break;
  }
}

void FirmwareHost::limits(uint8_t id, int* lo, int* hi) {
  (void)lo;
  switch (id) {
    case ui::kDelayTime: *hi = DELAY_MAX_MS; break;
    case ui::kDrumStyle: *hi = irdsp::drumStyleCount() - 1; break;
    case ui::kDrumKit: *hi = irdsp::drumKitCount() - 1; break;
    case ui::kDrumSong: *hi = drumSongTotal(); break;
    case ui::kDrumLoopFrom:
    case ui::kDrumLoopTo: *hi = songBars(); break;
    default: break;
  }
}

bool FirmwareHost::valueText(uint8_t id, char* buf, int n) {
  switch (id) {
    case ui::kScreenIR:
      if (!irSdOk()) snprintf(buf, n, "no SD");
      else if (irCurrent() < 0) snprintf(buf, n, "-/%d", irCount());
      else snprintf(buf, n, "%d/%d", irCurrent() + 1, irCount());
      return true;
    case ui::kDrumStyle: snprintf(buf, n, "%s", irdsp::drumStyle(g_settings.drumStyle).title); return true;
    case ui::kDrumKit: snprintf(buf, n, "%s", irdsp::drumKit(g_settings.drumKit).name); return true;
    case ui::kBassKey: snprintf(buf, n, "%s", bassKeyName(g_settings.bassKey)); return true;
    case ui::kDrumSong:
      snprintf(buf, n, "%s", g_settings.drumSong ? drumSongTitle(g_settings.drumSong) : "off");
      return true;
    case ui::kDrumLoopFrom:
      if (drumLoopFirst()) return false;
      snprintf(buf, n, "off");
      return true;
    default: return false;
  }
}

static FirmwareHost g_host;
static ui::Panel g_panel(ui::menuPages(), g_host, g_model);

// ---- The model the screens are drawn from -------------------------------------------

static void fillModel(uint32_t now) {
  const Settings& s = g_settings;
  ui::UiModel& m = g_model;
  m.nowMs = now;

  m.sdOk = irSdOk();
  m.irCount = irCount();
  m.irCurrent = irCurrent();
  m.irPath = irPath;

  m.bypass = s.bypass;
  m.delayOn = s.delayOn;
  m.reverbOn = s.reverbOn;
  const irdsp::DrumMachine& dm = convolver.drums();
  m.drumsPlaying = dm.playing();
  const irdsp::DrumSong* song = currentDrumSong();
  m.drumStyle = song ? song->name : irdsp::drumStyle(s.drumStyle).name;
  m.drumTempo = m.drumsPlaying ? (int)lroundf(dm.tempoNow()) : s.drumTempo;
  dm.beatPos(&m.drumBeat, &m.drumBeats);

  // Meters: CLIP at the serial meter's thresholds (in > -0.5 dBFS, out >
  // -0.1 dBFS), or when the clip counter moved since the last frame.
  float in, out;
  convolver.takeDisplayPeaks(in, out);
  const uint32_t clips = convolver.clipCount();
  m.in.update(in, in > 0.944f, now);
  m.out.update(out, out > 0.9886f || clips != g_clips, now);
  g_clips = clips;
  m.clips = clips - g_clipBase;
  m.lineIn = s.lineIn;

  // The Status screen, once a second; it resets the maxima like `s` does.
  if (g_panel.screen() != ui::Panel::kStatus) {
    g_statusAt = now - kStatusMs;  // at once when it opens
  } else if (now - g_statusAt >= kStatusMs) {
    g_statusAt = now;
    m.cpu = convolver.cpuPercent();
    m.cpuMax = convolver.cpuPercentMax();
    m.blocksUsed = AudioMemoryUsageMax();
    m.blocksMax = AUDIO_MEMORY_BLOCKS;
    convolver.resetCpuMax();
    AudioProcessorUsageMaxReset();
  }
}

// ---- Sending ---------------------------------------------------------------------

// Sends the next page that differs from what the OLED shows (round-robin,
// so a busy meter row can't starve the others). False if all match.
static bool sendOnePage() {
  for (int i = 0; i < ui::Canvas::kPages; i++) {
    const int p = (g_nextPage + i) % ui::Canvas::kPages;
    uint8_t* shown = g_shown + p * ui::Canvas::kW;
    if (!memcmp(g_canvas.page(p), shown, ui::Canvas::kW)) continue;
    oledSendPage(p, g_canvas.page(p));
    memcpy(shown, g_canvas.page(p), ui::Canvas::kW);
    g_nextPage = (p + 1) % ui::Canvas::kPages;
    return true;
  }
  return false;
}

void displayBegin() {
  if (!oledBegin()) {
    Serial.println("No OLED found (I2C 0x3C / 0x3D).");
    return;
  }
  memset(g_shown, 0, sizeof(g_shown));  // oledBegin() blanked the display
  const uint32_t now = millis();
  fillModel(now);
  ui::drawSplash(g_canvas, g_model);
  while (sendOnePage()) {}
  g_splashUntil = now + kSplashMs;
}

#if CONTROLS_DEBUG
static void printEvents(const ControlEvents& e) {
  static const char* const kNames[] = {"", "press", "release", "hold"};
  if (e.detents) Serial.printf("turn %+d\n", e.detents);
  if (e.push) Serial.printf("push %s\n", kNames[e.push]);
  if (e.confirm) Serial.printf("confirm %s\n", kNames[e.confirm]);
  if (e.back) Serial.printf("back %s\n", kNames[e.back]);
}
#endif

void serviceDisplay() {
  if (!oledPresent()) return;
  const uint32_t now = millis();
  const ControlEvents e = readControls(now);
#if CONTROLS_DEBUG
  printEvents(e);
#endif
  if ((int32_t)(g_splashUntil - now) > 0) return;  // input is ignored meanwhile
  // Short pushes act on release, so a hold can be told apart; CONFIRM and
  // BACK have no hold and act on press.
  bool redraw = false;
  if (e.detents) redraw |= g_panel.turn(e.detents, now);
  if (e.push == ui::Key::kRelease) redraw |= g_panel.push(now);
  if (e.push == ui::Key::kHold) redraw |= g_panel.hold(now);
  if (e.confirm == ui::Key::kPress) redraw |= g_panel.confirm(now);
  if (e.back == ui::Key::kPress) redraw |= g_panel.back(now);
  redraw |= g_panel.tick(now);
  if (redraw || now - g_frameAt >= kFrameMs) {
    g_frameAt = now;
    fillModel(now);
    g_panel.draw(g_canvas, g_model);
  }
  sendOnePage();
}
