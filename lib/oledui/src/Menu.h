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
#include "ParamDefs.h"

namespace ui {

// What a menu row can name: a setting (Param, 0 .. kValueCount-1), or one of
// these.
enum ItemId : uint8_t {
  // Read-only, for visibility.
  kIsReverbHall = kValueCount,  // 1 when the reverb is the hall
  kIsReverbGated,               // 1 when it is the gated reverb
  kIsDrumSong,                  // 1 when a drum song is selected
  kIsAmpGateOn,                 // 1 when the amp's noise gate is on
  // Screens opened from the menu (their row shows MenuHost::valueText).
  kScreenIR, kScreenMeter, kScreenStatus, kScreenAmp, kScreenTuner,
  // Actions of the panel's screens, sent with MenuHost::set.
  kIrLoad,      // load IR number `value` (0-based)
  kIrRescan,    // rescan the SD card
  kMeterReset,  // reset the clip counter and the peak holds
  kAmpLoad,     // load amp model number `value` (0-based)
  kNoItem = 0xFF,
};

// The pages of menuPages().
enum MenuPageId : uint8_t {
  kPageTop, kPageDelay, kPageReverb, kPageLevels, kPageDrums, kPageTrainer, kPageBass, kPageAmp, kPageDoubler,
  kPageCount
};

enum class Kind : uint8_t {
  kSetting,  // a Param: a toggle flips on push, a choice steps through its
             // names, a value opens the editor (min..max in steps); see editOf
  kSubmenu,  // push enters page `sub`; shows `id` (if any) as ON/OFF
  kScreen,   // push asks the caller to open screen `id`
};

struct MenuItem {
  const char* label;  // nullptr: the setting's label
  Kind kind;
  uint8_t id;         // Param or ItemId
  uint8_t sub;        // kSubmenu: page index
  uint8_t visibleIf;  // ItemId that must read nonzero; kNoItem = always
};

// How a setting row behaves on push.
enum class Edit : uint8_t { kToggle, kChoice, kValue };
Edit editOf(const ParamDef& d);

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

class Menu {
 public:
  enum Result : uint8_t {
    kNothing,     // nothing changed
    kRedraw,      // the menu changed: draw it again
    kExit,        // back from the top level: return to the home screen
    kOpenScreen,  // push on a kScreen item: see screen()
  };
  static constexpr int kRows = 3;  // item rows under the title (8x16 font)
  static constexpr uint32_t kFastMs = 40;
  static constexpr int kFastFactor = 5;

  Menu(const MenuPage* pages, MenuHost& host) : pages_(pages), host_(host) {}

  // At a page (the top level by default), cursor on its first item. BACK
  // from that page leaves the menu.
  void open(int page = kPageTop);
  Result turn(int detents, uint32_t nowMs);
  Result push();
  Result back();
  bool editing() const { return editing_; }
  // The editor's font is fixed per item, so it never changes while turning:
  // the 16x32 font for numbers (every one fits), 8x16 for names.
  static bool bigValue(const MenuItem& it) { return paramDef(it.id).unit != Unit::kName; }
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
  static const char* label(const MenuItem& it) { return it.label ? it.label : paramDef(it.id).label; }
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
