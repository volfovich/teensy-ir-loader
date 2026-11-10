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
  check(m.current()->id == ui::kAmpOn && m.current()->kind == ui::Kind::kSubmenu, "the top level starts with Amp",
        m.current()->id);
  check(m.turn(5, 0) == ui::Menu::kRedraw && m.current()->id == ui::kDelayOn,
        "turn moves past Gate, Presets, IR, Doubler to Delay",
        m.current()->id);
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
  m.turn(6, 0);
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

static void testDoublerPage() {
  std::printf("Doubler page\n");
  FakeHost h;
  ui::Menu m(ui::menuPages(), h);
  m.open();
  m.turn(4, 0);
  check(m.current()->id == ui::kDblOn && m.current()->kind == ui::Kind::kSubmenu, "Doubler after IR (a page)",
        m.current()->id);
  m.push();
  check(m.pageIndex() == ui::kPageDoubler && m.current()->id == ui::kDblOn, "  ... its first row: On", m.pageIndex());
  m.push();
  check(h.v[ui::kDblOn] == 0, "  ... push turns it off", h.v[ui::kDblOn]);
  m.turn(1, 0);
  m.push();
  m.turn(100, 1000);
  m.push();
  check(h.v[ui::kDblTime] == 35, "  ... Time up to 35 ms", h.v[ui::kDblTime]);
  m.push();
  m.turn(-100, 2000);
  m.push();
  check(h.v[ui::kDblTime] == 1, "  ... and down to 1 ms", h.v[ui::kDblTime]);
  m.turn(1, 0);
  check(m.current()->id == ui::kDblWidth, "  ... then Width, the last row", m.current()->id);
  check(ui::paramDef(ui::kDblTime).def == 10, "  ... Time defaults to 10 ms", ui::paramDef(ui::kDblTime).def);
}

static void testSongRows() {
  std::printf("Drums page: songs\n");
  FakeHost h;
  ui::Menu m(ui::menuPages(), h);
  m.open();
  m.turn(7, 0);
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
  m.turn(7, 0);
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
  ui::formatValue(ui::paramDef(m.current()->id), h.v[ui::kDrumCountIn], false, b2, sizeof(b2));
  check(h.v[ui::kDrumCountIn] == 0 && !std::strcmp(b2, "off"), "  ... 4 bars, then off", h.v[ui::kDrumCountIn]);
  const ui::ParamDef& bars = ui::paramDef(ui::kTrainerEvery);
  char b[16], b1[16];
  ui::formatValue(bars, 4, false, b, sizeof(b));
  ui::formatValue(bars, 1, false, b1, sizeof(b1));
  check(!std::strcmp(b, "4 bars") && !std::strcmp(b1, "1 bar"), "bars formatting (4 bars, 1 bar)", 0);
}

static void testFormat() {
  std::printf("Value formatting\n");
  const ui::ParamDef& gain = ui::paramDef(ui::kOutGain);
  const ui::ParamDef& time = ui::paramDef(ui::kDelayTime);
  char b[32];
  bool ok = true;
  ui::formatValue(gain, -12, false, b, sizeof(b)); ok = ok && !std::strcmp(b, "-6.0dB");
  ui::formatValue(gain, 7, true, b, sizeof(b)); ok = ok && !std::strcmp(b, "+3.5 dB");
  ui::formatValue(gain, -1, false, b, sizeof(b)); ok = ok && !std::strcmp(b, "-0.5dB");
  ui::formatValue(gain, 0, false, b, sizeof(b)); ok = ok && !std::strcmp(b, "0.0dB");
  ui::formatValue(time, 380, true, b, sizeof(b)); ok = ok && !std::strcmp(b, "380 ms");
  check(ok, "half dB and ms (-6.0dB, +3.5 dB, -0.5dB, 0.0dB, 380 ms)", ok);
  const ui::ParamDef& gate = ui::paramDef(ui::kGateThr);
  ok = true;
  ui::formatValue(gate, -64, true, b, sizeof(b)); ok = ok && !std::strcmp(b, "-64 dB");
  ui::formatValue(gate, gate.min, false, b, sizeof(b)); ok = ok && !std::strcmp(b, "-90dB");
  check(ok, "whole dB (the gate's threshold: -64 dB, -90dB)", ok);
  ui::formatValue(ui::paramDef(ui::kBassKey), 6, true, b, sizeof(b));
  check(!std::strcmp(b, "F#"), "a name from the table's choices (key 6: F#)", 0);
  char n[32];
  ui::irDisplayName("/IR/OH Mdrn-Prog.wav", n, sizeof(n));
  check(!std::strcmp(n, "OH Mdrn-Prog"), "IR name without folder and .wav", 0);
}

