// Host-side tests for the OLED UI library (no hardware needed).
//   make -C test/host && ./test/host/test_ui
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Canvas.h"
#include "Font5x7.h"
#include "Key.h"
#include "Menu.h"
#include "Meter.h"
#include "Quadrature.h"
#include "Screens.h"
#include "ui_fake.h"

static int failures = 0;

static void check(bool ok, const char* what, double value) {
  std::printf("  %-52s %10.4g  %s\n", what, value, ok ? "OK" : "FAIL");
  if (!ok) failures++;
}

static void testQuadrature() {
  std::printf("Quadrature\n");
  const uint8_t cw[] = {0, 1, 3, 2, 0, 1, 3, 2, 0};
  int n = 0;
  for (int i = 1; i < 9; i++) n += ui::quadStep(cw[i - 1], cw[i]);
  check(n == 8, "two detents one way (8 quarter steps)", n);
  n = 0;
  for (int i = 8; i > 0; i--) n += ui::quadStep(cw[i], cw[i - 1]);
  check(n == -8, "  ... and back", n);
  // Contact bounce on A: 00 -> 10 -> 00 -> 10 ... nets nothing extra.
  const uint8_t bounce[] = {0, 2, 0, 2, 0, 2, 3};
  n = 0;
  for (int i = 1; i < 7; i++) n += ui::quadStep(bounce[i - 1], bounce[i]);
  check(n == -2, "bounce on one pin cancels out (net steps)", n);
  check(ui::quadStep(0, 3) == 0 && ui::quadStep(1, 2) == 0, "both pins changed (missed edge) counts 0", 0);
}

static void testKey() {
  std::printf("Key\n");
  ui::Key k;
  int press = 0, rel = 0, hold = 0;
  auto feed = [&](bool down, uint32_t t0, uint32_t t1) {
    for (uint32_t t = t0; t < t1; t++) {
      auto e = k.update(down, t);
      press += e == ui::Key::kPress;
      rel += e == ui::Key::kRelease;
      hold += e == ui::Key::kHold;
    }
  };
  feed(true, 100, 108);  // 8 ms glitch
  feed(false, 108, 200);
  check(press == 0 && rel == 0, "8 ms glitch ignored", press + rel);
  feed(true, 200, 400);
  feed(false, 400, 500);
  check(press == 1 && rel == 1 && hold == 0, "200 ms press: press + release, no hold", press * 100 + rel * 10 + hold);
  feed(true, 1000, 2000);
  check(hold == 1, "1 s press: one hold", hold);
  feed(false, 2000, 2100);
  check(rel == 1 && press == 2, "  ... no release event after a hold", rel);
}

static void testMenu() {
  std::printf("Menu\n");
  FakeHost h;
  ui::Menu m(ui::menuPages(), h);
  m.open();
  check(m.turn(1, 0) == ui::Menu::kRedraw && m.current()->id == ui::kDelayOn, "turn moves to Delay", m.current()->id);
  m.push();
  check(m.pageIndex() == 1, "push enters the delay page", m.pageIndex());
  m.turn(3, 0);
  check(m.current()->id == ui::kDelayTime, "cursor on Time", m.current()->id);
  m.push();
  check(m.editing(), "push starts editing", m.editing());
  m.turn(1, 1000);
  m.turn(1, 2000);
  check(h.v[ui::kDelayTime] == 390, "two slow detents: +2 x 5 ms (applied live)", h.v[ui::kDelayTime]);
  m.turn(1, 2020);
  check(h.v[ui::kDelayTime] == 415, "a fast detent counts x5", h.v[ui::kDelayTime]);
  m.back();
  check(!m.editing() && h.v[ui::kDelayTime] == 380, "back cancels: value restored", h.v[ui::kDelayTime]);
  m.push();
  m.turn(-1000, 5000);
  check(h.v[ui::kDelayTime] == 20, "clamped at the minimum", h.v[ui::kDelayTime]);
  h.timeMax = 600;
  m.turn(1000, 9000);
  check(h.v[ui::kDelayTime] == 600, "maximum from MenuHost::limits", h.v[ui::kDelayTime]);
  m.push();
  check(!m.editing() && h.v[ui::kDelayTime] == 600, "push keeps the value", h.v[ui::kDelayTime]);

  m.turn(-10, 0);
  m.push();
  check(h.v[ui::kDelayOn] == 0, "push on a toggle flips it", h.v[ui::kDelayOn]);
  m.turn(1, 0);
  m.push();
  check(h.v[ui::kDelayType] == 0, "push on Type steps the choice (80s -> ping-pong)", h.v[ui::kDelayType]);
  m.turn(100, 0);
  check(m.current()->id == ui::kDelayMod, "ping-pong: Duck/Drive/Era hidden, last row Mod", m.current()->id);
  h.v[ui::kDelayType] = 1;
  m.turn(100, 0);
  check(m.current()->id == ui::kDelayEra, "studio 80s: last row Era", m.current()->id);
  h.v[ui::kDelayType] = 0;
  check(m.current()->id == ui::kDelayMod, "type changed elsewhere: cursor clamps to Mod", m.current()->id);

  check(m.back() == ui::Menu::kRedraw && m.pageIndex() == 0, "back to the top level", m.pageIndex());
  m.turn(100, 0);
  check(m.push() == ui::Menu::kOpenScreen && m.screen() == ui::kScreenStatus, "push on Status opens the screen", m.screen());
  check(m.back() == ui::Menu::kExit, "back at the top: exit", 0);
}

