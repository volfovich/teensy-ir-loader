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
#include "Params.h"
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
  int get(uint8_t id) override {
    switch (id) {
      case ui::kIsReverbHall: return getParam(ui::kReverbMode) == 0;
      case ui::kIsReverbGated: return getParam(ui::kReverbMode) == 1;
      case ui::kIsDrumSong: return getParam(ui::kDrumSong) != 0;
      default: return id < ui::kValueCount ? getParam(id) : 0;
    }
  }
  void set(uint8_t id, int v) override;
  void limits(uint8_t id, int* lo, int* hi) override {
    if (id < ui::kValueCount) paramLimits(id, lo, hi);
  }
  bool valueText(uint8_t id, char* buf, int n) override;
};

void FirmwareHost::set(uint8_t id, int v) {
  switch (id) {
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
    default:
      if (id < ui::kValueCount) setParam(id, v);
  }
}

// Names the table can't know (styles, kits, songs), and the IR screen's row.
bool FirmwareHost::valueText(uint8_t id, char* buf, int n) {
  switch (id) {
    case ui::kScreenIR:
      if (!irSdOk()) snprintf(buf, n, "no SD");
      else if (irCurrent() < 0) snprintf(buf, n, "-/%d", irCount());
      else snprintf(buf, n, "%d/%d", irCurrent() + 1, irCount());
      return true;
    case ui::kDrumStyle:
    case ui::kDrumKit:
    case ui::kDrumSong:
      paramText(id, buf, n);
      return true;
    case ui::kDrumLoopFrom:
      if (getParam(id)) return false;
      paramText(id, buf, n);  // "off"
      return true;
    default: return false;
  }
}

static FirmwareHost g_host;
static ui::Panel g_panel(ui::menuPages(), g_host, g_model);

// ---- The model the screens are drawn from -------------------------------------------

static void fillModel(uint32_t now) {
  ui::UiModel& m = g_model;
  m.nowMs = now;

  m.sdOk = irSdOk();
  m.irCount = irCount();
  m.irCurrent = irCurrent();
  m.irPath = irPath;

  m.bypass = getParam(ui::kBypass);
  m.delayOn = getParam(ui::kDelayOn);
  m.reverbOn = getParam(ui::kReverbOn);
  const irdsp::DrumMachine& dm = convolver.drums();
  m.drumsPlaying = dm.playing();
  const irdsp::DrumSong* song = currentDrumSong();
  m.drumStyle = song ? song->name : irdsp::drumStyle(getParam(ui::kDrumStyle)).name;
  m.drumTempo = m.drumsPlaying ? (int)lroundf(dm.tempoNow()) : getParam(ui::kDrumTempo);
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
  m.lineIn = getParam(ui::kLineIn);

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