// The parameter table: defaults in range, serial keys unique, and typed
// values parsed per unit.
static void testParams() {
  std::printf("Parameter table\n");
  bool inRange = true, unique = true;
  for (int i = 0; i < ui::kValueCount; i++) {
    const ui::ParamDef& d = ui::paramDef(i);
    inRange = inRange && d.min <= d.def && d.def <= d.max && d.step > 0 && d.name && d.label;
    for (int j = 0; j < i; j++)
      if (d.key && ui::paramDef(j).key && !std::strcmp(d.key, ui::paramDef(j).key)) unique = false;
  }
  check(inRange, "every default within its range", inRange);
  check(unique, "serial keys unique", unique);
  int v = -99;
  bool ok = true;
  ok = ok && ui::parseValue(ui::paramDef(ui::kDelayOn), "on", &v) && v == 1;
  ok = ok && ui::parseValue(ui::paramDef(ui::kDelayOn), "OFF", &v) && v == 0;
  ok = ok && ui::parseValue(ui::paramDef(ui::kDelayTime), "450", &v) && v == 450;
  ok = ok && !ui::parseValue(ui::paramDef(ui::kDelayTime), "45x", &v);
  ok = ok && ui::parseValue(ui::paramDef(ui::kOutGain), "-6", &v) && v == -12;
  ok = ok && ui::parseValue(ui::paramDef(ui::kOutGain), "3.5", &v) && v == 7;
  ok = ok && ui::parseValue(ui::paramDef(ui::kGateThr), "-70", &v) && v == -70;
  ok = ok && !ui::parseValue(ui::paramDef(ui::kGateThr), "off", &v);
  check(ok, "on/off, integers, dB in half steps, whole dB (no off)", ok);
  ok = true;
  ok = ok && ui::parseValue(ui::paramDef(ui::kReverbMode), "gated", &v) && v == 1;
  ok = ok && ui::parseValue(ui::paramDef(ui::kReverbMode), "0", &v) && v == 0;
  ok = ok && ui::parseValue(ui::paramDef(ui::kDrumCrash), "8", &v) && v == 3;    // "8 bars"
  ok = ok && ui::parseValue(ui::paramDef(ui::kDrumCrash), "0", &v) && v == 0;    // index: off
  ok = ok && ui::parseValue(ui::paramDef(ui::kDrumCountIn), "2", &v) && v == 2;  // "2 bars"
  ok = ok && ui::parseValue(ui::paramDef(ui::kBassKey), "f#", &v) && v == 6;
  check(ok, "choices by name, first word or index (crash 8 -> 8 bars)", ok);
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
  uint8_t buf[1024], buf2[1024];
  ui::Canvas c(buf);
  ui::UiModel m = fakeModel();  // IR on, delay and reverb on, drums playing
  c.clear();
  // The blocks: AMP x 0-24, NG 30-46, IR 52-68, DLY 74-98, REV 103-127.
  ui::drawHome(c, m, ui::kBlockDelay);
  check(c.get(86, 30) && c.get(86, 31) && !c.get(60, 30), "cursor bar under DLY, not under IR", 0);
  check(c.get(1, 17) && c.get(75, 17) && c.get(104, 17), "on blocks are filled (AMP, DLY, REV)", 0);
  c.clear();
  ui::drawHome(c, m, ui::kBlockIR);
  check(c.get(60, 30) && !c.get(86, 30) && !c.get(12, 30), "  ... and under IR", 0);
  char name[64];
  ui::homeName(m, ui::kBlockIR, name, sizeof(name));
  check(!std::strcmp(name, "V30 SM57 Mesa 4x12 close"), "row 0: the IR's name", 0);
  ui::homeName(m, ui::kBlockAmp, name, sizeof(name));
  check(!std::strcmp(name, "JCM 800 2203 OD8008 Boosted Amp Gain 5"), "  ... on AMP: the model's (_ as spaces)", 0);
  ui::listName(m, ui::FileList::kAmp, 6, name, sizeof(name));
  check(!std::strcmp(name, "OD8008 Boosted Amp Gain 5"), "the amp list: without the words all names share", 0);
  ui::listName(m, ui::FileList::kIR, 2, name, sizeof(name));
  check(!std::strcmp(name, "V30 SM57 Mesa 4x12 close"), "  ... the IR list: nothing shared, the whole name", 0);
  c.clear();
  ui::drawFileList(c, m, ui::FileList::kAmp, 7, 5, 0);  // rows: models 6, 7, 8 (cursor)
  ui::Canvas ref(buf2);
  ref.clear();
  ref.text(ui::Canvas::kLeft, 2, "06 OD800~ain 39");
  bool same = true;
  for (int y = 16; y < 32; y++)
    for (int x = 0; x < 128; x++) same = same && c.get(x, y) == ref.get(x, y);
  check(same, "  ... a long name off the cursor: both ends, '~' between", 0);
  c.clear();
  ui::drawHome(c, m, ui::kBlockIR);
  // The beat boxes: 8 px every 10 px from 4 px after the big "100", rows
  // 50-57; beat 2 of 4 filled, the others hollow.
  bool beats = true;
  for (int i = 0; i < 4; i++) {
    const int bx = 52 + i * 10;
    beats = beats && c.get(bx, 55) && c.get(bx + 4, 55) == (i == 1);
  }
  check(beats && !c.get(52 + 40, 55), "playing: 4 beat boxes, beat 2 filled", 0);
  m.drumsPlaying = false;
  m.delayOn = false;
  c.clear();
  ui::drawHome(c, m, ui::kBlockIR);
  bool hollow = true, tempo = false;
  for (int i = 0; i < 4; i++) hollow = hollow && c.get(52 + i * 10, 55) && !c.get(56 + i * 10, 55);
  for (int y = 32; y < 64; y++)
    for (int x = 0; x < 48; x++) tempo = tempo || c.get(x, y);
  check(hollow && tempo, "stopped: the tempo, the beat boxes all hollow", 0);
  bool style = false;
  for (int y = 34; y < 46; y++)
    for (int x = 52; x < 128; x++) style = style || c.get(x, y);
  check(style, "the style's name beside the tempo", 0);
  c.clear();
  ui::drawHome(c, m, ui::kBlockTempo);
  check(c.get(10, 60) && !c.get(10, 30), "cursor on the tempo: a bar under the digits", 0);
  c.clear();
  ui::drawHome(c, m, ui::kBlockTempo, ui::kBlockTempo);
  check(c.get(1, 35) && !c.get(10, 60), "editing the tempo: drawn inverted", 0);
  check(!c.get(86, 17) && c.get(86, 16) && c.get(86, 28) && !c.get(72, 20), "off block: top and bottom lines (DLY)", 0);
  check(c.get(38, 17) && c.get(38, 27), "the gate on: NG filled", 0);
  // No DRM block any more: REV ends the row at the right edge.
  check(c.get(127, 16) && c.get(127, 28), "  ... REV last, at the right edge", 0);
  c.clear();
  ui::drawHome(c, m, ui::kBlockStyle);
  check(c.get(53, 47) && !c.get(53, 30), "cursor on the style: a bar under its name", 0);
  c.clear();
  ui::drawHome(c, m, ui::kBlockStyle, ui::kBlockStyle);
  check(c.get(52, 33) && !c.get(53, 48), "editing the style: drawn inverted (no bar)", 0);
  // TRN: a block on row 3, right of the beat boxes; filled while on.
  c.clear();
  ui::drawHome(c, m, ui::kBlockTrainer);
  check(c.get(110, 48) && c.get(110, 60) && !c.get(110, 52) && c.get(110, 62), "TRN off: top and bottom lines, the cursor under it", 0);
  m.trainerOn = true;
  c.clear();
  ui::drawHome(c, m, ui::kBlockIR);
  check(c.get(104, 52) && !c.get(110, 62), "  ... on: filled", 0);
  // 7 beats: smaller boxes, all left of TRN.
  m.drumBeats = 7;
  c.clear();
  ui::drawHome(c, m, ui::kBlockIR);
  bool seven = true;
  for (int i = 0; i < 7; i++) seven = seven && c.get(52 + i * 7, 54);
  check(seven && !c.get(101, 54), "7 beats: 7 smaller boxes, clear of TRN", 0);
}

