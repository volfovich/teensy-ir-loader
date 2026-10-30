#include "Screens.h"

#include <math.h>
#include <stdio.h>
#include <string.h>


namespace ui {

void irDisplayName(const char* path, char* out, int n) {
  const char* name = strrchr(path, '/');
  name = name ? name + 1 : path;
  int len = (int)strlen(name);
  if (len > 4 && name[len - 4] == '.') len -= 4;
  if (len > n - 1) len = n - 1;
  memcpy(out, name, len);
  out[len] = 0;
}

void ampDisplayName(const char* path, char* out, int n) {
  irDisplayName(path, out, n);
  for (char* p = out; *p; p++)
    if (*p == '_') *p = ' ';
}

int fileCount(const UiModel& m, FileList list) { return list == FileList::kAmp ? m.ampCount : m.irCount; }
int fileCurrent(const UiModel& m, FileList list) { return list == FileList::kAmp ? m.ampCurrent : m.irCurrent; }

void fileName(const UiModel& m, FileList list, int i, char* out, int n) {
  const char* (*path)(int) = list == FileList::kAmp ? m.ampPath : m.irPath;
  if (!path) out[0] = 0;
  else if (list == FileList::kAmp) ampDisplayName(path(i), out, n);
  else irDisplayName(path(i), out, n);
}

// The length of the whole words (up to and including a space) that every
// name in the list starts with; 0 for fewer than two names.
static int commonPrefix(const UiModel& m, FileList list) {
  const int count = fileCount(m, list);
  if (count < 2) return 0;
  char first[64], other[64];
  fileName(m, list, 0, first, sizeof(first));
  int len = (int)strlen(first);
  for (int i = 1; i < count && len > 0; i++) {
    fileName(m, list, i, other, sizeof(other));
    int k = 0;
    while (k < len && first[k] && first[k] == other[k]) k++;
    len = k;
  }
  while (len > 0 && first[len - 1] != ' ') len--;  // whole words only
  return len;
}

void listName(const UiModel& m, FileList list, int i, char* out, int n) {
  char name[64];
  fileName(m, list, i, name, sizeof(name));
  const int skip = commonPrefix(m, list);
  snprintf(out, n, "%s", name[skip] ? name + skip : name);
}

void homeName(const UiModel& m, int block, char* out, int n) {
  char name[64];
  if (block == kBlockAmp) {
    if (!m.sdOk) snprintf(out, n, "%s", "No SD card");
    else if (m.ampCount == 0) snprintf(out, n, "%s", "No amps on SD");
    else if (m.ampCurrent < 0 || !m.ampPath) snprintf(out, n, "%s", "No amp loaded");
    else fileName(m, FileList::kAmp, m.ampCurrent, out, n);
    return;
  }
  if (!m.sdOk) snprintf(out, n, "%s", "No SD card");
  else if (m.irCount == 0) snprintf(out, n, "%s", "No IRs on SD");
  else if (m.irCurrent < 0 || !m.irPath) snprintf(out, n, "%s", "No IR loaded");
  else {
    fileName(m, FileList::kIR, m.irCurrent, name, sizeof(name));
    snprintf(out, n, m.bypass ? "BYPASS %s" : "%s", name);
  }
}

static void titleBar(Canvas& c, const char* left, const char* right) {
  c.clearPages(0, Canvas::kRowPages - 1);
  c.text(Canvas::kLeft, 0, left);
  if (right) c.textRight(Canvas::kW, 0, right);
  c.invertRow(0);
}

void drawSplash(Canvas& c, const UiModel& m) {
  char buf[24];
  c.clear();
  c.textCenter(2, "IR LOADER");
  c.hline(20, Canvas::kW - 21, 33);
  if (m.booting) return;
  if (!m.sdOk) snprintf(buf, sizeof(buf), "%s", "No SD card");
  else snprintf(buf, sizeof(buf), "%d IRs on SD", m.irCount);
  c.textCenter(5, buf);
}

// Home row 1: the blocks in signal order, AMP > IR > DLY > REV, then the
// drums mixed in after them. A block is its label in a box, filled when on;
// when off, only the box's top and bottom lines (its sides would touch the
// letters: five blocks leave one pixel each side).
struct HomeBlockPos { const char* label; int x, w; };
static constexpr int kBlockY = 16, kBlockH = 13, kCursorY = 30;
static constexpr int kHomeColumnX = 54;  // the style and the beat boxes (9 characters)
static constexpr HomeBlockPos kBlocks[kBlockTempo] = {
    {"AMP", 0, 25}, {"IR", 28, 17}, {"DLY", 48, 25}, {"REV", 76, 25}, {"DRM", 103, 25},
};

void drawHome(Canvas& c, const UiModel& m, int block, bool editingTempo, int marquee) {
  char buf[40], name[64];
  c.clear();

  // Row 0: the IR, or the amp model with the cursor on AMP.
  homeName(m, block, name, sizeof(name));
  const int len = (int)strlen(name);
  int off = block == kBlockAmp ? marquee : 0;
  if (off > len - kHomeNameChars) off = len - kHomeNameChars > 0 ? len - kHomeNameChars : 0;
  snprintf(buf, sizeof(buf), "%.*s", kHomeNameChars, name + off);
  c.text(Canvas::kLeft, 0, buf);

  // Row 1: the blocks, the cursor under one.
  const HomeBlockPos* blocks = kBlocks;
  const bool on[kBlockTempo] = {m.ampOn, !m.bypass, m.delayOn, m.reverbOn, m.drumsPlaying};
  for (int i = 0; i < kBlockTempo; i++) {
    const HomeBlockPos& b = blocks[i];
    // Centered by the ink: a glyph's last column is blank.
    c.text(b.x + (b.w - Canvas::width(b.label) + 1) / 2, 2, b.label);
    // The font's blank rows above and below the capitals are inside the
    // box; clear what the glyph cells put outside it.
    c.fill(b.x, kBlockY + kBlockH, b.w, 32 - kBlockY - kBlockH, false);
    if (on[i]) {
      c.invert(b.x, kBlockY, b.w, kBlockH);
    } else {
      c.hline(b.x, b.x + b.w - 1, kBlockY);
      c.hline(b.x, b.x + b.w - 1, kBlockY + kBlockH - 1);
    }
  }
  if (block >= 0 && block < kBlockTempo) c.fill(blocks[block].x, kCursorY, blocks[block].w, 2, true);

  // Rows 2-3: the tempo big (its digits' ink is y 38-57), and in a column
  // beside it the style and a box per beat, the one sounding filled (all
  // hollow when stopped, so nothing moves when the drums start).
  snprintf(buf, sizeof(buf), "%d", m.drumTempo);
  const int tw = c.text(0, 4, buf, Font::kBig);
  if (editingTempo) c.invert(0, 34, tw, 26);
  else if (block == kBlockTempo) c.fill(0, 60, tw, 2, true);
  snprintf(buf, sizeof(buf), "%.9s", m.drumStyle ? m.drumStyle : "");
  c.text(kHomeColumnX, 4, buf);
  const int beats = m.drumBeats > 7 ? 7 : m.drumBeats, size = 8, step = 10, y = 50;
  for (int i = 0; i < beats; i++) {
    const int bx = kHomeColumnX + i * step;
    if (m.drumsPlaying && i == m.drumBeat) c.fill(bx, y, size, size, true);
    else c.frame(bx, y, size, size);
  }
}

void drawFileList(Canvas& c, const UiModel& m, FileList list, int cursor, int top, int marquee) {
  char buf[40], name[64];
  c.clear();
  const int count = fileCount(m, list), current = fileCurrent(m, list);
  if (current >= 0) snprintf(buf, sizeof(buf), "%d/%d", current + 1, count);
  else snprintf(buf, sizeof(buf), "-/%d", count);
  titleBar(c, list == FileList::kAmp ? "AMP" : "IR", buf);
  const int skip = commonPrefix(m, list);
  for (int row = 0; row < kIrBrowserRows; row++) {
    const int n = top + row, page = (row + 1) * Canvas::kRowPages;
    if (n > count) break;
    if (n == count) {
      c.text(Canvas::kLeft, page, "Rescan SD card");
    } else {
      fileName(m, list, n, name, sizeof(name));
      if (name[skip]) memmove(name, name + skip, strlen(name + skip) + 1);
      // "NN " then the name, as much as fits before the loaded mark: the
      // cursor row scrolls it (marquee); the others keep both ends, joined
      // by '~', since names often differ only at the end ("... Gain 5").
      const int room = kIrBrowserNameChars;
      const int len = (int)strlen(name);
      if (n == cursor || len <= room) {
        int off = n == cursor ? marquee : 0;
        if (off > len - room) off = len - room > 0 ? len - room : 0;
        snprintf(buf, sizeof(buf), "%02d %.*s", n + 1, room, name + off);
      } else {
        const int tail = room / 2, head = room - 1 - tail;
        snprintf(buf, sizeof(buf), "%02d %.*s~%s", n + 1, head, name, name + len - tail);
      }
      c.text(Canvas::kLeft, page, buf);
      if (n == current) c.textRight(Canvas::kW, page, "*");
    }
    if (n == cursor) c.invertRow(page);
  }
}

void drawStatus(Canvas& c, const UiModel& m) {
  char buf[32];
  c.clear();
  titleBar(c, "STATUS", nullptr);
  snprintf(buf, sizeof(buf), "CPU %d%% max %d%%", (int)(m.cpu + 0.5f), (int)(m.cpuMax + 0.5f));
  c.text(Canvas::kLeft, 2, buf);
  snprintf(buf, sizeof(buf), "Clips %lu", (unsigned long)m.clips);
  c.text(Canvas::kLeft, 4, buf);
  snprintf(buf, sizeof(buf), "Blocks %d/%d", m.blocksUsed, m.blocksMax);
  c.text(Canvas::kLeft, 6, buf);
}

void drawMeterScreen(Canvas& c, const UiModel& m) {
  char buf[24];
  c.clear();
  if (m.in.clipping(m.nowMs) || m.out.clipping(m.nowMs)) snprintf(buf, sizeof(buf), "%s", "CLIP");
  else snprintf(buf, sizeof(buf), "clips %lu", (unsigned long)m.clips);
  titleBar(c, "METER", buf);
  c.text(Canvas::kLeft, 2, "IN");
  drawMeterBar(c, 32, 2, m.in, true);
  c.text(Canvas::kLeft, 4, "OUT");
  drawMeterBar(c, 32, 4, m.out, false);
  snprintf(buf, sizeof(buf), "Sens %d", m.lineIn);
  c.text(Canvas::kLeft, 6, buf);
  if (m.in.peakDb <= MeterState::kFloorDb) snprintf(buf, sizeof(buf), "%s", "pk <-48");
  else snprintf(buf, sizeof(buf), "pk %.1f", m.in.peakDb);
  c.textRight(Canvas::kW, 6, buf);
}

void drawTuner(Canvas& c, const UiModel& m) {
  static const char* const kNames[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
  char buf[24];
  c.clear();
  if (m.tunerValid) snprintf(buf, sizeof(buf), "%.1f Hz", m.tunerHz);
  else snprintf(buf, sizeof(buf), "%s", "A=440");
  titleBar(c, "TUNER", buf);
  // The bar: a line, ticks at -50, -25, +25, +50 and the centre taller.
  const int cx = Canvas::kW / 2, y = 57, half = 60;
  c.hline(cx - half, cx + half, y);
  for (int k = -2; k <= 2; k++) c.vline(cx + k * half / 2, y - (k ? 2 : 6), y + (k ? 2 : 6));
  if (!m.tunerValid) {
    c.textCenter(2, "-", Font::kBig);
    return;
  }
  // The note big (the octave small after it), the cents right.
  const int nw = Canvas::width(kNames[m.tunerNote], Font::kBig);
  const int x0 = cx - (nw + Canvas::kCharW) / 2;
  c.text(x0, 2, kNames[m.tunerNote], Font::kBig);
  snprintf(buf, sizeof(buf), "%d", m.tunerOctave);
  c.text(x0 + nw, 4, buf);
  const bool inTune = m.tunerCents > -kTunerInTuneCents && m.tunerCents < kTunerInTuneCents;
  if (inTune) c.invert(x0 - 4, 18, nw + Canvas::kCharW + 8, 30);
  snprintf(buf, sizeof(buf), "%+d", (int)lroundf(m.tunerCents));
  c.textRight(Canvas::kW, 4, buf);
  if (!inTune) c.text(Canvas::kLeft, 4, m.tunerCents < 0 ? ">" : "<");  // which way to turn
  // The needle: 3 px wide, the bar's height.
  float cents = m.tunerCents < -50 ? -50 : (m.tunerCents > 50 ? 50 : m.tunerCents);
  const int nx = cx + (int)lroundf(cents * (float)half / 50.0f);
  c.fill(nx - 1, y - 8, 3, 15, true);
}

void drawBusy(Canvas& c, const char* what, const char* name) {
  char buf[24];
  c.clear();
  titleBar(c, what, nullptr);
  // The name on up to two rows, broken at a space when one is near.
  const int len = (int)strlen(name), cols = Canvas::kCols;
  int cut = len <= cols ? len : cols;
  if (len > cols)
    for (int i = cols; i > cols / 2; i--)
      if (name[i] == ' ') {
        cut = i;
        break;
      }
  snprintf(buf, sizeof(buf), "%.*s", cut, name);
  c.textCenter(3, buf);
  const char* rest = name + cut;
  while (*rest == ' ') rest++;
  snprintf(buf, sizeof(buf), "%.*s", cols, rest);
  if (*buf) c.textCenter(5, buf);
}

}  // namespace ui
