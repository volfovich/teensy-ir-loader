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
  c.clearPages(0, Canvas::kRowPages - 1);
  c.text(Canvas::kLeft, 0, left);
  if (right) c.textRight(Canvas::kW, 0, right);
  c.invertRow(0);
}

UI_COLD void drawSplash(Canvas& c, const UiModel& m) {
  char buf[24];
  c.clear();
  c.textCenter(2, UI_STR("IR LOADER"));
  c.hline(20, Canvas::kW - 21, 33);
  if (!m.sdOk) snprintf(buf, sizeof(buf), UI_STR("%s"), UI_STR("No SD card"));
  else snprintf(buf, sizeof(buf), UI_STR("%d IRs on SD"), m.irCount);
  c.textCenter(5, buf);
}

// Home row 1: the blocks in signal order, IR > DLY > REV, then the drums
// mixed in after them. A block is a box around its label, filled when on.
struct HomeBlockPos { const char* label; int x, w; };
static constexpr int kBlockY = 16, kBlockH = 13, kCursorY = 30;
static constexpr int kHomeColumnX = 54;  // the style and the beat boxes (9 characters)

UI_COLD void drawHome(Canvas& c, const UiModel& m, int block, bool editingTempo) {
  char buf[40], name[64];
  c.clear();

  // Row 0: the IR.
  if (!m.sdOk) snprintf(buf, sizeof(buf), UI_STR("%s"), UI_STR("No SD card"));
  else if (m.irCount == 0) snprintf(buf, sizeof(buf), UI_STR("%s"), UI_STR("No IRs on SD"));
  else if (m.irCurrent < 0 || !m.irPath) snprintf(buf, sizeof(buf), UI_STR("%s"), UI_STR("No IR loaded"));
  else {
    irDisplayName(m.irPath(m.irCurrent), name, sizeof(name));
    snprintf(buf, sizeof(buf), m.bypass ? UI_STR("BYPASS %s") : UI_STR("%s"), name);
  }
  c.text(Canvas::kLeft, 0, buf);

  // Row 1: the blocks, the cursor under one.
  const HomeBlockPos blocks[kBlockTempo] = {
      {UI_STR("IR"), 0, 22},
      {UI_STR("DLY"), 27, 30},
      {UI_STR("REV"), 62, 30},
      {UI_STR("DRM"), 97, 30},
  };
  const bool on[kBlockTempo] = {!m.bypass, m.delayOn, m.reverbOn, m.drumsPlaying};
  for (int i = 0; i < kBlockTempo; i++) {
    const HomeBlockPos& b = blocks[i];
    c.text(b.x + (b.w - Canvas::width(b.label)) / 2, 2, b.label);
    // The font's blank rows above and below the capitals are inside the
    // box; clear what the glyph cells put outside it.
    c.fill(b.x, kBlockY + kBlockH, b.w, 32 - kBlockY - kBlockH, false);
    if (on[i]) c.invert(b.x, kBlockY, b.w, kBlockH);
    else c.frame(b.x, kBlockY, b.w, kBlockH);
  }
  if (block >= 0 && block < kBlockTempo) c.fill(blocks[block].x, kCursorY, blocks[block].w, 2, true);

  // Rows 2-3: the tempo big (its digits' ink is y 38-57), and in a column
  // beside it the style and a box per beat, the one sounding filled (all
  // hollow when stopped, so nothing moves when the drums start).
  snprintf(buf, sizeof(buf), UI_STR("%d"), m.drumTempo);
  const int tw = c.text(0, 4, buf, Font::kBig);
  if (editingTempo) c.invert(0, 34, tw, 26);
  else if (block == kBlockTempo) c.fill(0, 60, tw, 2, true);
  snprintf(buf, sizeof(buf), UI_STR("%.9s"), m.drumStyle ? m.drumStyle : UI_STR(""));
  c.text(kHomeColumnX, 4, buf);
  const int beats = m.drumBeats > 7 ? 7 : m.drumBeats, size = 8, step = 10, y = 50;
  for (int i = 0; i < beats; i++) {
    const int bx = kHomeColumnX + i * step;
    if (m.drumsPlaying && i == m.drumBeat) c.fill(bx, y, size, size, true);
    else c.frame(bx, y, size, size);
  }
}