// The editor's font is fixed per item: every number fits the 16x32 font at
// both ends of its range; names are always in 8x16.
static void testEditorFonts() {
  std::printf("Editor fonts\n");
  FakeHost h;
  const ui::MenuPage* pages = ui::menuPages();
  ui::Menu menu(pages, h);
  bool fits = true, names = true;
  for (int pg = 0; pg < ui::kPageCount; pg++) {
    for (int i = 0; i < pages[pg].count; i++) {
      const ui::MenuItem& it = pages[pg].items[i];
      if (it.kind != ui::Kind::kSetting || ui::editOf(ui::paramDef(it.id)) != ui::Edit::kValue) continue;
      if (!ui::Menu::bigValue(it)) {
        names = names && (it.id == ui::kDrumStyle || it.id == ui::kDrumKit || it.id == ui::kDrumSong ||
                          it.id == ui::kBassKey);
        continue;
      }
      int lo = ui::paramDef(it.id).min, hi = ui::paramDef(it.id).max;
      h.limits(it.id, &lo, &hi);
      for (int v : {lo, hi, 0}) {
        char buf[32];
        if (v < lo || v > hi) continue;
        if (!(it.id == ui::kDrumLoopFrom && v == 0 && h.valueText(it.id, buf, sizeof(buf))))
          ui::formatValue(ui::paramDef(it.id), v, true, buf, sizeof(buf));
        if (ui::Canvas::width(buf, ui::Font::kBig) > ui::Canvas::kW) {
          std::printf("    too wide for 16x32: %s\n", buf);
          fits = false;
        }
      }
    }
  }
  check(fits, "every number fits the big font at its range ends", 0);
  check(names, "only Style, Kit, Song and Key are names (small)", 0);
}

