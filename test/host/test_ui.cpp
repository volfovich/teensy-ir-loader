// Host-side tests for the OLED UI library (no hardware needed).
//   make -C test/host && ./test/host/test_ui
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "Canvas.h"
#include "Fonts.h"
#include "Key.h"
#include "Menu.h"
#include "Meter.h"
#include "Panel.h"
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
  m.turn(2, 0);
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
  m.turn(100, 0);
  check(m.current()->id == ui::kDelayEra, "last row Era", m.current()->id);

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
  check(m.current()->id == ui::kReverbEra, "hall: last row Era (Mod shown, Gate hidden)", m.current()->id);
  m.turn(-1, 0);
  check(m.current()->id == ui::kReverbMod, "  ... Mod above it", m.current()->id);
  h.v[ui::kReverbMode] = 1;
  m.turn(100, 0);
  check(m.current()->id == ui::kReverbGate, "gated: last row Gate", m.current()->id);
  m.push();
  m.turn(2, 1000);
  m.push();
  check(h.v[ui::kReverbGate] == 320, "  ... Gate edits in 10 ms steps", h.v[ui::kReverbGate]);
  m.turn(-100, 0);
  m.turn(1, 0);
  m.push();
  check(h.v[ui::kReverbMode] == 0 && m.current()->id == ui::kReverbMode, "push on Mode: gated -> hall", h.v[ui::kReverbMode]);
}

static void testSongRows() {
  std::printf("Drums page: songs\n");
  FakeHost h;
  ui::Menu m(ui::menuPages(), h);
  m.open();
  m.turn(3, 0);
  m.push();
  m.turn(3, 0);
  check(m.current()->id == ui::kDrumSong, "Song after Bass and Style", m.current()->id);
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
  check(m.current()->id == ui::kBassOn && m.current()->kind == ui::Kind::kSubmenu, "Bass right after Play (a page)",
        m.current()->id);
  m.push();
  check(m.pageIndex() == ui::kPageBass && m.current()->id == ui::kBassOn, "  ... its first row: On", m.pageIndex());
  m.push();
  check(h.v[ui::kBassOn] == 0, "  ... push turns the bass off", h.v[ui::kBassOn]);
  m.push();
  m.turn(2, 0);
  m.push();
  m.turn(5, 1000);
  char kb[8];
  h.valueText(ui::kBassKey, kb, sizeof(kb));
  check(h.v[ui::kBassOn] == 1 && h.v[ui::kBassKey] == 9 && !std::strcmp(kb, "A"), "  ... on again; key E -> A by name",
        h.v[ui::kBassKey]);
  m.push();
  m.back();
  check(m.pageIndex() == ui::kPageDrums && m.current()->id == ui::kBassOn, "  ... back to the Drums page", m.pageIndex());
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
  const uint8_t* g = ui::kFont8x16['A' - 32];
  check(!std::memcmp(c.page(2), g, 8) && !std::memcmp(c.page(3), g + 8, 8), "8x16 glyph bytes land in pages 2-3", 0);
  check(c.dirtyMask() == 0x0C, "only those pages are dirty", c.dirtyMask());
  c.text(0, 4, "A", ui::Font::kBig);
  check(c.dirtyMask() == 0xFC, "16x32 text marks four pages", c.dirtyMask());
  check(c.takeDirtyPage() == 2 && c.takeDirtyPage() == 3 && c.takeDirtyPage() == 4, "dirty pages come out lowest first", 0);
  c.text(0, 7, "A");
  check(c.page(7)[1] == 0, "a row that would run off the bottom is not drawn", 0);
  check(ui::Canvas::width("abc") == 24 && ui::Canvas::width("abc", ui::Font::kBig) == 48, "text widths 8 / 16 px", 0);
  // Right-aligned text ends at the right edge.
  c.clear();
  c.textRight(128, 0, "M");
  check(c.get(121, 8) && !c.get(119, 8), "right-aligned 'M' sits in the last cell", 0);
  // Every letter and digit has ink and fits its cell.
  bool ok = true;
  for (int ch = '0'; ch <= 'z'; ch++) {
    if (!std::isalnum(ch)) continue;
    int ink = 0;
    for (int i = 0; i < 16; i++) ink += ui::kFont8x16[ch - 32][i] != 0;
    ok = ok && ink > 0;
  }
  check(ok, "8x16: every letter and digit has ink", 0);
}

