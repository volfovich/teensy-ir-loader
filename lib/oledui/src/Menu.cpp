#include "Menu.h"

#include <stdio.h>
#include <string.h>

#include "ui_port.h"

namespace ui {

// ---- The menu tree (DISPLAY.md, "Menu tree") --------------------------------

#define UI_LABEL(name, text) static const char UI_PROGMEM name[] = text
UI_LABEL(kMenu, "MENU");
UI_LABEL(kDelay, "DELAY");
UI_LABEL(kReverb, "REVERB");
UI_LABEL(kLevels, "LEVELS");
UI_LABEL(kDrums, "DRUMS");
UI_LABEL(kTrainerTitle, "TRAINER");
UI_LABEL(kLDrums, "Drums");
UI_LABEL(kLPlay, "Play");
UI_LABEL(kLStyle, "Style");
UI_LABEL(kLKit, "Kit");
UI_LABEL(kLSong, "Song");
UI_LABEL(kLLoopFrom, "Loop from");
UI_LABEL(kLLoopTo, "Loop to");
UI_LABEL(kLCrash, "Crash");
UI_LABEL(kLComp, "Comp");
UI_LABEL(kLTempo, "Tempo");
UI_LABEL(kLVolume, "Volume");
UI_LABEL(kLCountIn, "Count-in");
UI_LABEL(kLTrainer, "Trainer");
UI_LABEL(kLStep, "Step");
UI_LABEL(kLEvery, "Every");
UI_LABEL(kLTarget, "Target");
UI_LABEL(kLIr, "IR");
UI_LABEL(kLDelay, "Delay");
UI_LABEL(kLReverb, "Reverb");
UI_LABEL(kLLevels, "Levels");
UI_LABEL(kLBypass, "Bypass");
UI_LABEL(kLMeter, "Meter");
UI_LABEL(kLStatus, "Status");
UI_LABEL(kLOn, "On");
UI_LABEL(kLType, "Type");
UI_LABEL(kLMix, "Mix");
UI_LABEL(kLTime, "Time");
UI_LABEL(kLRepeats, "Repeats");
UI_LABEL(kLTone, "Tone");
UI_LABEL(kLMod, "Mod");
UI_LABEL(kLDuck, "Duck");
UI_LABEL(kLDrive, "Drive");
UI_LABEL(kLEra, "Era");
UI_LABEL(kLSize, "Size");
UI_LABEL(kLMode, "Mode");
UI_LABEL(kLPreDelay, "Pre-delay");
UI_LABEL(kLGate, "Gate");
UI_LABEL(kLGain, "Out gain");
UI_LABEL(kLLineIn, "Line-in sens");
UI_LABEL(kLLineOut, "Line-out");
UI_LABEL(kLPhones, "Headphones");
UI_LABEL(kCPing, "ping-pong");
UI_LABEL(kC80s, "studio 80s");
UI_LABEL(kCPlate, "plate");
UI_LABEL(kC80sReverb, "80s");
UI_LABEL(kCHall, "hall");
UI_LABEL(kCGated, "gated");
UI_LABEL(kCOff, "off");
UI_LABEL(kC2Bars, "2 bars");
UI_LABEL(kC4Bars, "4 bars");
UI_LABEL(kC8Bars, "8 bars");
UI_LABEL(kC16Bars, "16 bars");
UI_LABEL(kC1Bar, "1 bar");
UI_LABEL(kC3Bars, "3 bars");
#undef UI_LABEL

static const char* const UI_PROGMEM kTypeNames[] = {kCPing, kC80s};
static const char* const UI_PROGMEM kReverbTypeNames[] = {kCPlate, kC80sReverb};
static const char* const UI_PROGMEM kReverbModeNames[] = {kCHall, kCGated};
static const char* const UI_PROGMEM kCrashNames[] = {kCOff, kC2Bars, kC4Bars, kC8Bars, kC16Bars};
static const char* const UI_PROGMEM kCountNames[] = {kCOff, kC1Bar, kC2Bars, kC3Bars, kC4Bars};

using K = Kind;
using U = Unit;

static const MenuItem UI_PROGMEM kTop[] = {
    {kLIr, K::kScreen, kScreenIR, 0, 0, 0, 0, U::kNone, kNoItem, nullptr},
    {kLDelay, K::kSubmenu, kDelayOn, 1, 0, 0, 0, U::kNone, kNoItem, nullptr},
    {kLReverb, K::kSubmenu, kReverbOn, 2, 0, 0, 0, U::kNone, kNoItem, nullptr},
    {kLDrums, K::kSubmenu, kDrumsPlay, 4, 0, 0, 0, U::kNone, kNoItem, nullptr},
    {kLLevels, K::kSubmenu, kNoItem, 3, 0, 0, 0, U::kNone, kNoItem, nullptr},
    {kLBypass, K::kToggle, kBypass, 0, 0, 1, 1, U::kNone, kNoItem, nullptr},
    {kLMeter, K::kScreen, kScreenMeter, 0, 0, 0, 0, U::kNone, kNoItem, nullptr},
    {kLStatus, K::kScreen, kScreenStatus, 0, 0, 0, 0, U::kNone, kNoItem, nullptr},
};

static const MenuItem UI_PROGMEM kDelayItems[] = {
    {kLOn, K::kToggle, kDelayOn, 0, 0, 1, 1, U::kNone, kNoItem, nullptr},
    {kLType, K::kChoice, kDelayType, 0, 0, 1, 1, U::kNone, kNoItem, kTypeNames},
    {kLMix, K::kValue, kDelayMix, 0, 0, 100, 1, U::kPercent, kNoItem, nullptr},
    {kLTime, K::kValue, kDelayTime, 0, 20, 750, 5, U::kMs, kNoItem, nullptr},
    {kLRepeats, K::kValue, kDelayRepeats, 0, 0, 100, 1, U::kPercent, kNoItem, nullptr},
    {kLTone, K::kValue, kDelayTone, 0, 0, 100, 1, U::kPercent, kNoItem, nullptr},
    {kLMod, K::kValue, kDelayMod, 0, 0, 100, 1, U::kPercent, kNoItem, nullptr},
    {kLDuck, K::kValue, kDelayDuck, 0, 0, 100, 1, U::kPercent, kIsStudioDelay, nullptr},
    {kLDrive, K::kValue, kDelayDrive, 0, 0, 100, 1, U::kPercent, kIsStudioDelay, nullptr},
    {kLEra, K::kValue, kDelayEra, 0, 0, 100, 1, U::kPercent, kIsStudioDelay, nullptr},
};

// Type: plate or 80s; the 80s rows show only for the 80s reverb (Mod for
// the hall, Gate for the gated mode).
static const MenuItem UI_PROGMEM kReverbItems[] = {
    {kLOn, K::kToggle, kReverbOn, 0, 0, 1, 1, U::kNone, kNoItem, nullptr},
    {kLType, K::kChoice, kReverbType, 0, 0, 1, 1, U::kNone, kNoItem, kReverbTypeNames},
    {kLMode, K::kChoice, kReverbMode, 0, 0, 1, 1, U::kNone, kIsReverb80, kReverbModeNames},
    {kLMix, K::kValue, kReverbMix, 0, 0, 100, 1, U::kPercent, kNoItem, nullptr},
    {kLSize, K::kValue, kReverbSize, 0, 0, 100, 1, U::kPercent, kNoItem, nullptr},
    {kLTone, K::kValue, kReverbTone, 0, 0, 100, 1, U::kPercent, kNoItem, nullptr},
    {kLPreDelay, K::kValue, kReverbPreDelay, 0, 0, 150, 1, U::kMs, kIsReverb80, nullptr},
    {kLMod, K::kValue, kReverbMod, 0, 0, 100, 1, U::kPercent, kIsReverbHall, nullptr},
    {kLEra, K::kValue, kReverbEra, 0, 0, 100, 1, U::kPercent, kIsReverb80, nullptr},
    {kLGate, K::kValue, kReverbGate, 0, 100, 600, 10, U::kMs, kIsReverbGated, nullptr},
};

static const MenuItem UI_PROGMEM kLevelItems[] = {
    {kLGain, K::kValue, kOutGain, 0, -120, 48, 1, U::kHalfDb, kNoItem, nullptr},
    {kLLineIn, K::kValue, kLineIn, 0, 0, 15, 1, U::kNone, kNoItem, nullptr},
    {kLLineOut, K::kValue, kLineOut, 0, 13, 31, 1, U::kNone, kNoItem, nullptr},
    {kLPhones, K::kValue, kHeadphones, 0, 0, 100, 1, U::kPercent, kNoItem, nullptr},
};

// The style's, kit's and song's range ends come from MenuHost::limits (how
// many there are), their names from MenuHost::valueText; so do the loop's
// bars (the song's length; "off" for from = 0).
static const MenuItem UI_PROGMEM kDrumItems[] = {
    {kLPlay, K::kToggle, kDrumsPlay, 0, 0, 1, 1, U::kNone, kNoItem, nullptr},
    {kLStyle, K::kValue, kDrumStyle, 0, 0, 7, 1, U::kOneBased, kNoItem, nullptr},
    {kLSong, K::kValue, kDrumSong, 0, 0, 5, 1, U::kNone, kNoItem, nullptr},
    {kLLoopFrom, K::kValue, kDrumLoopFrom, 0, 0, 32, 1, U::kNone, kIsDrumSong, nullptr},
    {kLLoopTo, K::kValue, kDrumLoopTo, 0, 1, 32, 1, U::kNone, kIsDrumSong, nullptr},
    {kLKit, K::kValue, kDrumKit, 0, 0, 2, 1, U::kOneBased, kNoItem, nullptr},
    {kLCrash, K::kChoice, kDrumCrash, 0, 0, 4, 1, U::kNone, kNoItem, kCrashNames},
    {kLTempo, K::kValue, kDrumTempo, 0, 40, 240, 1, U::kBpm, kNoItem, nullptr},
    {kLVolume, K::kValue, kDrumVolume, 0, 0, 100, 1, U::kPercent, kNoItem, nullptr},
    {kLComp, K::kValue, kDrumComp, 0, 0, 100, 1, U::kPercent, kNoItem, nullptr},
    {kLCountIn, K::kChoice, kDrumCountIn, 0, 0, 4, 1, U::kNone, kNoItem, kCountNames},
    {kLTrainer, K::kSubmenu, kTrainerOn, 5, 0, 0, 0, U::kNone, kNoItem, nullptr},
};

// Speed trainer: all settings on one screen, so they can be set up before
// switching it on.
static const MenuItem UI_PROGMEM kTrainerItems[] = {
    {kLOn, K::kToggle, kTrainerOn, 0, 0, 1, 1, U::kNone, kNoItem, nullptr},
    {kLStep, K::kValue, kTrainerStep, 0, 1, 20, 1, U::kBpm, kNoItem, nullptr},
    {kLEvery, K::kValue, kTrainerEvery, 0, 1, 16, 1, U::kBars, kNoItem, nullptr},
    {kLTarget, K::kValue, kTrainerTarget, 0, 40, 240, 1, U::kBpm, kNoItem, nullptr},
};

#define UI_PAGE(title, items) {title, items, sizeof(items) / sizeof(items[0])}
static const MenuPage UI_PROGMEM kPages[] = {
    UI_PAGE(kMenu, kTop),
    UI_PAGE(kDelay, kDelayItems),
    UI_PAGE(kReverb, kReverbItems),
    UI_PAGE(kLevels, kLevelItems),
    UI_PAGE(kDrums, kDrumItems),
    UI_PAGE(kTrainerTitle, kTrainerItems),
};
#undef UI_PAGE

UI_COLD const MenuPage* menuPages() { return kPages; }

// ---- Formatting ----------------------------------------------------------------

// Half-dB steps as "-6.0", "+3.5", "0.0".
static UI_COLD void halfDb(int v, char* buf, int n) {
  const int a = v < 0 ? -v : v;
  snprintf(buf, n, UI_STR("%s%d.%d"), v < 0 ? UI_STR("-") : (v > 0 ? UI_STR("+") : UI_STR("")),
           a / 2, (a & 1) * 5);
}

UI_COLD void formatValue(const MenuItem& it, int v, bool spaced, char* buf, int n) {
  const char* sp = spaced ? UI_STR(" ") : UI_STR("");
  switch (it.kind) {
    case Kind::kToggle:
    case Kind::kSubmenu:
      snprintf(buf, n, UI_STR("%s"), v ? UI_STR("ON") : UI_STR("OFF"));
      return;
    case Kind::kChoice:
      snprintf(buf, n, UI_STR("%s"), (v >= it.min && v <= it.max) ? it.choices[v] : UI_STR("?"));
      return;
    default:
      break;
  }
  switch (it.unit) {
    case Unit::kPercent: snprintf(buf, n, UI_STR("%d%s%%"), v, sp); break;
    case Unit::kMs: snprintf(buf, n, UI_STR("%d%sms"), v, sp); break;
    case Unit::kBpm: snprintf(buf, n, UI_STR("%d BPM"), v); break;
    case Unit::kBars: snprintf(buf, n, UI_STR("%d bar%s"), v, v == 1 ? UI_STR("") : UI_STR("s")); break;
    case Unit::kOneBased: snprintf(buf, n, UI_STR("%d"), v + 1); break;
    case Unit::kHalfDb: {
      char num[16];
      halfDb(v, num, sizeof(num));
      snprintf(buf, n, UI_STR("%s%sdB"), num, sp);
      break;
    }
    default: snprintf(buf, n, UI_STR("%d"), v); break;
  }
}

// A range end for the editor, without the unit ("-60", "+24", "750").
static UI_COLD void formatBound(const MenuItem& it, int v, char* buf, int n) {
  if (it.unit == Unit::kHalfDb) {
    if (v % 2) halfDb(v, buf, n);
    else snprintf(buf, n, UI_STR("%s%d"), v > 0 ? UI_STR("+") : UI_STR(""), v / 2);
  } else {
    snprintf(buf, n, UI_STR("%d"), it.unit == Unit::kOneBased ? v + 1 : v);
  }
}

// ---- Engine --------------------------------------------------------------------

UI_COLD bool Menu::visible(const MenuItem& it) {
  return it.visibleIf == kNoItem || host_.get(it.visibleIf) != 0;
}

UI_COLD int Menu::visibleCount() {
  const MenuPage& pg = pages_[stack_[depth_].page];
  int n = 0;
  for (int i = 0; i < pg.count; i++) n += visible(pg.items[i]);
  return n;
}

UI_COLD const MenuItem* Menu::visibleItem(int n) {
  const MenuPage& pg = pages_[stack_[depth_].page];
  for (int i = 0; i < pg.count; i++) {
    if (!visible(pg.items[i])) continue;
    if (n-- == 0) return &pg.items[i];
  }
  return nullptr;
}

UI_COLD const MenuItem* Menu::current() {
  Level& lv = stack_[depth_];
  const int count = visibleCount();
  if (lv.cursor >= count) lv.cursor = count > 0 ? count - 1 : 0;
  return visibleItem(lv.cursor);
}

UI_COLD void Menu::range(const MenuItem& it, int* lo, int* hi) {
  *lo = it.min;
  *hi = it.max;
  host_.limits(it.id, lo, hi);
}

UI_COLD void Menu::open() {
  depth_ = 0;
  stack_[0] = Level{0, 0, 0};
  editing_ = false;
  screen_ = kNoItem;
}

UI_COLD Menu::Result Menu::turn(int detents, uint32_t now) {
  if (detents == 0) return kNothing;
  const MenuItem* it = current();
  if (!it) return kNothing;
  if (editing_) {
    const int factor = (lastTurnMs_ != 0 && now - lastTurnMs_ < kFastMs) ? kFastFactor : 1;
    lastTurnMs_ = now ? now : 1;
    int lo, hi;
    range(*it, &lo, &hi);
    int v = host_.get(it->id) + detents * it->step * factor;
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

UI_COLD Menu::Result Menu::push() {
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
    case Kind::kToggle:
      host_.set(it->id, host_.get(it->id) ? 0 : 1);
      return kRedraw;
    case Kind::kChoice: {
      int lo, hi;
      range(*it, &lo, &hi);
      const int v = host_.get(it->id) + 1;
      host_.set(it->id, v > hi ? lo : v);
      return kRedraw;
    }
    case Kind::kValue:
      editing_ = true;
      original_ = host_.get(it->id);
      lastTurnMs_ = 0;
      return kRedraw;
    case Kind::kScreen:
      screen_ = it->id;
      return kOpenScreen;
  }
  return kNothing;
}

UI_COLD Menu::Result Menu::back() {
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

static UI_COLD void titleBar(Canvas& c, const char* left, const char* right) {
  c.clearPages(0, 0);
  c.text(1, 0, left);
  if (right) c.textRight(Canvas::kW - 1, 0, right);
  c.invertPage(0);
}

UI_COLD void Menu::draw(Canvas& c) {
  if (editing_) drawEditor(c);
  else drawList(c);
}

UI_COLD void Menu::drawList(Canvas& c) {
  c.clear();
  Level& lv = stack_[depth_];
  current();  // clamps the cursor if items were hidden
  const int count = visibleCount();
  if (lv.top > lv.cursor) lv.top = lv.cursor;
  if (lv.top + kRows > count) lv.top = count > kRows ? count - kRows : 0;
  // More items than rows: scroll marks on the title bar.
  const char* marks = nullptr;
  if (count > kRows) {
    const bool up = lv.top > 0, down = lv.top + kRows < count;
    marks = up && down ? UI_STR("^v") : (up ? UI_STR("^ ") : UI_STR(" v"));
  }
  titleBar(c, pages_[lv.page].title, marks);
  for (int row = 0; row < kRows; row++) {
    const int n = lv.top + row;
    const MenuItem* it = visibleItem(n);
    if (!it) break;
    const int page = row + 1;
    if (n == lv.cursor) c.text(0, page, UI_STR(">"));
    c.text(12, page, it->label);
    char val[24] = "";
    if (host_.valueText(it->id, val, sizeof(val))) {
      // named value or screen row
    } else if (it->kind != Kind::kScreen && it->id != kNoItem) {
      formatValue(*it, host_.get(it->id), false, val, sizeof(val));
    } else {
      val[0] = 0;
    }
    if (val[0]) {
      // A value too long for the space after the label is cut short with a
      // '~' (the editor shows it in full).
      const int room = (Canvas::kW - 2 - (12 + Canvas::width(it->label) + Canvas::kCharW)) / Canvas::kCharW;
      if (room > 1 && (int)strlen(val) > room) {
        val[room - 1] = '~';
        val[room] = 0;
      }
      c.textRight(Canvas::kW - 2, page, val);
    }
    if (n == lv.cursor) c.invertPage(page);
  }
}

// x of value v on the editor's bar (inside the frame at x0, w wide).
static UI_COLD int barPos(int v, int lo, int hi, int x0, int w) {
  return x0 + 1 + (int)((long)(v - lo) * (w - 3) / (hi > lo ? hi - lo : 1));
}

UI_COLD void Menu::drawEditor(Canvas& c) {
  c.clear();
  const MenuItem* it = current();
  if (!it) return;
  char buf[32];
  snprintf(buf, sizeof(buf), UI_STR("%s > %s"), pages_[stack_[depth_].page].title, it->label);
  titleBar(c, buf, nullptr);

  // The value big, or small if it is a name too long for the 2x font.
  const int v = host_.get(it->id);
  if (!host_.valueText(it->id, buf, sizeof(buf))) formatValue(*it, v, true, buf, sizeof(buf));
  if (Canvas::width(buf, true) <= Canvas::kW) c.textCenter(2, buf, true);
  else c.textCenter(2, buf);

  // Position bar: from the low end, or from 0 for ranges that span it.
  int lo, hi;
  range(*it, &lo, &hi);
  const int x0 = 4, w = Canvas::kW - 8, y = 4 * 8 + 1, h = 6;
  c.frame(x0, y, w, h);
  const int from = barPos(lo < 0 && hi > 0 ? 0 : lo, lo, hi, x0, w), to = barPos(v, lo, hi, x0, w);
  c.fill(from < to ? from : to, y + 1, (from < to ? to - from : from - to) + 1, h - 2, true);
  if (lo < 0 && hi > 0) c.vline(barPos(0, lo, hi, x0, w), y - 1, y + h);

  formatBound(*it, lo, buf, sizeof(buf));
  c.text(x0, 5, buf);
  formatBound(*it, hi, buf, sizeof(buf));
  c.textRight(Canvas::kW - x0, 5, buf);
  c.text(0, 7, UI_STR("push=ok"));
  c.textRight(Canvas::kW, 7, UI_STR("back=cancel"));
}

}  // namespace ui