static void testPanel() {
  std::printf("Panel\n");
  FakeHost h;
  ui::UiModel m = fakeModel();
  ui::Panel p(ui::menuPages(), h, m);
  using P = ui::Panel;
  uint32_t t = 1000;

  // Home: turn moves between the blocks, stopping at the ends.
  check(p.block() == ui::kBlockAmp, "home: the cursor starts on AMP", p.block());
  check(p.turn(-1, t) && p.block() == ui::kBlockPreset, "  ... PRE (row 0) left of it", p.block());
  check(!p.turn(-1, t) && p.block() == ui::kBlockPreset, "  ... and it stops there", p.block());
  p.turn(1, t);
  p.turn(3, t);
  check(p.block() == ui::kBlockDelay, "  ... turn: past NG, IR to DLY", p.block());
  p.turn(2, t);
  check(p.block() == ui::kBlockTempo, "  ... past REV to the tempo (no DRM)", p.block());
  p.turn(10, t);
  check(p.block() == ui::kBlockTrainer, "  ... then the style and TRN, and stops there", p.block());
  p.turn(-2, t);

  // The tempo: push edits it in place, live; BACK restores, push keeps.
  const int bpm = h.v[ui::kDrumTempo];
  p.push(t);
  check(p.editing() == ui::kBlockTempo && p.screen() == P::kHome, "push on the tempo: editing, still home", p.editing());
  p.turn(1, t);
  p.turn(1, t + 500);
  check(h.v[ui::kDrumTempo] == bpm + 2, "  ... two slow detents: +2 BPM, applied live", h.v[ui::kDrumTempo]);
  p.turn(1, t + 510);
  check(h.v[ui::kDrumTempo] == bpm + 7, "  ... a fast detent counts x5", h.v[ui::kDrumTempo]);
  p.back(t);
  check(p.editing() < 0 && p.screen() == P::kHome && h.v[ui::kDrumTempo] == bpm, "  ... BACK restores, stays home",
        h.v[ui::kDrumTempo]);
  p.push(t);
  p.turn(-500, t + 2000);
  check(h.v[ui::kDrumTempo] == 40, "  ... clamped at 40 BPM", h.v[ui::kDrumTempo]);
  p.push(t);
  check(p.editing() < 0 && h.v[ui::kDrumTempo] == 40, "  ... push keeps it", h.v[ui::kDrumTempo]);
  p.push(t);
  check(p.tick(t + P::kTimeoutMs) && p.editing() < 0, "  ... 30 s idle ends editing", p.editing());
  h.v[ui::kDrumTempo] = bpm;
  const int playing = h.v[ui::kDrumsPlay];
  p.hold(t);
  check(h.v[ui::kDrumsPlay] == !playing, "hold on the tempo: drums start/stop", h.v[ui::kDrumsPlay]);
  p.hold(t);

  // The style: push edits it in place, live; a song gives way; BACK restores
  // the style and the song, push keeps.
  p.turn(1, t);
  check(p.block() == ui::kBlockStyle, "the style after the tempo", p.block());
  h.v[ui::kDrumSong] = 2;
  p.push(t);
  check(p.editing() == ui::kBlockStyle && p.screen() == P::kHome, "push on the style: editing, still home", p.editing());
  p.turn(2, t);
  check(h.v[ui::kDrumStyle] == 3 && h.v[ui::kDrumSong] == 0, "  ... turn: style 1 -> 3, live; the song off",
        h.v[ui::kDrumStyle]);
  p.turn(100, t);
  check(h.v[ui::kDrumStyle] == 7, "  ... clamped at the last style", h.v[ui::kDrumStyle]);
  p.back(t);
  check(p.editing() < 0 && h.v[ui::kDrumStyle] == 1 && h.v[ui::kDrumSong] == 2,
        "  ... BACK restores the style and the song", h.v[ui::kDrumStyle]);
  h.v[ui::kDrumSong] = 0;
  p.push(t);
  p.turn(-1, t);
  p.push(t);
  check(p.editing() < 0 && h.v[ui::kDrumStyle] == 0, "  ... push keeps it", h.v[ui::kDrumStyle]);
  h.v[ui::kDrumStyle] = 1;
  const int playing2 = h.v[ui::kDrumsPlay];
  p.hold(t);
  check(h.v[ui::kDrumsPlay] == !playing2, "hold on the style: drums start/stop", h.v[ui::kDrumsPlay]);
  p.hold(t);

  // TRN: push or hold switches the speed trainer.
  p.turn(1, t);
  p.push(t);
  check(h.v[ui::kTrainerOn] == 1 && p.screen() == P::kHome, "push on TRN: the trainer on, still home", h.v[ui::kTrainerOn]);
  p.hold(t);
  check(h.v[ui::kTrainerOn] == 0, "  ... hold: off", h.v[ui::kTrainerOn]);

  // Push opens the block's page; BACK from it returns home, cursor kept.
  p.turn(-3, t);
  p.push(t);
  check(p.screen() == P::kMenu && p.menu().pageIndex() == ui::kPageReverb, "push on REV: the Reverb page", p.menu().pageIndex());
  p.back(t);
  check(p.screen() == P::kHome && p.block() == ui::kBlockReverb, "  ... back: home, cursor still on REV", p.screen());
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
  const uint8_t ids[] = {ui::kDelayOn, ui::kReverbOn};
  for (int i = 0; i < 2; i++) {
    p.turn(1, t);
    const int before = h.v[ids[i]];
    p.hold(t);
    ok = ok && h.v[ids[i]] == !before && p.screen() == P::kHome;
  }
  check(ok, "hold on DLY, REV: each one's on/off", ok);
  // NG: hold switches the gate, push opens the Gate page.
  p.turn(-3, t);
  check(p.block() == ui::kBlockGate, "NG between AMP and IR", p.block());
  p.hold(t);
  check(h.v[ui::kGateOn] == 0, "  ... hold: the gate off", h.v[ui::kGateOn]);
  p.hold(t);
  p.push(t);
  check(p.screen() == P::kMenu && p.menu().pageIndex() == ui::kPageGate && p.menu().current()->id == ui::kGateOn,
        "  ... push: the Gate page, On first", p.menu().pageIndex());
  p.back(t);
  const int drums = h.v[ui::kDrumsPlay];
  p.turn(-10, t);
  p.turn(1, t);  // AMP
  p.confirm(t);
  check(h.v[ui::kDrumsPlay] == !drums && p.block() == ui::kBlockAmp, "CONFIRM: drums start/stop from any block", h.v[ui::kDrumsPlay]);

  // BACK: the menu; its screens return to it.
  p.back(t);
  check(p.screen() == P::kMenu && p.menu().pageIndex() == ui::kPageTop, "BACK on home: the top menu", p.menu().pageIndex());
  p.turn(3, t);
  p.confirm(t);
  check(p.screen() == P::kIrList, "  ... CONFIRM works like push (IR)", p.screen());
  p.back(t);
  check(p.screen() == P::kMenu, "  ... back from the IR list: the menu", p.screen());
  p.turn(7, t);
  p.push(t);
  check(p.screen() == P::kTuner, "Tuner (after Bypass)", p.screen());
  check(!p.tick(t + 60000) && p.screen() == P::kTuner, "  ... no timeout on the tuner", p.screen());
  p.back(t);
  check(p.screen() == P::kMenu, "  ... back: the menu", p.screen());
  p.turn(1, t);
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

  // Holding BACK opens the tuner (its press has gone back a step first).
  p.back(t);
  check(p.backHold(t) && p.screen() == P::kTuner, "BACK held (from home, via the menu): the tuner", p.screen());
  check(!p.backHold(t), "  ... held again: stays", p.screen());
  p.push(t);
  check(p.screen() == P::kMenu, "  ... push closes it, back to the menu", p.screen());
  p.back(t);
  p.backHold(t);
  p.back(t);
  check(p.screen() == P::kHome, "  ... from home: BACK returns home", p.screen());

  // The IR list's marquee scrolls long names only.
  p.turn(2, t);
  p.push(t);  // IR list, on "V30 SM57 Mesa 4x12 close"
  check(p.tick(t + P::kMarqueeMs), "marquee: a long name scrolls", 0);
  p.turn(-2, t);  // "Greenback"
  p.tick(t + P::kPreviewMs);  // it plays (a redraw)
  check(!p.tick(t + 2 * P::kMarqueeMs), "  ... a short one doesn't", 0);
}