// The home screen: the blocks (filled when on), the cursor under one, the
// tempo and beat while the drums play (DISPLAY.md, "Home").
static void testHome() {
  std::printf("Home screen\n");
  uint8_t buf[1024];
  ui::Canvas c(buf);
  ui::UiModel m = fakeModel();  // IR on, delay and reverb on, drums playing
  c.clear();
  ui::drawHome(c, m, ui::kBlockDelay);
  check(c.get(40, 30) && c.get(40, 31) && !c.get(10, 30), "cursor bar under DLY, not under IR", 0);
  check(c.get(28, 17) && c.get(98, 17), "on blocks are filled (DLY, DRM)", 0);
  c.clear();
  ui::drawHome(c, m, ui::kBlockIR);
  check(c.get(10, 30) && !c.get(40, 30), "  ... and under IR", 0);
  // The beat boxes: 8 px every 10 px from 6 px after the big "100", rows
  // 52-59; beat 2 of 4 filled, the others hollow.
  bool beats = true;
  for (int i = 0; i < 4; i++) {
    const int bx = 48 + 6 + i * 10;
    beats = beats && c.get(bx, 55) && c.get(bx + 4, 55) == (i == 1);
  }
  check(beats && !c.get(54 + 40, 55), "playing: 4 beat boxes, beat 2 filled", 0);
  m.drumsPlaying = false;
  m.delayOn = false;
  c.clear();
  ui::drawHome(c, m, ui::kBlockIR);
  bool hollow = true, tempo = false;
  for (int i = 0; i < 4; i++) hollow = hollow && c.get(54 + i * 10, 55) && !c.get(58 + i * 10, 55);
  for (int y = 32; y < 64; y++)
    for (int x = 0; x < 48; x++) tempo = tempo || c.get(x, y);
  check(hollow && tempo, "stopped: the tempo, the beat boxes all hollow", 0);
  bool style = false;
  for (int y = 34; y < 46; y++)
    for (int x = 54; x < 128; x++) style = style || c.get(x, y);
  check(style, "the style's name beside the tempo", 0);
  c.clear();
  ui::drawHome(c, m, ui::kBlockTempo);
  check(c.get(10, 60) && !c.get(10, 30), "cursor on the tempo: a bar under the digits", 0);
  c.clear();
  ui::drawHome(c, m, ui::kBlockTempo, true);
  check(c.get(1, 35) && !c.get(10, 60), "editing the tempo: drawn inverted", 0);
  check(!c.get(40, 20) && c.get(27, 20) && c.get(56, 20), "off block: an outline (DLY)", 0);
}

// The editor's font is fixed per item: every number fits the 16x32 font at
// both ends of its range; names are always in 8x16.
static void testEditorFonts() {
  std::printf("Editor fonts\n");
  FakeHost h;
  const ui::MenuPage* pages = ui::menuPages();
  ui::Menu menu(pages, h);
  bool fits = true, names = true;
  for (int pg = 0; pg <= ui::kPageTrainer; pg++) {
    for (int i = 0; i < pages[pg].count; i++) {
      const ui::MenuItem& it = pages[pg].items[i];
      if (it.kind != ui::Kind::kValue) continue;
      if (!ui::Menu::bigValue(it)) {
        names = names && (it.id == ui::kDrumStyle || it.id == ui::kDrumKit || it.id == ui::kDrumSong);
        continue;
      }
      int lo = it.min, hi = it.max;
      h.limits(it.id, &lo, &hi);
      for (int v : {lo, hi, 0}) {
        char buf[32];
        if (v < lo || v > hi) continue;
        if (!(it.id == ui::kDrumLoopFrom && v == 0 && h.valueText(it.id, buf, sizeof(buf))))
          ui::formatValue(it, v, true, buf, sizeof(buf));
        if (ui::Canvas::width(buf, ui::Font::kBig) > ui::Canvas::kW) {
          std::printf("    too wide for 16x32: %s\n", buf);
          fits = false;
        }
      }
    }
  }
  check(fits, "every number fits the big font at its range ends", 0);
  check(names, "only Style, Kit and Song are names (small)", 0);
}

