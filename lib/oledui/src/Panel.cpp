#include "Panel.h"

#include <string.h>


namespace ui {

// A scrolling name waits this many steps at each end.
static constexpr int kMarqueePause = 3;

bool Panel::toggle(uint8_t id) {
  host_.set(id, host_.get(id) ? 0 : 1);
  return true;
}

// A list opens on the loaded file, about in the middle of the screen.
void Panel::openList(Screen list, Screen from) {
  from_ = from;
  screen_ = list;
  const int count = fileCount(m_, fileList()), current = fileCurrent(m_, fileList());
  listCursor_ = current >= 0 && current < count ? current : 0;
  const int entries = count + 1;  // the files and "Rescan SD card"
  int top = listCursor_ - kIrBrowserRows / 2;
  if (top > entries - kIrBrowserRows) top = entries - kIrBrowserRows;
  listTop_ = top < 0 ? 0 : top;
  marquee_ = 0;
}

bool Panel::menuResult(Menu::Result r) {
  switch (r) {
    case Menu::kExit:
      screen_ = kHome;
      return true;
    case Menu::kOpenScreen:
      if (menu_.screen() == kScreenIR || menu_.screen() == kScreenAmp) {
        openList(menu_.screen() == kScreenAmp ? kAmpList : kIrList, kMenu);
      } else {
        from_ = kMenu;
        screen_ = menu_.screen() == kScreenMeter ? kMeter : (menu_.screen() == kScreenTuner ? kTuner : kStatus);
      }
      return true;
    case Menu::kRedraw:
      return true;
    default:
      return false;
  }
}

bool Panel::turn(int detents, uint32_t now) {
  if (detents == 0) return false;
  touch(now);
  switch (screen_) {
    case kHome: {
      if (tempoEdit_) {
        const int factor = (tempoTurnMs_ != 0 && now - tempoTurnMs_ < Menu::kFastMs) ? Menu::kFastFactor : 1;
        tempoTurnMs_ = now ? now : 1;
        const ParamDef& d = paramDef(kDrumTempo);
        int v = host_.get(kDrumTempo) + detents * factor;
        v = v < d.min ? d.min : (v > d.max ? d.max : v);
        host_.set(kDrumTempo, v);
        return true;
      }
      int b = block_ + detents;
      b = b < 0 ? 0 : (b >= kHomeBlocks ? kHomeBlocks - 1 : b);
      if (b == block_) return false;
      block_ = (uint8_t)b;
      marquee_ = 0;
      marqueeMs_ = now;
      return true;
    }
    case kMenu:
      return menuResult(menu_.turn(detents, now));
    case kIrList:
    case kAmpList: {
      const int count = fileCount(m_, fileList());
      int c = listCursor_ + detents;
      c = c < 0 ? 0 : (c > count ? count : c);  // count: the Rescan row
      if (c == listCursor_) return false;
      listCursor_ = c;
      if (c < listTop_) listTop_ = c;
      if (c >= listTop_ + kIrBrowserRows) listTop_ = c - kIrBrowserRows + 1;
      marquee_ = 0;
      marqueeMs_ = now;
      return true;
    }
    case kMeter: {
      // The line-in sensitivity (Levels > Line-in sens).
      const ParamDef& d = paramDef(kLineIn);
      int v = host_.get(kLineIn) + detents;
      v = v < d.min ? d.min : (v > d.max ? d.max : v);
      host_.set(kLineIn, v);
      return true;
    }
    default:
      return false;
  }
}

bool Panel::push(uint32_t now) {
  touch(now);
  switch (screen_) {
    case kHome:
      if (block_ == kBlockTempo) {
        // Push starts editing the tempo in place, and push again keeps it.
        tempoEdit_ = !tempoEdit_;
        tempoOrig_ = host_.get(kDrumTempo);
        tempoTurnMs_ = 0;
        return true;
      }
      if (block_ == kBlockIR || block_ == kBlockAmp) {
        openList(block_ == kBlockAmp ? kAmpList : kIrList, kHome);
        return true;
      }
      menu_.open(block_ == kBlockDelay ? kPageDelay : (block_ == kBlockReverb ? kPageReverb : kPageDrums));
      screen_ = kMenu;
      return true;
    case kMenu:
      return menuResult(menu_.push());
    case kIrList:
    case kAmpList:
      if (listCursor_ >= fileCount(m_, fileList())) host_.set(kIrRescan, 1);
      else host_.set(screen_ == kAmpList ? kAmpLoad : kIrLoad, listCursor_);
      return true;
    case kTuner:
      screen_ = from_;
      return true;
    default:
      return false;
  }
}

bool Panel::hold(uint32_t now) {
  touch(now);
  if (screen_ != kHome) return false;
  // The IR block is on when not bypassed: flipping bypass flips it.
  static const uint8_t kOnOff[kHomeBlocks] = {kAmpOn, kBypass, kDelayOn, kReverbOn, kDrumsPlay, kDrumsPlay};
  return toggle(kOnOff[block_]);
}

bool Panel::confirm(uint32_t now) {
  touch(now);
  switch (screen_) {
    case kHome:
      return toggle(kDrumsPlay);
    case kMenu:
      return menuResult(menu_.push());
    case kIrList:
    case kAmpList:
      return push(now);
    case kMeter:
      host_.set(kMeterReset, 1);
      return true;
    default:
      return false;
  }
}

bool Panel::back(uint32_t now) {
  touch(now);
  switch (screen_) {
    case kHome:
      if (tempoEdit_) {
        host_.set(kDrumTempo, tempoOrig_);
        tempoEdit_ = false;
        return true;
      }
      menu_.open();
      screen_ = kMenu;
      return true;
    case kMenu:
      return menuResult(menu_.back());
    default:
      screen_ = from_;
      marquee_ = 0;
      return true;
  }
}

// The press before the hold has already gone back one step: the tuner
// returns to where that left the panel (home or the menu).
bool Panel::backHold(uint32_t now) {
  touch(now);
  if (screen_ == kTuner) return false;
  tempoEdit_ = false;
  from_ = screen_ == kMenu ? kMenu : kHome;
  screen_ = kTuner;
  return true;
}

// The characters the cursor row's name is scrolled by: it waits at the
// start, scrolls to the end, waits again and starts over.
int Panel::marqueeOffset() const {
  return marquee_ > kMarqueePause ? marquee_ - kMarqueePause : 0;
}

bool Panel::tick(uint32_t now) {
  if (tempoEdit_ && now - lastInputMs_ >= kTimeoutMs) {
    tempoEdit_ = false;  // keeps the tempo
    return true;
  }
  if (screen_ != kHome && screen_ != kMeter && screen_ != kTuner && now - lastInputMs_ >= kTimeoutMs) {
    screen_ = kHome;
    return true;
  }
  // The scrolling name: the open list's cursor row, or home row 0 on AMP.
  char name[64];
  int room;
  if (screen_ == kIrList || screen_ == kAmpList) {
    if (listCursor_ >= fileCount(m_, fileList())) return false;
    listName(m_, fileList(), listCursor_, name, sizeof(name));
    room = kIrBrowserNameChars;
  } else if (screen_ == kHome && block_ == kBlockAmp && !tempoEdit_) {
    homeName(m_, block_, name, sizeof(name));
    room = kHomeNameChars;
  } else {
    return false;
  }
  if (now - marqueeMs_ < kMarqueeMs) return false;
  marqueeMs_ = now;
  const int over = (int)strlen(name) - room;
  if (over <= 0) return false;
  if (++marquee_ > kMarqueePause + over + kMarqueePause) marquee_ = 0;
  return true;
}

void Panel::draw(Canvas& c, const UiModel& m) {
  switch (screen_) {
    case kHome:
      drawHome(c, m, block_, tempoEdit_, block_ == kBlockAmp ? marqueeOffset() : 0);
      break;
    case kMenu:
      menu_.draw(c);
      break;
    case kIrList:
    case kAmpList: {
      const int count = fileCount(m, fileList());
      if (listCursor_ > count) listCursor_ = count;  // after a rescan found fewer
      if (listTop_ > listCursor_) listTop_ = listCursor_;
      drawFileList(c, m, fileList(), listCursor_, listTop_, marqueeOffset());
      break;
    }
    case kMeter:
      drawMeterScreen(c, m);
      break;
    case kStatus:
      drawStatus(c, m);
      break;
    case kTuner:
      drawTuner(c, m);
      break;
  }
}

}  // namespace ui
