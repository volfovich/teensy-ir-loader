// Panel.h - the front panel: which screen is showing, and what the encoder
// and the two keys do on it. Events in, "redraw" out; settings change
// through MenuHost, and the screens are drawn from a UiModel, so the whole
// panel runs and is tested on a PC.
//
// Controls (DISPLAY.md, "Controls"):
//   home:    turn = move between the blocks (AMP, NG, IR, DLY, REV), the
//            tempo, the style and TRN; push = open the block (the amp or IR
//            list, or its menu page); hold = the block on/off (IR: bypass;
//            tempo, style: drums start/stop); TRN: push or hold = the speed
//            trainer on/off; CONFIRM = drums start/stop; BACK = the menu
//   tempo, style: push edits it in place: turn = BPM (x5 when fast) or the
//            style, applied live (a drum song is switched off); push = keep;
//            BACK = restore (the style's song too)
//   menu:    as in Menu.h; CONFIRM works like push; BACK from the page it
//            opened at returns home
//   lists:   (IR, amp) turn = move; push or CONFIRM = load (stays on the
//            list); the last row rescans the card; BACK. The IR list plays
//            the IR under the cursor once it rests kPreviewMs, without saving
//            it: push or CONFIRM keeps it (saved, marked *), leaving without
//            (BACK, the timeout) loads the saved IR again
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
  static constexpr uint32_t kPreviewMs = 150;  // the IR list: rest before the cursor's IR plays

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
  // The home item being edited in place (kBlockTempo, kBlockStyle), or -1.
  int editing() const { return editing_; }
  int block() const { return block_; }
  int irCursor() const { return listCursor_; }  // in the open list (IR or amp)
  Menu& menu() { return menu_; }

 private:
  void touch(uint32_t nowMs) { lastInputMs_ = nowMs; }
  bool menuResult(Menu::Result r);
  void openList(Screen list, Screen from);
  void leaveIrList();  // the IR list closes without a click: back to the saved IR
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
  // The IR list: the IR playing, the one saved, and when the cursor's plays (0 = none due).
  int irPlaying_ = -1, irSaved_ = -1;
  uint32_t previewAt_ = 0;
  int editing_ = -1;
  int editOrig_ = 0, editSong_ = 0;  // the value (and the song) before editing
  uint32_t editTurnMs_ = 0;
};

}  // namespace ui
