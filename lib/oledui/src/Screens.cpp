#include "Screens.h"

#include <stdio.h>
#include <string.h>

#include "ui_port.h"

namespace ui {

UI_COLD void irDisplayName(const char* path, char* out, int n) {
  const char* name = strrchr(path, '/');
  name = name ? name + 1 : path;
  int len = (int)strlen(name);
  if (len > 4 && name[len - 4] == '.') len -= 4;
  if (len > n - 1) len = n - 1;
  memcpy(out, name, len);
  out[len] = 0;
}

static UI_COLD void titleBar(Canvas& c, const char* left, const char* right) {
  c.clearPages(0, 0);
  c.text(1, 0, left);
  if (right) c.textRight(Canvas::kW - 1, 0, right);
  c.invertPage(0);
}

UI_COLD void drawSplash(Canvas& c, const UiModel& m) {
  char buf[24];
  c.clear();
  c.textCenter(0, UI_STR("IR LOADER"), true);
  c.hline(14, Canvas::kW - 15, 19);
  c.textCenter(3, UI_STR("Teensy 4.0"));
  snprintf(buf, sizeof(buf), UI_STR("block %d  IR %d"), m.blockSize, m.irMaxLength);
  c.textCenter(4, buf);
  if (!m.sdOk) snprintf(buf, sizeof(buf), UI_STR("%s"), UI_STR("No SD card"));
  else snprintf(buf, sizeof(buf), UI_STR("%d IRs on SD"), m.irCount);
  c.textCenter(6, buf);
}

// A home-screen block on page 0: inverted box when on, dotted underline
// when off.
static UI_COLD void badge(Canvas& c, int x, const char* s, bool on) {
  c.text(x, 0, s);
  const int w = Canvas::width(s);
  if (on) c.invertText(x - 1, 0, w + 1);
  else c.dots(x, x + w - 2, 7);
}

// An arrow on page 0 from x0 to x1 (signal flow between two blocks).
static UI_COLD void arrow(Canvas& c, int x0, int x1) {
  c.hline(x0, x1, 3);
  c.vline(x1 - 1, 2, 4);
  c.vline(x1 - 2, 1, 5);
}

// Page 2: the selected block in a line.
static UI_COLD void blockDetail(Canvas& c, const UiModel& m, int block) {
  char buf[48], name[64];
  const char* off = UI_STR("off ");
  const char* on = UI_STR("");
  switch (block) {
    case kBlockIR:
      if (!m.sdOk) snprintf(buf, sizeof(buf), UI_STR("%s"), UI_STR("No SD card"));
      else if (m.irCount == 0) snprintf(buf, sizeof(buf), UI_STR("%s"), UI_STR("No IRs in / or /IR"));
      else if (m.irCurrent < 0 || !m.irPath) snprintf(buf, sizeof(buf), UI_STR("-/%d no IR"), m.irCount);
      else {
        irDisplayName(m.irPath(m.irCurrent), name, sizeof(name));
        if (m.bypass) snprintf(buf, sizeof(buf), UI_STR("BYPASS %s"), name);
        else snprintf(buf, sizeof(buf), UI_STR("%d/%d %s"), m.irCurrent + 1, m.irCount, name);
      }
      break;
    case kBlockDelay:
      snprintf(buf, sizeof(buf), UI_STR("%s%dms %s mix %d%%"), m.delayOn ? on : off, m.delayTimeMs,
               m.studioDelay ? UI_STR("80s") : UI_STR("PP"), m.delayMix);
      break;
    case kBlockReverb:
      snprintf(buf, sizeof(buf), UI_STR("%s%s mix %d%%"), m.reverbOn ? on : off,
               !m.reverb80 ? UI_STR("plate") : (m.reverbGated ? UI_STR("80s gated") : UI_STR("80s hall")), m.reverbMix);
      break;
    default:
      snprintf(buf, sizeof(buf), UI_STR("kit %s  vol %d%%"), m.drumKit, m.drumVolume);
      break;
  }
  c.text(0, 2, buf);
}

// Page 5: a box per beat (the one sounding filled), and for a song its
// progress with the loop's ends.
static UI_COLD void beatRow(Canvas& c, const UiModel& m) {
  const int y = 5 * 8 + 1, size = 5;
  const int beats = m.drumBeats > 7 ? 7 : m.drumBeats;
  for (int i = 0; i < beats; i++) {
    const int x = 1 + i * 7;
    if (m.drumsPlaying && i == m.drumBeat) c.fill(x, y, size, size, true);
    else c.frame(x, y, size, size);
  }
  if (m.drumSong && m.drumSongBars > 0) {
    const int x0 = 60, w = Canvas::kW - x0, bars = m.drumSongBars;
    c.frame(x0, y, w, size);
    if (m.drumsPlaying) c.fill(x0 + 1, y + 1, (w - 2) * m.drumSongBar / bars, size - 2, true);
    if (m.drumLoopFrom > 0) {
      c.vline(x0 + (m.drumLoopFrom - 1) * w / bars, 5 * 8, 5 * 8 + 7);
      c.vline(x0 + m.drumLoopTo * w / bars - 1, 5 * 8, 5 * 8 + 7);
    }
  } else if (m.trainerOn) {
    char buf[24];
    snprintf(buf, sizeof(buf), UI_STR("to %d BPM"), m.trainerTarget);
    c.textRight(Canvas::kW, 5, buf);
  }
}

UI_COLD void drawHome(Canvas& c, const UiModel& m, int block) {
  char buf[40];
  c.clearPages(0, 5);

  // Page 0: the blocks, IR > DLY > REV + DRM; page 1: the cursor under one.
  struct Block { const char* label; int x; bool on; };
  const Block blocks[kHomeBlocks] = {
      {UI_STR("IR"), 9, !m.bypass},
      {UI_STR("DLY"), 36, m.delayOn},
      {UI_STR("REV"), 69, m.reverbOn},
      {UI_STR("DRM"), 102, m.drumsPlaying},
  };
  for (int i = 0; i < kHomeBlocks; i++) badge(c, blocks[i].x, blocks[i].label, blocks[i].on);
  arrow(c, 23, 32);
  arrow(c, 56, 65);
  c.text(91, 0, UI_STR("+"));
  if (block >= 0 && block < kHomeBlocks) {
    const Block& b = blocks[block];
    c.fill(b.x - 1, 9, Canvas::width(b.label) + 1, 2, true);
  }

  blockDetail(c, m, block);

  // Pages 3-4: the tempo big; the style or song, and where it is.
  snprintf(buf, sizeof(buf), UI_STR("%d"), m.drumTempo);
  c.text(0, 3, buf, true);
  snprintf(buf, sizeof(buf), UI_STR("%.14s"), m.drumSong ? m.drumSong : m.drumStyle);
  c.text(40, 3, buf);
  c.text(40, 4, UI_STR("BPM"));
  if (!m.drumsPlaying) snprintf(buf, sizeof(buf), UI_STR("%s"), UI_STR("stopped"));
  else if (m.drumsCountingIn) snprintf(buf, sizeof(buf), UI_STR("%s"), UI_STR("count-in"));
  else if (m.drumSong) snprintf(buf, sizeof(buf), UI_STR("bar %d/%d"), m.drumSongBar, m.drumSongBars);
  else snprintf(buf, sizeof(buf), UI_STR("bar %d"), m.drumBar);
  c.text(70, 4, buf);

  beatRow(c, m);
}

UI_COLD void drawHomeMeters(Canvas& c, const UiModel& m) {
  drawRowMeter(c, 6, UI_STR("IN"), m.in, m.nowMs);
  drawRowMeter(c, 7, UI_STR("OUT"), m.out, m.nowMs);
}

UI_COLD void drawIrBrowser(Canvas& c, const UiModel& m, int cursor, int top, int marquee) {
  char buf[40], name[64];
  c.clear();
  if (m.irCurrent >= 0) snprintf(buf, sizeof(buf), UI_STR("%d/%d"), m.irCurrent + 1, m.irCount);
  else snprintf(buf, sizeof(buf), UI_STR("-/%d"), m.irCount);
  titleBar(c, UI_STR("IR"), buf);
  for (int row = 0; row < kIrBrowserRows; row++) {
    const int n = top + row, page = row + 1;
    if (n > m.irCount) break;
    if (n == cursor) c.text(0, page, UI_STR(">"));
    if (n == m.irCount) {
      c.text(12, page, UI_STR("Rescan SD card"));
    } else {
      irDisplayName(m.irPath ? m.irPath(n) : UI_STR(""), name, sizeof(name));
      // "NN " then as much of the name as fits before the loaded mark.
      const int room = kIrBrowserNameChars;
      const int len = (int)strlen(name);
      int off = n == cursor ? marquee : 0;
      if (off > len - room) off = len - room > 0 ? len - room : 0;
      snprintf(buf, sizeof(buf), UI_STR("%02d %.*s"), n + 1, room, name + off);
      c.text(12, page, buf);
      if (n == m.irCurrent) c.text(Canvas::kW - Canvas::kCharW, page, UI_STR("*"));
    }
    if (n == cursor) c.invertPage(page);
  }
}

UI_COLD void drawStatus(Canvas& c, const UiModel& m) {
  char buf[32];
  c.clear();
  titleBar(c, UI_STR("STATUS"), nullptr);
  snprintf(buf, sizeof(buf), UI_STR("CPU %.1f%% max %.1f%%"), m.cpu, m.cpuMax);
  c.text(0, 1, buf);
  snprintf(buf, sizeof(buf), UI_STR("dly %.1f%%  rev %.1f%%"), m.cpuDelay, m.cpuReverb);
  c.text(0, 2, buf);
  snprintf(buf, sizeof(buf), UI_STR("blocks %d/%d  clips %lu"), m.blocksUsed, m.blocksMax, (unsigned long)m.clips);
  c.text(0, 3, buf);
  snprintf(buf, sizeof(buf), UI_STR("latency %.2fms+codec"), m.latencyMs);
  c.text(0, 4, buf);
  snprintf(buf, sizeof(buf), UI_STR("%d x %d partitions"), m.partitions, m.blockSize);
  c.text(0, 5, buf);
  snprintf(buf, sizeof(buf), UI_STR("gain %.1fdB  hp %d%%"), m.gainDb, m.hpPercent);
  c.text(0, 6, buf);
  snprintf(buf, sizeof(buf), UI_STR("line in %d  out %d"), m.lineIn, m.lineOut);
  c.text(0, 7, buf);
}

// "-7.5" or "<-48" for a meter reading.
static UI_COLD void dbText(float db, char* buf, int n) {
  if (db <= MeterState::kFloorDb) snprintf(buf, n, UI_STR("%s"), UI_STR("<-48"));
  else snprintf(buf, n, UI_STR("%.1f"), db);
}

static UI_COLD void meterBlock(Canvas& c, int page, const char* label, const MeterState& s, bool zone) {
  char lv[12], pk[12], buf[24];
  dbText(s.levelDb, lv, sizeof(lv));
  dbText(s.peakDb, pk, sizeof(pk));
  c.text(0, page, label);
  c.textRight(72, page, lv);
  snprintf(buf, sizeof(buf), UI_STR("pk %s"), pk);
  c.textRight(Canvas::kW, page, buf);
  drawWideMeter(c, page + 1, s, zone);
}

UI_COLD void drawMeterScreen(Canvas& c, const UiModel& m) {
  char buf[24];
  c.clear();
  snprintf(buf, sizeof(buf), UI_STR("clips %lu"), (unsigned long)m.clips);
  titleBar(c, UI_STR("METER"), buf);
  meterBlock(c, 1, UI_STR("IN"), m.in, true);
  meterBlock(c, 3, UI_STR("OUT"), m.out, false);
  if (m.in.clipping(m.nowMs) || m.out.clipping(m.nowMs)) {
    c.textCenter(5, UI_STR("CLIP"));
    c.invertText(52, 5, 24);
  }
  snprintf(buf, sizeof(buf), UI_STR("Line-in sens %d"), m.lineIn);
  c.text(0, 6, buf);
  c.textRight(Canvas::kW, 6, UI_STR("<turn>"));
  c.text(0, 7, UI_STR("ok=reset"));
  c.textRight(Canvas::kW, 7, UI_STR("back=exit"));
}

}  // namespace ui