UI_COLD void drawIrBrowser(Canvas& c, const UiModel& m, int cursor, int top, int marquee) {
  char buf[40], name[64];
  c.clear();
  if (m.irCurrent >= 0) snprintf(buf, sizeof(buf), UI_STR("%d/%d"), m.irCurrent + 1, m.irCount);
  else snprintf(buf, sizeof(buf), UI_STR("-/%d"), m.irCount);
  titleBar(c, UI_STR("IR"), buf);
  for (int row = 0; row < kIrBrowserRows; row++) {
    const int n = top + row, page = (row + 1) * Canvas::kRowPages;
    if (n > m.irCount) break;
    if (n == m.irCount) {
      c.text(Canvas::kLeft, page, UI_STR("Rescan SD card"));
    } else {
      irDisplayName(m.irPath ? m.irPath(n) : UI_STR(""), name, sizeof(name));
      // "NN " then as much of the name as fits before the loaded mark.
      const int room = kIrBrowserNameChars;
      const int len = (int)strlen(name);
      int off = n == cursor ? marquee : 0;
      if (off > len - room) off = len - room > 0 ? len - room : 0;
      snprintf(buf, sizeof(buf), UI_STR("%02d %.*s"), n + 1, room, name + off);
      c.text(Canvas::kLeft, page, buf);
      if (n == m.irCurrent) c.textRight(Canvas::kW, page, UI_STR("*"));
    }
    if (n == cursor) c.invertRow(page);
  }
}

UI_COLD void drawStatus(Canvas& c, const UiModel& m) {
  char buf[32];
  c.clear();
  titleBar(c, UI_STR("STATUS"), nullptr);
  snprintf(buf, sizeof(buf), UI_STR("CPU %d%% max %d%%"), (int)(m.cpu + 0.5f), (int)(m.cpuMax + 0.5f));
  c.text(Canvas::kLeft, 2, buf);
  snprintf(buf, sizeof(buf), UI_STR("Clips %lu"), (unsigned long)m.clips);
  c.text(Canvas::kLeft, 4, buf);
  snprintf(buf, sizeof(buf), UI_STR("Blocks %d/%d"), m.blocksUsed, m.blocksMax);
  c.text(Canvas::kLeft, 6, buf);
}

UI_COLD void drawMeterScreen(Canvas& c, const UiModel& m) {
  char buf[24];
  c.clear();
  if (m.in.clipping(m.nowMs) || m.out.clipping(m.nowMs)) snprintf(buf, sizeof(buf), UI_STR("%s"), UI_STR("CLIP"));
  else snprintf(buf, sizeof(buf), UI_STR("clips %lu"), (unsigned long)m.clips);
  titleBar(c, UI_STR("METER"), buf);
  c.text(Canvas::kLeft, 2, UI_STR("IN"));
  drawMeterBar(c, 32, 2, m.in, true);
  c.text(Canvas::kLeft, 4, UI_STR("OUT"));
  drawMeterBar(c, 32, 4, m.out, false);
  snprintf(buf, sizeof(buf), UI_STR("Sens %d"), m.lineIn);
  c.text(Canvas::kLeft, 6, buf);
  if (m.in.peakDb <= MeterState::kFloorDb) snprintf(buf, sizeof(buf), UI_STR("%s"), UI_STR("pk <-48"));
  else snprintf(buf, sizeof(buf), UI_STR("pk %.1f"), m.in.peakDb);
  c.textRight(Canvas::kW, 6, buf);
}

}  // namespace ui