// The amp block on the panel: the AMP block (push: the model list, hold:
// on/off, row 0 the model's name, scrolling) and the Amp page.
static void testAmpPanel() {
  std::printf("Amp on the panel\n");
  FakeHost h;
  ui::UiModel m = fakeModel();
  ui::Panel p(ui::menuPages(), h, m);
  using P = ui::Panel;
  uint32_t t = 1000;

  p.hold(t);
  check(h.v[ui::kAmpOn] == 0 && p.screen() == P::kHome, "hold on AMP: the amp off", h.v[ui::kAmpOn]);
  p.hold(t);
  check(h.v[ui::kAmpOn] == 1, "  ... hold again: on", h.v[ui::kAmpOn]);
  check(p.tick(t + P::kMarqueeMs), "  ... row 0 scrolls the model's long name", 0);
  p.push(t);
  check(p.screen() == P::kAmpList && p.irCursor() == 6, "push on AMP: the model list, on the loaded model", p.irCursor());
  p.turn(1, t);
  p.push(t);
  check(h.ampLoaded == 7 && h.loaded < 0 && p.screen() == P::kAmpList, "  ... push loads model 8, the list stays",
        h.ampLoaded);
  p.turn(100, t);
  p.confirm(t);
  check(p.irCursor() == m.ampCount && h.rescans == 1, "  ... the last row (CONFIRM): rescan", h.rescans);
  p.back(t);
  check(p.screen() == P::kHome && p.block() == ui::kBlockAmp, "  ... back: home, still on AMP", p.screen());
  p.turn(1, t);
  check(!p.tick(t + 10 * P::kMarqueeMs), "on IR: row 0 doesn't scroll", 0);

  p.back(t);  // the top menu: Amp first
  p.push(t);
  check(p.screen() == P::kMenu && p.menu().pageIndex() == ui::kPageAmp && p.menu().current()->id == ui::kAmpOn,
        "menu > Amp: the Amp page, On first", p.menu().pageIndex());
  p.turn(1, t);
  p.push(t);
  check(p.screen() == P::kAmpList, "  ... Model opens the model list", p.screen());
  p.back(t);
  check(p.screen() == P::kMenu && p.menu().pageIndex() == ui::kPageAmp, "  ... back: the Amp page", p.screen());
  p.turn(1, t);
  p.push(t);
  p.turn(2, t + 1000);
  p.push(t);
  check(h.v[ui::kAmpIn] == 8, "  ... Input: +3.0 -> +4.0 dB (half-dB steps)", h.v[ui::kAmpIn]);
  p.turn(1, t);
  p.push(t);
  p.turn(5, t + 2000);
  p.push(t);
  check(h.v[ui::kAmpBass] == 5, "  ... Bass: 0.0 -> +2.5 dB (half-dB steps)", h.v[ui::kAmpBass]);
  p.turn(5, t);
  check(p.menu().current()->id == ui::kAmpCal, "  ... Calibrate last (after Middle, Treble, Presence, Output; no gate)",
        p.menu().current()->id);
  p.push(t);
  check(h.v[ui::kAmpCal] == 0, "  ... Calibrate toggles", h.v[ui::kAmpCal]);

  // The gate's own page, after Amp in the top menu.
  p.back(t);
  p.turn(1, t);
  check(p.menu().current()->id == ui::kGateOn && p.menu().current()->kind == ui::Kind::kSubmenu, "menu > Gate after Amp",
        p.menu().current()->id);
  p.push(t);
  p.push(t);
  check(p.menu().pageIndex() == ui::kPageGate && h.v[ui::kGateOn] == 0, "  ... On toggles", h.v[ui::kGateOn]);
  p.turn(1, t);
  check(p.menu().current()->id == ui::kGateType && h.v[ui::kGateType] == 1, "  ... Type next, hard by default",
        p.menu().current()->id);
  p.push(t);
  check(h.v[ui::kGateType] == 0, "  ... Type: push steps hard -> nam", h.v[ui::kGateType]);
  p.turn(1, t);
  p.turn(1, t);
  check(p.menu().current()->id == ui::kGateThr, "  ... no Release with the nam type (Threshold last)",
        p.menu().current()->id);
  p.turn(-1, t);
  p.push(t);
  check(h.v[ui::kGateType] == 1, "  ... Type: push steps nam -> hard", h.v[ui::kGateType]);
  p.turn(1, t);
  p.push(t);
  p.turn(-4, t + 4000);
  p.push(t);
  check(h.v[ui::kGateThr] == -68, "  ... Threshold: -64 -> -68 dB", h.v[ui::kGateThr]);
  p.turn(1, t);
  check(p.menu().current()->id == ui::kGateRel, "  ... Release shown for hard, with the gate off too",
        p.menu().current()->id);
  p.push(t);
  p.turn(4, t + 6000);
  p.push(t);
  check(h.v[ui::kGateRel] == 70, "  ... Release: 50 -> 70 ms (5 ms steps)", h.v[ui::kGateRel]);
  char b[16];
  check(h.valueText(ui::kScreenAmp, b, sizeof(b)) && !std::strcmp(b, "7/8"), "  ... Model shows 7/8", 0);
}

