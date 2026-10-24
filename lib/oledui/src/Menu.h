// Menu.h - the menu tree and the engine that browses and edits it.
//
// The tree is plain data in flash: pages of items, each naming a setting by
// an ItemId. Reading and changing settings goes through MenuHost, which the
// firmware implements with g_settings and the AudioGraph setters (and the
// host tests with a fake), so the whole menu can be tested and rendered on a
// PC.
//
// Controls (DISPLAY.md):
//   list:    turn = move the cursor; push = enter / start editing / toggle /
//            open; back = up one level (from the top: leave the menu)
//   editing: turn = change the value, applied live; push = keep;
//            back = restore the value from before editing
// Detents less than 40 ms apart count x5 while editing.
#pragma once

#include <stdint.h>

#include "Canvas.h"

namespace ui {

enum ItemId : uint8_t {
  kNoItem = 0,
  // Settings.
  kDelayOn, kDelayType, kDelayMix, kDelayTime, kDelayRepeats, kDelayTone, kDelayMod,
  kDelayDuck, kDelayDrive, kDelayEra,
  kReverbOn, kReverbMix, kReverbSize, kReverbTone,
  kReverbType,   // 0 plate, 1 80s
  kReverbMode,   // 80s: 0 hall, 1 gated
  kReverbPreDelay, kReverbMod, kReverbEra, kReverbGate,
  kOutGain,  // half-dB steps
  kLineIn, kLineOut, kHeadphones,
  kBypass,
  kDrumsPlay, kDrumStyle, kDrumKit,
  kDrumSong,                 // 0 = off (the style), else 1-based
  kDrumLoopFrom, kDrumLoopTo,  // bars of the song; from 0 = no loop
  kDrumCrash,  // 0 off, 1..4 = every 2, 4, 8, 16 bars
  kDrumTempo, kDrumVolume, kDrumComp, kDrumCountIn,
  kTrainerOn, kTrainerStep, kTrainerEvery, kTrainerTarget,
  // Read-only, for visibility.
  kIsStudioDelay,  // 1 when the delay type is studio 80s
  kIsReverb80,     // 1 when the reverb type is 80s
  kIsReverbHall,   // 1 when it is the 80s hall
  kIsReverbGated,  // 1 when it is the 80s gated reverb
  kIsDrumSong,     // 1 when a drum song is selected
  // Screens opened from the menu (their row shows MenuHost::valueText).
  kScreenIR, kScreenMeter, kScreenStatus,
};

enum class Kind : uint8_t {
  kSubmenu,  // push enters page `sub`; shows `id` (if any) as ON/OFF
  kToggle,   // push flips 0/1
  kChoice,   // push steps through `choices`
  kValue,    // push starts editing min..max in `step`s
  kScreen,   // push asks the caller to open screen `id`
};

enum class Unit : uint8_t {
  kNone, kPercent, kMs, kHalfDb, kBpm, kBars,
  kOneBased,  // an index shown from 1 (the editor's range ends)
};

struct MenuItem {
  const char* label;
  Kind kind;
  uint8_t id;         // ItemId
  uint8_t sub;        // kSubmenu: page index
  int16_t min, max, step;
  Unit unit;
  uint8_t visibleIf;  // ItemId that must read nonzero; kNoItem = always
  const char* const* choices;  // kChoice: max + 1 names (list, editor)
};

struct MenuPage {
  const char* title;
  const MenuItem* items;
  uint8_t count;
};

// The firmware's menu tree (Menu.cpp). Page 0 is the top level.
const MenuPage* menuPages();

class MenuHost {
 public:
  virtual int get(uint8_t id) = 0;
  virtual void set(uint8_t id, int value) = 0;
  // Overrides an item's range (e.g. the delay time's maximum from config.h).
  virtual void limits(uint8_t id, int* lo, int* hi) { (void)id; (void)lo; (void)hi; }
  // Value text instead of the number, for kScreen rows ("3/12" next to IR)
  // and named values (drum style, kit, song, loop bars). Returns false to use
  // the number.
  virtual bool valueText(uint8_t id, char* buf, int n) { (void)id; (void)buf; (void)n; return false; }

 protected:
  ~MenuHost() = default;
};

// Formats v in an item's unit: compact for lists ("380ms"), spaced for the
// editor ("380 ms").
void formatValue(const MenuItem& it, int v, bool spaced, char* buf, int n);

class Menu {
 public:
  enum Result : uint8_t {
    kNothing,     // nothing changed
    kRedraw,      // the menu changed: draw it again
    kExit,        // back from the top level: return to the home screen
    kOpenScreen,  // push on a kScreen item: see screen()
  };
  static constexpr int kRows = 7;  // item rows under the title
  static constexpr uint32_t kFastMs = 40;
  static constexpr int kFastFactor = 5;

  Menu(const MenuPage* pages, MenuHost& host) : pages_(pages), host_(host) {}

  void open();  // to the top level, cursor on the first item
  Result turn(int detents, uint32_t nowMs);
  Result push();
  Result back();
  bool editing() const { return editing_; }
  uint8_t screen() const { return screen_; }
  void draw(Canvas& c);

  // For tests: the current page, the cursor's item.
  int pageIndex() const { return stack_[depth_].page; }
  const MenuItem* current();

 private:
  struct Level { uint8_t page, cursor, top; };
  int visibleCount();
  const MenuItem* visibleItem(int n);  // n-th visible item of the page
  bool visible(const MenuItem& it);
  void range(const MenuItem& it, int* lo, int* hi);
  void drawList(Canvas& c);
  void drawEditor(Canvas& c);

  const MenuPage* pages_;
  MenuHost& host_;
  Level stack_[4] = {};
  int depth_ = 0;
  bool editing_ = false;
  int original_ = 0;
  uint32_t lastTurnMs_ = 0;
  uint8_t screen_ = kNoItem;
};

}  // namespace ui