static void testPanel() {
  std::printf("Panel\n");
  FakeHost h;
  ui::UiModel m = fakeModel();
  ui::Panel p(ui::menuPages(), h, m);
  using P = ui::Panel;
  uint32_t t = 1000;

  // Home: turn moves between the blocks, stopping at the ends.
  check(!p.turn(-1, t) && p.block() == ui::kBlockIR, "home: the cursor starts on IR, stops at the left", p.block());
  p.turn(1, t);
  check(p.block() == ui::kBlockDelay, "  ... turn: DLY", p.block());
  p.turn(10, t);
  check(p.block() == ui::kBlockTempo, "  ... past DRM to the tempo, and stops there", p.block());

  // The tempo: push edits it in place, live; BACK restores, push keeps.
  const int bpm = h.v[ui::kDrumTempo];
  p.push(t);
  check(p.tempoEditing() && p.screen() == P::kHome, "push on the tempo: editing, still home", p.tempoEditing());
  p.turn(1, t);
  p.turn(1, t + 500);
  check(h.v[ui::kDrumTempo] == bpm + 2, "  ... two slow detents: +2 BPM, applied live", h.v[ui::kDrumTempo]);
  p.turn(1, t + 510);
  check(h.v[ui::kDrumTempo] == bpm + 7, "  ... a fast detent counts x5", h.v[ui::kDrumTempo]);
  p.back(t);
  check(!p.tempoEditing() && p.screen() == P::kHome && h.v[ui::kDrumTempo] == bpm, "  ... BACK restores, stays home",
        h.v[ui::kDrumTempo]);
  p.push(t);
  p.turn(-500, t + 2000);
  check(h.v[ui::kDrumTempo] == P::kTempoMin, "  ... clamped at 40 BPM", h.v[ui::kDrumTempo]);
  p.push(t);
  check(!p.tempoEditing() && h.v[ui::kDrumTempo] == P::kTempoMin, "  ... push keeps it", h.v[ui::kDrumTempo]);
  p.push(t);
  check(p.tick(t + P::kTimeoutMs) && !p.tempoEditing(), "  ... 30 s idle ends editing", p.tempoEditing());
  h.v[ui::kDrumTempo] = bpm;
  const int playing = h.v[ui::kDrumsPlay];
  p.hold(t);
  check(h.v[ui::kDrumsPlay] == !playing, "hold on the tempo: drums start/stop", h.v[ui::kDrumsPlay]);
  p.hold(t);
  p.turn(-1, t);
  check(p.block() == ui::kBlockDrums, "  ... turn back: DRM", p.block());

  // Push opens the block's page; BACK from it returns home, cursor kept.
  p.push(t);
  check(p.screen() == P::kMenu && p.menu().pageIndex() == ui::kPageDrums, "push on DRM: the Drums page", p.menu().pageIndex());
  p.back(t);
  check(p.screen() == P::kHome && p.block() == ui::kBlockDrums, "  ... back: home, cursor still on DRM", p.screen());
  p.turn(-1, t);
  p.push(t);
  check(p.menu().pageIndex() == ui::kPageReverb, "push on REV: the Reverb page", p.menu().pageIndex());
  p.back(t);
  p.turn(-1, t);
  p.push(t);
  check(p.menu().pageIndex() == ui::kPageDelay, "push on DLY: the Delay page", p.menu().pageIndex());
  p.turn(3, t);
  p.push(t);
  p.turn(2, t + 500);
  p.back(t);
  check(p.screen() == P::kMenu && h.v[ui::kDelayTime] == 380, "  ... editing: back cancels, still on the page", h.v[ui::kDelayTime]);
  p.back(t);
  check(p.screen() == P::kHome, "  ... back again: home", p.screen());

  // IR: the list on the loaded IR; push loads and stays; Rescan; back home.
  p.turn(-1, t);
  p.push(t);
  check(p.screen() == P::kIrList && p.irCursor() == 2, "push on IR: the IR list, on the loaded IR", p.irCursor());
  p.turn(1, t);
  p.push(t);
  check(h.loaded == 3 && p.screen() == P::kIrList, "  ... push loads IR 4, the list stays", h.loaded);
  p.turn(100, t);
  p.confirm(t);
  check(p.irCursor() == m.irCount && h.rescans == 1, "  ... the last row (CONFIRM): rescan", h.rescans);
  p.back(t);
  check(p.screen() == P::kHome && p.block() == ui::kBlockIR, "  ... back: home", p.screen());

  // Hold: the selected block on/off; CONFIRM: the drums.
  p.hold(t);
  check(h.v[ui::kBypass] == 1, "hold on IR: bypass", h.v[ui::kBypass]);
  p.hold(t);
  check(h.v[ui::kBypass] == 0, "  ... hold again: back on", h.v[ui::kBypass]);
  bool ok = true;
  const uint8_t ids[] = {ui::kDelayOn, ui::kReverbOn, ui::kDrumsPlay};
  for (int i = 0; i < 3; i++) {
    p.turn(1, t);
    const int before = h.v[ids[i]];
    p.hold(t);
    ok = ok && h.v[ids[i]] == !before && p.screen() == P::kHome;
  }
  check(ok, "hold on DLY, REV, DRM: each one's on/off", ok);
  const int drums = h.v[ui::kDrumsPlay];
  p.turn(-10, t);
  p.confirm(t);
  check(h.v[ui::kDrumsPlay] == !drums && p.block() == ui::kBlockIR, "CONFIRM: drums start/stop from any block", h.v[ui::kDrumsPlay]);

  // BACK: the menu; its screens return to it.
  p.back(t);
  check(p.screen() == P::kMenu && p.menu().pageIndex() == ui::kPageTop, "BACK on home: the top menu", p.menu().pageIndex());
  p.confirm(t);
  check(p.screen() == P::kIrList, "  ... CONFIRM works like push (IR)", p.screen());
  p.back(t);
  check(p.screen() == P::kMenu, "  ... back from the IR list: the menu", p.screen());
  p.turn(6, t);
  p.push(t);
  check(p.screen() == P::kMeter, "Meter", p.screen());
  p.turn(2, t);
  p.confirm(t);
  check(h.v[ui::kLineIn] == 2 && h.resets == 1, "  ... turn: line-in sens 0 -> 2; CONFIRM: reset", h.v[ui::kLineIn]);
  check(!p.tick(t + 60000) && p.screen() == P::kMeter, "  ... no timeout on the meter", p.screen());
  p.back(t);
  p.turn(1, t);
  p.push(t);
  check(p.screen() == P::kStatus, "Status", p.screen());
  p.back(t);
  check(p.screen() == P::kMenu, "  ... back: the menu", p.screen());
  p.back(t);
  check(p.screen() == P::kHome, "back from the top menu: home", p.screen());

  // 30 s without input: home.
  p.back(t);
  check(!p.tick(t + P::kTimeoutMs - 1) && p.screen() == P::kMenu, "menu: still open after 29.999 s", p.screen());
  check(p.tick(t + P::kTimeoutMs) && p.screen() == P::kHome, "  ... home after 30 s", p.screen());

  // The IR list's marquee scrolls long names only.
  p.push(t);  // IR list, on "V30 SM57 Mesa 4x12 close"
  check(p.tick(t + P::kMarqueeMs), "marquee: a long name scrolls", 0);
  p.turn(-2, t);  // "Greenback"
  check(!p.tick(t + 2 * P::kMarqueeMs), "  ... a short one doesn't", 0);
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
  testHome();
  testEditorFonts();
  testPanel();
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