// The IR list plays the IR under the cursor once it rests, without saving
// it; a click keeps it; leaving without one loads the saved IR again.
static void testIrPreview() {
  std::printf("IR list: hear while scrolling\n");
  FakeHost h;
  ui::UiModel m = fakeModel();  // IR 3 (index 2) loaded and saved
  ui::Panel p(ui::menuPages(), h, m);
  using P = ui::Panel;
  uint32_t t = 1000;
  p.turn(2, t);  // AMP > NG > IR
  p.push(t);
  check(p.screen() == P::kIrList && p.irCursor() == 2, "the IR list, on the saved IR", p.irCursor());
  p.tick(t + 1000);
  check(h.previews == 0, "  ... nothing plays until the cursor moves", h.previews);

  t = 5000;
  p.turn(1, t);
  check(h.previews == 0, "turn: nothing loads at once", h.previews);
  p.tick(t + P::kPreviewMs - 1);
  check(h.previews == 0, "  ... still nothing just before the rest time", h.previews);
  check(p.tick(t + P::kPreviewMs) && h.previews == 1 && h.previewed == 3 && h.loaded < 0,
        "  ... after it: IR 4 plays, not saved", h.previewed);
  p.tick(t + 1000);
  check(h.previews == 1, "  ... once", h.previews);

  // A fast scroll plays only where it stops.
  t = 10000;
  for (int i = 0; i < 4; i++) p.turn(1, t + i * 40);
  p.tick(t + 3 * 40 + P::kPreviewMs);
  check(h.previews == 2 && h.previewed == 7, "a fast scroll: only IR 8, where it stopped", h.previewed);

  // BACK without a click: the saved IR plays again.
  p.back(t + 1000);
  check(p.screen() == P::kHome && h.previews == 3 && h.previewed == 2 && h.loaded < 0,
        "BACK: home, the saved IR (3) loads again, nothing saved", h.previewed);

  // Push keeps it: saved; leaving then reloads nothing.
  t = 20000;
  p.push(t);
  p.turn(-1, t);
  p.tick(t + P::kPreviewMs);
  check(h.previewed == 1, "back in the list: IR 2 plays", h.previewed);
  p.push(t + 500);
  check(h.loaded == 1 && p.screen() == P::kIrList, "  ... push keeps it (saved), the list stays", h.loaded);
  m.irCurrent = m.irSaved = 1;  // as the firmware's model follows
  const int before = h.previews;
  p.tick(t + 1000);
  p.back(t + 1000);
  check(h.previews == before && p.screen() == P::kHome, "  ... BACK then: nothing reloads", h.previews);

  // The timeout goes back to the saved IR (now 2) too.
  t = 30000;
  p.push(t);
  p.turn(3, t);
  p.tick(t + P::kPreviewMs);
  check(h.previewed == 4, "IR 5 plays", h.previewed);
  check(p.tick(t + P::kTimeoutMs) && p.screen() == P::kHome && h.previewed == 1,
        "  ... 30 s idle: home, the saved IR (2) again", h.previewed);

  // The Rescan row plays nothing; neither does the amp list.
  t = 40000;
  p.push(t);
  const int n = h.previews;
  p.turn(100, t);
  p.tick(t + 1000);
  check(h.previews == n && p.irCursor() == m.irCount, "the Rescan row: nothing plays", h.previews);
  p.back(t + 1000);
  p.turn(-10, t);
  p.turn(1, t);
  p.push(t);  // AMP: the model list
  p.turn(1, t);
  p.tick(t + 1000);
  check(p.screen() == P::kAmpList && h.previews == n && h.ampLoaded < 0, "the amp list: nothing loads by scrolling",
        h.previews);
}

