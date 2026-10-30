#include "Menu.h"

#include <stdio.h>
#include <string.h>

namespace ui {

// ---- The menu tree (DISPLAY.md, "Menu tree") --------------------------------
//
// A setting's row takes its label, range, step, unit and names from the
// parameter table (ParamDefs.cpp); only submenus and screens name themselves.

using K = Kind;
static constexpr uint8_t kAlways = kNoItem;

#define SETTING(id) {nullptr, K::kSetting, id, 0, kAlways}
#define SETTING_IF(id, cond) {nullptr, K::kSetting, id, 0, cond}

static const MenuItem kTop[] = {
    {"Amp", K::kSubmenu, kAmpOn, kPageAmp, kAlways},
    {"IR", K::kScreen, kScreenIR, 0, kAlways},
    {"Doubler", K::kSubmenu, kDblOn, kPageDoubler, kAlways},
    {"Delay", K::kSubmenu, kDelayOn, kPageDelay, kAlways},
    {"Reverb", K::kSubmenu, kReverbOn, kPageReverb, kAlways},
    {"Drums", K::kSubmenu, kDrumsPlay, kPageDrums, kAlways},
    {"Levels", K::kSubmenu, kNoItem, kPageLevels, kAlways},
    SETTING(kBypass),
    {"Tuner", K::kScreen, kScreenTuner, 0, kAlways},
    {"Meter", K::kScreen, kScreenMeter, 0, kAlways},
    {"Status", K::kScreen, kScreenStatus, 0, kAlways},
};

static const MenuItem kDelayItems[] = {
    SETTING(kDelayOn), SETTING(kDelayMix), SETTING(kDelayTime), SETTING(kDelayRepeats), SETTING(kDelayTone),
    SETTING(kDelayMod), SETTING(kDelayDuck), SETTING(kDelayDrive), SETTING(kDelayEra),
};

// Mod shows for the hall, Gate for the gated mode.
static const MenuItem kReverbItems[] = {
    SETTING(kReverbOn), SETTING(kReverbMode), SETTING(kReverbMix), SETTING(kReverbSize), SETTING(kReverbTone),
    SETTING(kReverbPreDelay), SETTING_IF(kReverbMod, kIsReverbHall), SETTING(kReverbEra),
    SETTING_IF(kReverbGate, kIsReverbGated),
};

static const MenuItem kLevelItems[] = {
    SETTING(kOutGain), SETTING(kLineIn), SETTING(kLineOut), SETTING(kHeadphones),
};

// The style's, kit's and song's range ends come from MenuHost::limits (how
// many there are), their names from MenuHost::valueText; so do the loop's
// bars (the song's length; "off" for from = 0).
static const MenuItem kDrumItems[] = {
    SETTING(kDrumsPlay),
    {"Bass", K::kSubmenu, kBassOn, kPageBass, kAlways},
    SETTING(kDrumStyle),
    SETTING(kDrumSong),
    SETTING_IF(kDrumLoopFrom, kIsDrumSong),
    SETTING_IF(kDrumLoopTo, kIsDrumSong),
    SETTING(kDrumKit),
    SETTING(kDrumCrash),
    SETTING(kDrumTempo),
    SETTING(kDrumVolume),
    SETTING(kDrumComp),
    SETTING(kDrumCountIn),
    {"Trainer", K::kSubmenu, kTrainerOn, kPageTrainer, kAlways},
};

// Speed trainer: all settings on one screen, so they can be set up before
// switching it on.
static const MenuItem kTrainerItems[] = {
    SETTING(kTrainerOn), SETTING(kTrainerStep), SETTING(kTrainerEvery), SETTING(kTrainerTarget),
};

// The amp block (NAM.md): on/off first; Model opens the list of /AMP.
// Release only with the gate on.
static const MenuItem kAmpItems[] = {
    SETTING(kAmpOn), {"Model", K::kScreen, kScreenAmp, 0, kAlways}, SETTING(kAmpIn),
    SETTING(kAmpBass), SETTING(kAmpMid), SETTING(kAmpTreble), SETTING(kAmpPresence),
    SETTING(kAmpOut), SETTING(kAmpGate), SETTING_IF(kAmpGateRel, kIsAmpGateOn), SETTING(kAmpCal),
};

static const MenuItem kDoublerItems[] = {
    SETTING(kDblOn), SETTING(kDblTime), SETTING(kDblWidth),
};

// The bass playing with the drums: on/off first, so it is one push away.
// The key is the styles' (songs have their own).
static const MenuItem kBassItems[] = {
    SETTING(kBassOn), SETTING(kBassVolume), SETTING(kBassKey), SETTING(kBassTone),
};

#undef SETTING
#undef SETTING_IF

#define PAGE(title, items) {title, items, sizeof(items) / sizeof(items[0])}
// In MenuPageId order.
static const MenuPage kPages[] = {
    PAGE("MENU", kTop),
    PAGE("DELAY", kDelayItems),
    PAGE("REVERB", kReverbItems),
    PAGE("LEVELS", kLevelItems),
    PAGE("DRUMS", kDrumItems),
    PAGE("TRAINER", kTrainerItems),
    PAGE("BASS", kBassItems),
    PAGE("AMP", kAmpItems),
    PAGE("DOUBLER", kDoublerItems),
};
static_assert(sizeof(kPages) / sizeof(kPages[0]) == kPageCount, "a page per MenuPageId");
#undef PAGE

const MenuPage* menuPages() { return kPages; }

Edit editOf(const ParamDef& d) {
  if (d.unit == Unit::kOnOff) return Edit::kToggle;
  if (d.choices && d.unit != Unit::kName) return Edit::kChoice;
  return Edit::kValue;
}

// ---- Engine --------------------------------------------------------------------

bool Menu::visible(const MenuItem& it) {
  return it.visibleIf == kNoItem || host_.get(it.visibleIf) != 0;
}

int Menu::visibleCount() {
  const MenuPage& pg = pages_[stack_[depth_].page];
  int n = 0;
  for (int i = 0; i < pg.count; i++) n += visible(pg.items[i]);
  return n;
}

const MenuItem* Menu::visibleItem(int n) {
  const MenuPage& pg = pages_[stack_[depth_].page];
  for (int i = 0; i < pg.count; i++) {
    if (!visible(pg.items[i])) continue;
    if (n-- == 0) return &pg.items[i];
  }
  return nullptr;
}

const MenuItem* Menu::current() {
  Level& lv = stack_[depth_];
  const int count = visibleCount();
  if (lv.cursor >= count) lv.cursor = count > 0 ? count - 1 : 0;
  return visibleItem(lv.cursor);
}

void Menu::range(const MenuItem& it, int* lo, int* hi) {
  const ParamDef& d = paramDef(it.id);
  *lo = d.min;
  *hi = d.max;
  host_.limits(it.id, lo, hi);
}

void Menu::open(int page) {
  depth_ = 0;
  stack_[0] = Level{(uint8_t)page, 0, 0};
  editing_ = false;
  screen_ = kNoItem;
}

Menu::Result Menu::turn(int detents, uint32_t now) {
  if (detents == 0) return kNothing;
  const MenuItem* it = current();
  if (!it) return kNothing;
  if (editing_) {
    const int factor = (lastTurnMs_ != 0 && now - lastTurnMs_ < kFastMs) ? kFastFactor : 1;
    lastTurnMs_ = now ? now : 1;
    int lo, hi;
    range(*it, &lo, &hi);
    int v = host_.get(it->id) + detents * paramDef(it->id).step * factor;
    v = v < lo ? lo : (v > hi ? hi : v);
    host_.set(it->id, v);
    return kRedraw;
  }
  Level& lv = stack_[depth_];
  const int count = visibleCount();
  int c = lv.cursor + detents;
  c = c < 0 ? 0 : (c >= count ? count - 1 : c);
  if (c == lv.cursor) return kNothing;
  lv.cursor = c;
  if (c < lv.top) lv.top = c;
  if (c >= lv.top + kRows) lv.top = c - kRows + 1;
  return kRedraw;
}

Menu::Result Menu::push() {
  const MenuItem* it = current();
  if (!it) return kNothing;
  if (editing_) {
    editing_ = false;
    return kRedraw;
  }
  switch (it->kind) {
    case Kind::kSubmenu:
      if (depth_ + 1 < (int)(sizeof(stack_) / sizeof(stack_[0]))) {
        stack_[++depth_] = Level{it->sub, 0, 0};
        return kRedraw;
      }
      return kNothing;
    case Kind::kScreen:
      screen_ = it->id;
      return kOpenScreen;
    case Kind::kSetting:
      break;
  }
  switch (editOf(paramDef(it->id))) {
    case Edit::kToggle:
      host_.set(it->id, host_.get(it->id) ? 0 : 1);
      return kRedraw;
    case Edit::kChoice: {
      int lo, hi;
      range(*it, &lo, &hi);
      const int v = host_.get(it->id) + 1;
      host_.set(it->id, v > hi ? lo : v);
      return kRedraw;
    }
    case Edit::kValue:
      editing_ = true;
      original_ = host_.get(it->id);
      lastTurnMs_ = 0;
      return kRedraw;
  }
  return kNothing;
}

Menu::Result Menu::back() {
  if (editing_) {
    const MenuItem* it = current();
    if (it) host_.set(it->id, original_);
    editing_ = false;
    return kRedraw;
  }
  if (depth_ > 0) {
    depth_--;
    return kRedraw;
  }
  return kExit;
}

// ---- Drawing -------------------------------------------------------------------

static void titleBar(Canvas& c, const char* left, const char* right) {
  c.clearPages(0, Canvas::kRowPages - 1);
  c.text(Canvas::kLeft, 0, left);
  if (right) c.textRight(Canvas::kW, 0, right);
  c.invertRow(0);
}

void Menu::draw(Canvas& c) {
  if (editing_) drawEditor(c);
  else drawList(c);
}

void Menu::drawList(Canvas& c) {
  c.clear();
  Level& lv = stack_[depth_];
  current();  // clamps the cursor if items were hidden
  const int count = visibleCount();
  if (lv.top > lv.cursor) lv.top = lv.cursor;
  if (lv.top + kRows > count) lv.top = count > kRows ? count - kRows : 0;
  // Where the cursor is ("3/9") when the page has more items than rows.
  char pos[8];
  if (count > kRows) snprintf(pos, sizeof(pos), "%d/%d", lv.cursor + 1, count);
  titleBar(c, pages_[lv.page].title, count > kRows ? pos : nullptr);
  for (int row = 0; row < kRows; row++) {
    const int n = lv.top + row;
    const MenuItem* it = visibleItem(n);
    if (!it) break;
    const int page = (row + 1) * Canvas::kRowPages;
    c.text(Canvas::kLeft, page, label(*it));
    char val[24] = "";
    if (host_.valueText(it->id, val, sizeof(val))) {
      // named value or screen row
    } else if (it->kind != Kind::kScreen && it->id != kNoItem) {
      formatValue(paramDef(it->id), host_.get(it->id), false, val, sizeof(val));
    } else {
      val[0] = 0;
    }
    if (val[0]) {
      // A value too long for the space after the label is cut short with a
      // '~' (the editor shows it in full).
      const int room = (Canvas::kW - Canvas::kLeft - Canvas::width(label(*it)) - Canvas::kCharW) / Canvas::kCharW;
      if (room > 1 && (int)strlen(val) > room) {
        val[room - 1] = '~';
        val[room] = 0;
      }
      c.textRight(Canvas::kW, page, val);
    }
    if (n == lv.cursor) c.invertRow(page);  // the cursor
  }
}

// A name centered on row 1.5, or wrapped onto rows 1-2 at the last space
// that leaves at most a row's worth on the first line.
static void drawName(Canvas& c, const char* s) {
  const int len = (int)strlen(s);
  if (len <= Canvas::kCols) {
    c.textCenter(3, s);
    return;
  }
  int cut = Canvas::kCols;
  while (cut > 0 && s[cut] != ' ') cut--;
  if (cut == 0) cut = Canvas::kCols;  // no space: break mid-word
  const char* rest = s + cut + (s[cut] == ' ');
  char line[Canvas::kCols + 1];
  snprintf(line, sizeof(line), "%.*s", cut, s);
  c.textCenter(2, line);
  snprintf(line, sizeof(line), "%s", rest);
  c.textCenter(4, line);
}

// x of value v on the editor's bar (inside the frame at x0, w wide).
static int barPos(int v, int lo, int hi, int x0, int w) {
  return x0 + 1 + (int)((long)(v - lo) * (w - 3) / (hi > lo ? hi - lo : 1));
}

void Menu::drawEditor(Canvas& c) {
  c.clear();
  const MenuItem* it = current();
  if (!it) return;
  titleBar(c, label(*it), nullptr);

  // The value: numbers big, names in the normal font (bigValue).
  char buf[32];
  const int v = host_.get(it->id);
  if (!host_.valueText(it->id, buf, sizeof(buf))) formatValue(paramDef(it->id), v, true, buf, sizeof(buf));
  if (bigValue(*it)) c.textCenter(2, buf, Font::kBig);
  else drawName(c, buf);

  // Position bar: from the low end, or from 0 for ranges that span it.
  int lo, hi;
  range(*it, &lo, &hi);
  const int x0 = 4, w = Canvas::kW - 8, y = 6 * 8 + 3, h = 10;
  c.frame(x0, y, w, h);
  const int from = barPos(lo < 0 && hi > 0 ? 0 : lo, lo, hi, x0, w), to = barPos(v, lo, hi, x0, w);
  c.fill(from < to ? from : to, y + 1, (from < to ? to - from : from - to) + 1, h - 2, true);
  if (lo < 0 && hi > 0) c.vline(barPos(0, lo, hi, x0, w), y - 2, y + h + 1);
}

}  // namespace ui