static void testReverbPage() {
  std::printf("Reverb page\n");
  FakeHost h;
  ui::Menu m(ui::menuPages(), h);
  m.open();
  m.turn(2, 0);
  m.push();
  m.turn(100, 0);
  check(m.current()->id == ui::kReverbEra, "80s hall: last row Era (Mod shown, Gate hidden)", m.current()->id);
  m.turn(-1, 0);
  check(m.current()->id == ui::kReverbMod, "  ... Mod above it", m.current()->id);
  h.v[ui::kReverbMode] = 1;
  m.turn(100, 0);
  check(m.current()->id == ui::kReverbGate, "80s gated: last row Gate", m.current()->id);
  m.push();
  m.turn(2, 1000);
  m.push();
  check(h.v[ui::kReverbGate] == 320, "  ... Gate edits in 10 ms steps", h.v[ui::kReverbGate]);
  h.v[ui::kReverbType] = 0;
  m.turn(100, 0);
  check(m.current()->id == ui::kReverbTone, "plate: last row Tone (no 80s rows)", m.current()->id);
  m.turn(-100, 0);
  m.turn(1, 0);
  m.push();
  check(h.v[ui::kReverbType] == 1 && m.current()->id == ui::kReverbType, "push on Type: plate -> 80s", h.v[ui::kReverbType]);
}

static void testSongRows() {
  std::printf("Drums page: songs\n");
  FakeHost h;
  ui::Menu m(ui::menuPages(), h);
  m.open();
  m.turn(3, 0);
  m.push();
  m.turn(2, 0);
  check(m.current()->id == ui::kDrumSong, "Song after Style", m.current()->id);
  m.turn(1, 0);
  check(m.current()->id == ui::kDrumKit, "  ... no song: the Loop rows are hidden", m.current()->id);
  m.turn(-1, 0);
  m.push();
  m.turn(4, 1000);
  m.push();
  check(h.v[ui::kDrumSong] == 4, "  ... pick song 4 by turning", h.v[ui::kDrumSong]);
  m.turn(1, 0);
  check(m.current()->id == ui::kDrumLoopFrom, "  ... with a song: Loop from, Loop to", m.current()->id);
  char b[24];
  h.valueText(ui::kDrumLoopFrom, b, sizeof(b));
  check(!std::strcmp(b, "off"), "  ... Loop from 0 reads off", 0);
  m.push();
  m.turn(9, 1000);
  m.push();
  m.turn(1, 0);
  m.push();
  m.turn(-16, 2000);
  m.push();
  check(h.v[ui::kDrumLoopFrom] == 9 && h.v[ui::kDrumLoopTo] == 16, "  ... loop bars 9-16", h.v[ui::kDrumLoopTo]);
}