// Presets on the panel (PRESETS.md, "Panel"): PRE on row 0 and its names;
// the list, which plays a preset once the cursor rests and keeps it on push
// (leaving without goes back); naming a new preset; save and delete behind a
// confirm; the Presets page; the name editor's keys.
static void testPresetPanel() {
  std::printf("Presets on the panel\n");
  FakeHost h;
  ui::UiModel m = fakeModel();  // presets: 5, "03 Lead" recalled
  ui::Panel p(ui::menuPages(), h, m);
  using P = ui::Panel;
  uint32_t t = 1000;
  char name[64];

  // Row 0.
  ui::homeName(m, ui::kBlockPreset, name, sizeof(name));
  check(!std::strcmp(name, "03 Lead"), "row 0 on PRE: the recalled preset", 0);
  ui::homeName(m, ui::kBlockDelay, name, sizeof(name));
  check(!std::strcmp(name, "03 Lead"), "  ... and on the other blocks", 0);
  ui::homeName(m, ui::kBlockIR, name, sizeof(name));
  check(!std::strcmp(name, "V30 SM57 Mesa 4x12 close"), "  ... but the IR's on IR", 0);
  m.presetEdited = true;
  ui::homeName(m, ui::kBlockPreset, name, sizeof(name));
  check(!std::strcmp(name, "03 Lead*"), "  ... * when the sound differs from it", 0);
  m.presetEdited = false;
  m.presetCurrent = -1;
  ui::homeName(m, ui::kBlockDelay, name, sizeof(name));
  check(!std::strcmp(name, "V30 SM57 Mesa 4x12 close"), "  ... none recalled: the IR's, as before presets", 0);
  ui::homeName(m, ui::kBlockPreset, name, sizeof(name));
  check(!std::strcmp(name, "No preset"), "  ... and on PRE: No preset", 0);
  m.presetCurrent = 2;
  uint8_t buf[1024];
  ui::Canvas c(buf);
  ui::drawHome(c, m, ui::kBlockPreset);
  check(c.get(0, 0) && c.get(127, 14) && !c.get(127, 15), "PRE: row 0 inverted, a dark line under it", 0);

  // The list: it opens on the recalled preset; a preset plays once the
  // cursor rests on it, not while scrolling past.
  p.turn(-1, t);
  check(p.block() == ui::kBlockPreset, "PRE left of AMP", p.block());
  p.push(t);
  check(p.screen() == P::kPresetList && p.irCursor() == 2, "push on PRE: the preset list, on the recalled one",
        p.irCursor());
  p.turn(1, t);
  p.turn(1, t + 50);
  p.tick(t + 100);
  check(h.presetTries == 0, "  ... scrolling past plays nothing", h.presetTries);
  p.tick(t + 50 + P::kPreviewMs);
  check(h.presetTried == 4 && h.presetTries == 1 && h.presetLoaded < 0, "  ... resting plays 07 Djent, not kept yet",
        h.presetTried);
  p.back(t + 1000);
  check(p.screen() == P::kHome && h.presetRestores == 1, "  ... BACK: the sound from before the list", h.presetRestores);
  t = 5000;
  p.push(t);
  p.turn(1, t);
  p.tick(t + P::kPreviewMs);
  p.push(t + 1000);
  check(h.presetLoaded == 3 && p.screen() == P::kPresetList, "  ... push keeps 04, the list stays", h.presetLoaded);
  check(p.tick(t + 1000 + P::kMarqueeMs) || p.tick(t + 1000 + 2 * P::kMarqueeMs), "  ... its long name scrolls", 0);
  p.back(t + 1000);
  check(h.presetRestores == 1, "  ... BACK after a push: nothing to undo", h.presetRestores);
  p.push(t);
  p.turn(1, t);
  p.tick(t + P::kPreviewMs);
  check(p.tick(t + P::kTimeoutMs) && p.screen() == P::kHome && h.presetRestores == 2,
        "  ... a try left for 30 s: back to the sound from before", h.presetRestores);

  // Save as new, the list's next-to-last row: its name first.
  t = 50000;
  p.push(t);
  p.turn(10, t);
  p.turn(-1, t);
  p.push(t);
  check(p.screen() == P::kRename && p.renameText()[0] == 0, "Save as new (list): an empty name to fill in", p.screen());
  p.turn(3, t);  // C
  p.push(t);
  p.turn(38, t);  // l
  p.hold(t);
  check(p.screen() == P::kPresetList && h.presetNews == 1 && !std::strcmp(h.newName, "Cl"),
        "  ... hold saves it as \"Cl\", back on the list", h.presetNews);
  p.turn(-10, t);
  p.turn(5, t);
  p.push(t);
  p.back(t);
  check(p.screen() == P::kPresetList && h.presetNews == 1, "  ... BACK on the first letter cancels", h.presetNews);
  p.turn(1, t);
  p.push(t);
  check(h.rescans == 1, "  ... and Rescan, the last row", h.rescans);
  p.turn(1, t);
  check(p.irCursor() == 6, "  ... where the cursor stops", p.irCursor());
  p.back(t);
  check(p.screen() == P::kHome, "  ... BACK: home", p.screen());

  // Hold on PRE: save, after a yes; with none recalled, name a new one.
  p.hold(t);
  check(p.screen() == P::kConfirm && h.presetSaves == 0, "hold on PRE: asks before saving", p.screen());
  p.back(t);
  check(p.screen() == P::kHome && h.presetSaves == 0, "  ... BACK: no", h.presetSaves);
  p.hold(t);
  p.push(t);
  check(p.screen() == P::kHome && h.presetSaves == 1, "  ... push: saved, home", h.presetSaves);
  m.presetCurrent = -1;
  p.hold(t);
  check(p.screen() == P::kRename, "  ... none recalled: a new preset's name", p.screen());
  p.hold(t);
  check(p.screen() == P::kHome && h.presetNews == 2 && h.newName[0] == 0, "  ... kept empty: \"Preset NN\" (the firmware's)",
        h.presetNews);
  m.presetCurrent = 2;

  // The Presets page: after Gate in the menu.
  p.back(t);
  p.turn(2, t);
  check(p.menu().current()->kind == ui::Kind::kSubmenu && p.menu().current()->sub == ui::kPagePresets,
        "menu > Presets after Gate", p.menu().current()->sub);
  p.push(t);
  check(p.menu().current()->id == ui::kScreenPreset, "  ... Preset first (the list)", p.menu().current()->id);
  p.turn(1, t);
  check(p.menu().current()->id == ui::kPresetLevel, "  ... then Level", p.menu().current()->id);
  p.push(t);
  p.turn(-3, t + 2000);
  p.push(t);
  check(h.v[ui::kPresetLevel] == -3, "  ... Level: -1.5 dB (half-dB steps)", h.v[ui::kPresetLevel]);
  p.turn(1, t);
  p.push(t);
  check(p.screen() == P::kConfirm, "  ... Save asks", p.screen());
  p.push(t);
  check(p.screen() == P::kMenu && h.presetSaves == 2, "  ... yes: saved, back on the page", h.presetSaves);
  p.turn(1, t);
  p.push(t);
  check(p.screen() == P::kRename && p.renameText()[0] == 0, "  ... Save as new: its name first", p.screen());
  p.back(t);
  check(p.screen() == P::kMenu && h.presetNews == 2, "  ... BACK: nothing saved", h.presetNews);

  // Rename: the name without its number; push forward, BACK back, CONFIRM
  // deletes, push at the end adds a space.
  p.turn(1, t);
  p.push(t);
  check(p.screen() == P::kRename && !std::strcmp(p.renameText(), "Lead") && p.renamePos() == 0,
        "  ... Rename: the name without its number, cursor first", 0);
  for (int i = 0; i < 4; i++) p.push(t);
  check(p.renamePos() == 4, "  ... push moves to the end", p.renamePos());
  p.push(t);
  p.turn(1, t);  // ' ' -> 'A'
  check(!std::strcmp(p.renameText(), "Lead A") && p.renamePos() == 5, "  ... push at the end adds a space: \"Lead A\"",
        p.renamePos());
  p.back(t);
  p.back(t);
  check(p.renamePos() == 3, "  ... BACK moves back", p.renamePos());
  p.confirm(t);
  check(!std::strcmp(p.renameText(), "Lea A") && p.renamePos() == 3, "  ... CONFIRM deletes the letter under the cursor",
        p.renamePos());
  p.hold(t);
  check(p.screen() == P::kMenu && !std::strcmp(h.renamed, "Lea A"), "  ... hold keeps it", 0);
  p.push(t);  // "Lead" again (the model's name)
  for (int i = 0; i < 4; i++) p.push(t);  // past its end
  p.confirm(t);
  check(!std::strcmp(p.renameText(), "Lea") && p.renamePos() == 3, "  ... past the end, CONFIRM is a backspace",
        p.renamePos());
  for (int i = 0; i < 3; i++) p.confirm(t);
  check(p.renameText()[0] == 0 && p.renamePos() == 0 && !p.confirm(t), "  ... down to an empty name, then nothing", 0);
  p.back(t);
  check(p.screen() == P::kMenu && !std::strcmp(h.renamed, "Lea A"), "  ... BACK on the first letter cancels", 0);
  p.push(t);
  for (int i = 0; i < 6; i++) p.push(t);  // past "Lead" and two added spaces
  p.hold(t);
  check(!std::strcmp(h.renamed, "Lead"), "  ... trailing spaces dropped", 0);
  p.push(t);
  for (int i = 0; i < 40; i++) p.push(t);
  check(p.renamePos() == ui::kRenameMax - 1 && (int)std::strlen(p.renameText()) == ui::kRenameMax,
        "  ... the name stops at 24 characters", p.renamePos());
  p.confirm(t);
  check((int)std::strlen(p.renameText()) == ui::kRenameMax - 1, "  ... CONFIRM on the last letter: 23", 0);
  p.back(t + 100000);  // (a back, not a cancel: the cursor is far along)
  p.tick(t + 200000);

  // Delete, after a yes; with no preset the rows that need one are hidden.
  t = 300000;
  p.back(t);
  p.turn(2, t);
  p.push(t);
  p.turn(5, t);
  p.push(t);
  check(p.screen() == P::kConfirm, "  ... Delete asks", p.screen());
  p.push(t);
  check(h.presetDeletes == 1 && p.screen() == P::kMenu, "  ... yes: deleted", h.presetDeletes);
  h.hasPreset = false;
  p.turn(-10, t);
  p.turn(10, t);
  check(p.menu().current()->id == ui::kScreenPresetNew, "  ... none recalled: Save, Rename, Delete hidden",
        p.menu().current()->id);
}

int main() {
  testQuadrature();
  testKey();
  testMenu();
  testReverbPage();
  testDrumsPage();
  testSongRows();
  testDoublerPage();
  testFormat();
  testParams();
  testMeter();
  testCanvas();
  testHome();
  testEditorFonts();
  testPanel();
  testAmpPanel();
  testIrPreview();
  testPresetPanel();
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
