// Panel.h - the front panel: which screen is showing, and what the encoder
// and the two keys do on it. Events in, "redraw" out; settings change
// through MenuHost, and the screens are drawn from a UiModel, so the whole
// panel runs and is tested on a PC.
//
// Controls (DISPLAY.md, "Controls"):
//   home:    turn = move between the blocks (AMP, IR, DLY, REV, DRM) and
//            the tempo; push = open the block (the amp or IR list, or its
//            menu page); hold = the block on/off (IR: bypass; DRM, tempo:
//            drums start/stop); CONFIRM = drums start/stop; BACK = the menu
//   tempo:   push edits it in place: turn = BPM, applied live (x5 when
//            fast); push = keep; BACK = restore
//   menu:    as in Menu.h; CONFIRM works like push; BACK from the page it
//            opened at returns home
//   lists:   (IR, amp) turn = move; push or CONFIRM = load (stays on the
//            list); the last row rescans the card; BACK
//   meter:   turn = line-in sensitivity; CONFIRM = reset clips and peaks; BACK
//   status:  BACK
//   tuner:   (the guitar muted) BACK or push closes it
//   anywhere: hold BACK = the tuner
// After 30 s without input the panel returns home (not from the meter or
// the tuner, which are used while playing). Long names scroll: the list's cursor row,
// and home row 0 with the cursor on AMP (the model's name).
#pragma once

#include <stdint.h>

#include "Canvas.h"
#include "Menu.h"
#include "Screens.h"

namespace ui {

class Panel {
 public:
  enum Screen : uint8_t { kHome, kMenu, kIrList, kMeter, kStatus, kAmpList, kTuner };
  static constexpr uint32_t kTimeoutMs = 30000;
  static constexpr uint32_t kMarqueeMs = 400;  // the lists' and home's scrolling name

  // model: read for the IR and amp lists and the loaded ones; the caller
  // keeps it up to date.
  Panel(const MenuPage* pages, MenuHost& host, const UiModel& model) : menu_(pages, host), host_(host), m_(model) {}

  // Each returns true when the screen should be drawn again.
  bool turn(int detents, uint32_t nowMs);
  bool push(uint32_t nowMs);  // a short encoder press (on release)
  bool hold(uint32_t nowMs);  // the encoder held down
  bool confirm(uint32_t nowMs);
  bool back(uint32_t nowMs);
  bool backHold(uint32_t nowMs);  // BACK held down: the tuner
  bool tick(uint32_t nowMs);  // call often: the timeout, the marquee

  // The whole screen.
  void draw(Canvas& c, const UiModel& m);

  Screen screen() const { return screen_; }
  bool tempoEditing() const { return tempoEdit_; }
  int block() const { return block_; }
  int irCursor() const { return listCursor_; }  // in the open list (IR or amp)
  Menu& menu() { return menu_; }

 private:
  void touch(uint32_t nowMs) { lastInputMs_ = nowMs; }
  bool menuResult(Menu::Result r);
  void openList(Screen list, Screen from);
  FileList fileList() const { return screen_ == kAmpList ? FileList::kAmp : FileList::kIR; }
  bool toggle(uint8_t id);
  int marqueeOffset() const;

  Menu menu_;
  MenuHost& host_;
  const UiModel& m_;
  Screen screen_ = kHome, from_ = kHome;  // from_: where BACK goes from a screen
  uint8_t block_ = kBlockAmp;
  int listCursor_ = 0, listTop_ = 0, marquee_ = 0;
  uint32_t lastInputMs_ = 0, marqueeMs_ = 0;
  bool tempoEdit_ = false;
  int tempoOrig_ = 0;
  uint32_t tempoTurnMs_ = 0;
};

}  // namespace ui
