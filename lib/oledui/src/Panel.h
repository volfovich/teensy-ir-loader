// Panel.h - the front panel: which screen is showing, and what the encoder
// and the two keys do on it. Events in, "redraw" out; settings change
// through MenuHost, and the screens are drawn from a UiModel, so the whole
// panel runs and is tested on a PC.
//
// Controls (DISPLAY.md, "Controls"):
//   home:    turn = move between the blocks (IR, DLY, REV, DRM);
//            push = open the block (IR list, or its menu page);
//            hold = the block on/off (IR: bypass; DRM: start/stop);
//            CONFIRM = drums start/stop; BACK = the menu
//   menu:    as in Menu.h; CONFIRM works like push; BACK from the page it
//            opened at returns home
//   IR list: turn = move; push or CONFIRM = load (stays on the list); BACK
//   meter:   turn = line-in sensitivity; CONFIRM = reset clips and peaks; BACK
//   status:  BACK
// After 30 s without input the panel returns home (not from the meter,
// which is used while playing).
#pragma once

#include <stdint.h>

#include "Canvas.h"
#include "Menu.h"
#include "Screens.h"

namespace ui {

class Panel {
 public:
  enum Screen : uint8_t { kHome, kMenu, kIrList, kMeter, kStatus };
  static constexpr uint32_t kTimeoutMs = 30000;
  static constexpr uint32_t kMarqueeMs = 400;  // the IR list's scrolling name

  // model: read for the IR count and the loaded IR; the caller keeps it up
  // to date.
  Panel(const MenuPage* pages, MenuHost& host, const UiModel& model) : menu_(pages, host), host_(host), m_(model) {}

  // Each returns true when the screen should be drawn again.
  bool turn(int detents, uint32_t nowMs);
  bool push(uint32_t nowMs);  // a short encoder press (on release)
  bool hold(uint32_t nowMs);  // the encoder held down
  bool confirm(uint32_t nowMs);
  bool back(uint32_t nowMs);
  bool tick(uint32_t nowMs);  // call often: the timeout, the marquee

  // The whole screen.
  void draw(Canvas& c, const UiModel& m);

  Screen screen() const { return screen_; }
  int block() const { return block_; }
  int irCursor() const { return irCursor_; }
  Menu& menu() { return menu_; }

 private:
  void touch(uint32_t nowMs) { lastInputMs_ = nowMs; }
  bool menuResult(Menu::Result r);
  void openIrList(Screen from);
  bool toggle(uint8_t id);
  int marqueeOffset() const;

  Menu menu_;
  MenuHost& host_;
  const UiModel& m_;
  Screen screen_ = kHome, from_ = kHome;  // from_: where BACK goes from a screen
  uint8_t block_ = kBlockIR;
  int irCursor_ = 0, irTop_ = 0, marquee_ = 0;
  uint32_t lastInputMs_ = 0, marqueeMs_ = 0;
};

}  // namespace ui