static void testDrumsPage() {
  std::printf("Drums page\n");
  FakeHost h;
  ui::Menu m(ui::menuPages(), h);
  m.open();
  m.turn(3, 0);
  check(m.current()->id == ui::kDrumsPlay, "Drums on the top level after Reverb", m.current()->id);
  m.push();
  m.turn(100, 0);
  check(m.current()->id == ui::kTrainerOn && m.current()->kind == ui::Kind::kSubmenu,
        "last Drums row: Trainer (a page)", m.current()->id);
  m.push();
  const int trainerPage = m.pageIndex();
  m.turn(100, 0);
  check(m.current()->id == ui::kTrainerTarget, "Trainer page: Step/Every/Target shown while off", m.current()->id);
  m.turn(-100, 0);
  m.push();
  check(h.v[ui::kTrainerOn] == 1, "  ... On toggles the trainer", h.v[ui::kTrainerOn]);
  m.turn(3, 0);
  m.push();
  m.turn(5, 1000);
  m.push();
  check(h.v[ui::kTrainerTarget] == 145, "  ... Target edits (140 -> 145 BPM)", h.v[ui::kTrainerTarget]);
  m.back();
  check(m.pageIndex() != trainerPage && m.current()->id == ui::kTrainerOn, "back returns to the Drums page", m.pageIndex());
  m.turn(-100, 0);
  m.turn(1, 0);
  m.push();
  m.turn(2, 1000);
  check(h.v[ui::kDrumStyle] == 3, "style edits by turning (rock -> offbeat 16ths)", h.v[ui::kDrumStyle]);
  m.turn(100, 2000);
  check(h.v[ui::kDrumStyle] == 7, "  ... up to the last style (MenuHost::limits)", h.v[ui::kDrumStyle]);
  m.back();
  check(h.v[ui::kDrumStyle] == 1, "  ... back restores rock", h.v[ui::kDrumStyle]);
  m.turn(2, 0);
  check(m.current()->id == ui::kDrumKit, "Kit after Style and Song", m.current()->id);
  m.push();
  m.turn(5, 1000);
  check(h.v[ui::kDrumKit] == 1, "  ... kit range from MenuHost::limits (2 kits)", h.v[ui::kDrumKit]);
  m.turn(-2, 2000);
  m.push();
  check(h.v[ui::kDrumKit] == 0, "  ... rock -> synth", h.v[ui::kDrumKit]);
  m.turn(1, 0);
  m.push();
  check(m.current()->id == ui::kDrumCrash && h.v[ui::kDrumCrash] == 3, "Crash: push steps 4 bars -> 8 bars", h.v[ui::kDrumCrash]);
  m.push();
  m.push();
  check(h.v[ui::kDrumCrash] == 0, "  ... 16 bars, then off", h.v[ui::kDrumCrash]);
  m.turn(3, 0);
  check(m.current()->id == ui::kDrumComp, "Comp after Volume", m.current()->id);
  m.turn(1, 0);
  m.push();
  check(m.current()->id == ui::kDrumCountIn && h.v[ui::kDrumCountIn] == 3, "Count-in: push steps 2 -> 3 bars", h.v[ui::kDrumCountIn]);
  m.push();
  m.push();
  char b2[16];
  ui::formatValue(*m.current(), h.v[ui::kDrumCountIn], false, b2, sizeof(b2));
  check(h.v[ui::kDrumCountIn] == 0 && !std::strcmp(b2, "off"), "  ... 4 bars, then off", h.v[ui::kDrumCountIn]);
  const ui::MenuItem bars{"e", ui::Kind::kValue, ui::kTrainerEvery, 0, 1, 16, 1, ui::Unit::kBars, 0, nullptr};
  char b[16], b1[16];
  ui::formatValue(bars, 4, false, b, sizeof(b));
  ui::formatValue(bars, 1, false, b1, sizeof(b1));
  check(!std::strcmp(b, "4 bars") && !std::strcmp(b1, "1 bar"), "bars formatting (4 bars, 1 bar)", 0);
}

