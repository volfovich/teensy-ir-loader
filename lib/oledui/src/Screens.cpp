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

// A home-screen badge: inverted box when on, dotted underline when off.
static UI_COLD void badge(Canvas& c, int x, const char* s, bool on) {
  c.text(x, 0, s);
  const int w = Canvas::width(s);
  if (on) c.invertText(x - 1, 0, w + 1);
  else c.dots(x, x + w - 2, 7);
}

UI_COLD void drawHome(Canvas& c, const UiModel& m, bool blink) {
  char buf[40], name[64];
  c.clearPages(0, 5);

  // Row 0: number / count and the badges.
  if (m.irShown >= 0) snprintf(buf, sizeof(buf), UI_STR("%d/%d"), m.irShown + 1, m.irCount);
  else snprintf(buf, sizeof(buf), UI_STR("-/%d"), m.irCount);
  const bool browsing = m.irShown != m.irCurrent;
  const int end = c.text(0, 0, buf);
  if (browsing && blink) c.invertText(0, 0, end - 1);
  badge(c, 62, UI_STR("BYP"), m.bypass);
  badge(c, 86, UI_STR("DLY"), m.delayOn);
  badge(c, 110, UI_STR("REV"), m.reverbOn);

  // Rows 1-3: the IR name, the first word(s) big, the rest small.
  if (!m.sdOk || m.irCount == 0 || m.irShown < 0 || !m.irPath) {
    c.text(0, 1, !m.sdOk ? UI_STR("No SD card") : (m.irCount == 0 ? UI_STR("No IRs") : UI_STR("No IR")), true);
    if (m.sdOk && m.irCount == 0) c.text(0, 3, UI_STR("in / or /IR"));
  } else {
    irDisplayName(m.irPath(m.irShown), name, sizeof(name));
    const int bigChars = Canvas::kW / Canvas::kBigCharW;  // 10
    int cut = (int)strlen(name);
    if (cut > bigChars) {
      cut = bigChars;
      for (int i = bigChars; i > 0; i--) {
        if (name[i] == ' ') { cut = i; break; }
      }
    }
    char first[16];
    memcpy(first, name, cut);
    first[cut] = 0;
    c.text(0, 1, first, true);
    const char* rest = name + cut;
    while (*rest == ' ') rest++;
    c.text(0, 3, rest);
  }

  // Row 4: effects, or the browsing hint.
  if (browsing) {
    c.text(0, 4, UI_STR("loads when you stop"));
  } else {
    if (m.delayOn)
      snprintf(buf, sizeof(buf), UI_STR("DLY %dms %s"), m.delayTimeMs, m.studioDelay ? UI_STR("80s") : UI_STR("PP"));
    else
      snprintf(buf, sizeof(buf), UI_STR("%s"), UI_STR("DLY off"));
    c.text(0, 4, buf);
    if (m.reverbOn) snprintf(buf, sizeof(buf), UI_STR("REV %d%%"), m.reverbMix);
    else snprintf(buf, sizeof(buf), UI_STR("%s"), UI_STR("REV off"));
    c.textRight(Canvas::kW, 4, buf);
  }

  // Row 5: the drums while they play.
  if (m.drumsPlaying) {
    snprintf(buf, sizeof(buf), UI_STR("DRM %.10s %d"), m.drumStyle, m.drumTempo);
    c.text(0, 5, buf);
    if (m.drumsCountingIn) snprintf(buf, sizeof(buf), UI_STR("%s"), UI_STR("count"));
    else snprintf(buf, sizeof(buf), UI_STR("bar %d"), m.drumBar);
    c.textRight(Canvas::kW, 5, buf);
  }
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
      const int room = (Canvas::kW - 12 - Canvas::kCharW) / Canvas::kCharW - 3;  // 15
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