static void testFormat() {
  std::printf("Value formatting\n");
  const ui::MenuItem gain{"g", ui::Kind::kValue, ui::kOutGain, 0, -120, 48, 1, ui::Unit::kHalfDb, 0, nullptr};
  const ui::MenuItem time{"t", ui::Kind::kValue, ui::kDelayTime, 0, 20, 750, 5, ui::Unit::kMs, 0, nullptr};
  char b[32];
  bool ok = true;
  ui::formatValue(gain, -12, false, b, sizeof(b)); ok = ok && !std::strcmp(b, "-6.0dB");
  ui::formatValue(gain, 7, true, b, sizeof(b)); ok = ok && !std::strcmp(b, "+3.5 dB");
  ui::formatValue(gain, -1, false, b, sizeof(b)); ok = ok && !std::strcmp(b, "-0.5dB");
  ui::formatValue(gain, 0, false, b, sizeof(b)); ok = ok && !std::strcmp(b, "0.0dB");
  ui::formatValue(time, 380, true, b, sizeof(b)); ok = ok && !std::strcmp(b, "380 ms");
  check(ok, "half dB and ms (-6.0dB, +3.5 dB, -0.5dB, 0.0dB, 380 ms)", ok);
  char n[32];
  ui::irDisplayName("/IR/OH Mdrn-Prog.wav", n, sizeof(n));
  check(!std::strcmp(n, "OH Mdrn-Prog"), "IR name without folder and .wav", 0);
}

static void testMeter() {
  std::printf("Meter ballistics\n");
  ui::MeterState s;
  s.update(1.0f, false, 1000);
  check(std::fabs(s.levelDb) < 1e-4, "full scale reads 0 dB at once", s.levelDb);
  s.update(0.0f, false, 1100);
  check(std::fabs(s.levelDb + 2.0) < 1e-3, "falls 20 dB/s (after 100 ms)", s.levelDb);
  s.update(0.0f, false, 2400);
  check(s.peakDb == 0.0f, "peak held at 1.4 s", s.peakDb);
  s.update(0.0f, false, 2600);
  check(s.peakDb < 0.0f && s.peakDb >= s.levelDb, "  ... falling after 1.5 s, never below the bar", s.peakDb);
  s.update(0.5f, true, 3000);
  check(s.clipping(4900) && !s.clipping(5001), "CLIP latched for 2 s", 0);
}

static void testCanvas() {
  std::printf("Canvas\n");
  uint8_t buf[1024];
  ui::Canvas c(buf);
  c.clear();
  while (c.takeDirtyPage() >= 0) {}
  c.text(0, 2, "A");
  const uint8_t* g = ui::glyph('A');
  check(!std::memcmp(c.page(2), g, 5) && c.page(2)[5] == 0, "small glyph bytes land in its page", 0);
  check(c.dirtyMask() == 0x04, "only that page is dirty", c.dirtyMask());
  c.text(0, 4, "A", true);
  check(c.dirtyMask() == 0x34, "2x text marks two pages", c.dirtyMask());
  check(c.takeDirtyPage() == 2 && c.takeDirtyPage() == 4 && c.takeDirtyPage() == 5 && c.takeDirtyPage() == -1,
        "dirty pages come out lowest first", 0);
  // Right-aligned text ends with ink in the column before xEnd.
  c.clear();
  c.textRight(128, 0, "I");
  check(c.get(126, 0) && !c.get(127, 0), "right-aligned 'I' ends at column 126", 0);
  check(ui::Canvas::width("abc") == 18 && ui::Canvas::width("abc", true) == 36, "text widths 6 / 12 px", 0);

  // drawHome leaves the meter rows alone; drawHomeMeters only touches them.
  ui::UiModel m = fakeModel();
  c.clear();
  while (c.takeDirtyPage() >= 0) {}
  ui::drawHome(c, m, false);
  check(c.dirtyMask() == 0x3F, "home: pages 0-5 dirty", c.dirtyMask());
  while (c.takeDirtyPage() >= 0) {}
  ui::drawHomeMeters(c, m);
  check(c.dirtyMask() == 0xC0, "home meters: pages 6-7 dirty", c.dirtyMask());
}

int main() {
  testQuadrature();
  testKey();
  testMenu();
  testReverbPage();
  testDrumsPage();
  testSongRows();
  testFormat();
  testMeter();
  testCanvas();
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
